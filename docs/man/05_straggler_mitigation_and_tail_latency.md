# Man 05: Straggler Mitigation & Tail-Latency Scheduling

## 1. The Straggler Problem in Distributed Systems

In distributed synchronous computing (such as multi-GPU gradient reduction or batch processing), the entire cluster moves at the speed of its **slowest node**.

```
Node 0 (Fast):  [   Compute   ][ All-Reduce ]════════════════════► WAITING FOR NODE 2...
Node 1 (Fast):  [   Compute   ][ All-Reduce ]════════════════════► WAITING FOR NODE 2...
Node 2 (SLOW):  [       Compute (STRAGGLER)       ][ All-Reduce ] ──► Barrier Released
Node 3 (Fast):  [   Compute   ][ All-Reduce ]════════════════════► WAITING FOR NODE 2...
```

* **Tail Latency Collapse**: Even if $99\%$ of chunks finish in $100\ \mu\text{s}$, a single degraded core or throttled network link dragging at $2\text{ ms}$ degrades the overall collective completion time from $100\ \mu\text{s}$ to $2{,}000\ \mu\text{s}$ ($20\times$ slowdown!).
* **Root Causes in Production**: Thermal throttling, background OS interrupts, noisy cloud hypervisor neighbors (CPU steal time), degraded Ethernet cables, or asymmetric core layouts (e.g. Intel P-cores vs. E-cores).

---

## 2. Online Straggler Detection Architecture

Adder implements real-time straggler detection in `straggler/detector.h/.cc` without offline profiling or external monitoring agents.

### 2.1 Online Statistical Estimation (EMA)
Rather than maintaining an unbounded history buffer, each node's completion times are tracked using an **Exponential Moving Average (EMA)** of both mean ($\mu$) and variance ($\sigma^2$):

For each observed round latency $x$:
$$\Delta = x - \mu_{k-1}$$
$$\mu_k = \mu_{k-1} + \alpha \Delta$$
$$\sigma_k^2 = (1 - \alpha) \left(\sigma_{k-1}^2 + \alpha \Delta^2\right)$$

* Smoothing parameter $\alpha \in [0.1, 0.2]$ balances sensitivity against transient noise.
* Standard deviation is computed as $\sigma = \sqrt{\sigma_k^2}$.

### 2.2 Trigger Condition: $2\sigma$ Thresholding for 3 Consecutive Rounds
A node is officially marked as a straggler if and only if:
$$\text{Latency} > \mu_k + 2\sigma_k \quad \text{for } 3 \text{ consecutive rounds}$$

> [!IMPORTANT]
> **Why 3 Consecutive Rounds?**
> A single slow round can be caused by a transient Linux kernel interrupt or page fault. Triggering schedule rebalancing immediately on a single spike causes **false-positive thrashing**. Requiring 3 consecutive slow rounds ensures the system only responds to persistent hardware degradation.

---

## 3. Dynamic Workload Rebalancing

When a straggler is detected, Adder's `AdaptiveScheduler` (`straggler/scheduler.h/.cc`) computes a new chunk distribution.

### 3.1 The Conservation Invariant
To guarantee mathematical correctness and prevent dropped or duplicated tensor data, the scheduler strictly enforces the **Conservation Invariant**:

$$\sum_{i=0}^{N-1} \text{chunks}_i = \text{Total Chunks} \quad (\forall \text{ epochs})$$

### 3.2 Rebalancing Algorithm
1. **Identify Stragglers**: Query the `StragglerDetector` for all nodes where `is_straggler == true`.
2. **Shed Workload**: Cut the straggler's chunk allocation by $50\%$ (down to a minimum floor of 1 chunk).
3. **Redistribute**: Evenly allocate the shed chunks among the remaining fast (non-straggler) nodes.
4. **Assert Invariant**: Validate that $\sum \text{chunks}_i == \text{Total Chunks}$.

```
Initial Schedule (Uniform, 16 Chunks across 4 Nodes):
Node 0: [ 4 chunks ]
Node 1: [ 4 chunks ]
Node 2: [ 4 chunks ] (Straggler detected!)
Node 3: [ 4 chunks ]

Rebalanced Schedule (Epoch 1):
Node 0: [ 5 chunks ] (+1)
Node 1: [ 5 chunks ] (+1)
Node 2: [ 2 chunks ] (-2 shed)
Node 3: [ 4 chunks ]
Total = 5 + 5 + 2 + 4 = 16 Chunks (Invariant Preserved)
```

---

## 4. Out-of-Band TCP Control Channel & Epoch Synchronization

Changing collective schedules mid-flight across distributed nodes can easily cause **deadlocks** or **split-brain state**:
* If Node 0 switches to the new schedule on Round 10, but Node 1 does not switch until Round 11, Node 0 sends chunks that Node 1 is not expecting to receive.

Adder prevents this through an **out-of-band TCP epoch synchronization protocol**:

```
Rank 0 (Coordinator)                                  Worker Nodes
      │                                                     │
      ├── Detects Straggler on Round k                      │
      ├── Computes Next Schedule (Epoch E + 1)              │
      ├── Sets Future Start Round: R >= k + 2               │
      │                                                     │
      ├─── TCP Broadcast: {Epoch: E+1, StartRound: R} ─────►│
      │                                                     │
      │  [ Continue executing Round k with Old Schedule ]   │
      │  [ Continue executing Round k+1 with Old Schedule ] │
      │                                                     │
      ▼                                                     ▼
═════════════════════ SYNCHRONOUS SWITCH AT ROUND R ══════════════════════
      │                                                     │
      └─── Both switch to Epoch E+1 schedule simultaneously ─┘
```

### The 3 Epoch Rules:
1. **Future Round Offset**: The start round $R$ must be scheduled at least 2 rounds in advance ($R \ge \text{current} + 2$) to account for network propagation delay.
2. **Zero Mid-Round Switching**: A node **never** alters its active schedule during an ongoing reduction round.
3. **Synchronous Transition**: Every node checks for pending epoch messages at the round boundary before initiating the first step of round $R$.

---

## 5. Synthetic Straggler Injection

To validate straggler mitigation in benchmarks (`straggler/main.cc`), synthetic perturbations can be injected via three mechanisms:

1. **User-Space Software Delay (`shared::busy_poll`)**:
   Pins a core in an active spin loop (`_mm_pause()`) for a fixed duration (e.g. $500\ \mu\text{s}$) prior to posting send work requests.
2. **Linux `cgroups v2` CPU Quota Throttling**:
   Applies hard CPU bandwidth caps via `/sys/fs/cgroup/cpu.max` (e.g. `20000 100000` limits the process to 20% of one core), simulating thermal throttling.
3. **Network Emulation (`tc-netem`)**:
   Applies packet-level delay and jitter across isolated network namespaces:
   ```bash
   sudo tc qdisc add dev veth_cli root netem delay 500us 50us
   ```
