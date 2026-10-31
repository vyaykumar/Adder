# Man 07: Hardware Topology, Cache Clusters, & Linux Tuning

## 1. CPU Topology & NUMA Architecture

In microsecond-scale distributed systems, physical CPU architecture dictates latency and bandwidth ceilings:

```
[ Socket 0 / NUMA Node 0 ]                     [ Socket 1 / NUMA Node 1 ]
┌────────────────────────┐                     ┌────────────────────────┐
│ CPU Cores 0, 1, 2, 3   │                     │ CPU Cores 4, 5, 6, 7   │
│ Local L3 Cache (32 MB) │                     │ Local L3 Cache (32 MB) │
│ Local RAM Channels     │                     │ Local RAM Channels     │
└───────────┬────────────┘                     └───────────┬────────────┘
            │                                              │
            └─────────────── UPI / Infinity Fabric ────────┘
                                    │
                               PCIe Root 0
                                    ▼
                         [ Hardware RDMA NIC ]
```

### The NUMA Penalty:
If an RDMA completion polling thread runs on **Socket 1**, but the PCIe Network Interface Card is physically wired to the PCIe root complex of **Socket 0**:
* Every PCIe DMA transaction and MMIO doorbell must cross the inter-socket interconnect (Intel UPI or AMD Infinity Fabric).
* Adds **$100\text{--}200\text{ ns}$ of latency** per operation and degrades peak throughput.
* **Rule**: Always pin RDMA polling threads to the specific NUMA node physically local to the RDMA adapter (check `/sys/class/infiniband/<dev>/device/numa_node`).

---

## 2. Hybrid CPU Architecture: The Intel P-Core / E-Core Trap

Modern client and edge processors (e.g. 12th/13th/14th Gen Intel Alder Lake like the **Core i3-1220P**) introduce **heterogeneous core architectures**:
* **Performance Cores (P-cores, Golden Cove)**: Wide out-of-order execution, deep instruction reorder buffers, high single-core frequency ($\sim 4.4\text{ GHz}$), Hyper-Threading (SMT, 2 threads/core).
* **Efficiency Cores (E-cores, Gracemont)**: Narrow out-of-order execution, lower peak frequency ($\sim 3.3\text{ GHz}$), single-threaded (no SMT).

> [!CAUTION]
> **The Baseline Contamination Trap**:
> The problem specification mandates running an $N=4$ node clean all-reduce baseline.
> If the OS scheduler arbitrarily places:
> - Rank 0 and Rank 1 on P-cores
> - Rank 2 and Rank 3 on E-cores
>
> Your benchmark possesses a **permanent, unmitigated $2\times$ hardware straggler** before you have even injected synthetic delays!
> 
> **The Fix**: All $N=4$ nodes in clean baselines must be pinned strictly to **homogeneous cores** (specifically, 4 E-cores).

---

## 3. The L2 Cache Cluster Trap on Gracemont E-Cores

Even when pinning exclusively to E-cores, a subtle hardware trap exists:

On Intel hybrid chips, E-cores are physically manufactured into **4-core clusters**, where each cluster of 4 cores shares a single, unified **2 MB Level-2 Cache**:

```
[ Core 4 ]   [ Core 5 ]   [ Core 6 ]   [ Core 7 ]
    │            │            │            │
    └────────────┴─────┬──────┴────────────┘
                       ▼
            [ Shared 2MB L2 Cache #0 ]

[ Core 8 ]   [ Core 9 ]   [ Core 10 ]  [ Core 11 ]
    │            │            │            │
    └────────────┴─────┬──────┴────────────┘
                       ▼
            [ Shared 2MB L2 Cache #1 ]
```

### The Pitfall:
* If an $N=4$ benchmark selects **Cores 2, 3, 4, 8**, data passing between cores crosses separate L2 cache boundaries, introducing cache-line invalidation traffic over the system ring bus.
* If all 4 processes are pinned to **Cores 4, 5, 6, 7**, chunk passing stays entirely within **L2 Cache Cluster #0**, eliminating ring bus latency.

### How Adder Automates L2 Cluster Pinning (`shared/affinity.h`):
`adder::shared::CpuTopology::discover()` traverses `/sys/devices/system/cpu/cpu*/cache/` and queries Level-2 shared CPU lists. When calling `get_homogeneous_cores(4)`, Adder verifies that all 4 selected cores reside inside the **same physical L2 cluster**.

---

## 4. Linux Operating System Tuning for Deterministic Latency

Dynamic power-saving features introduce microsecond-level latency jitter that ruins $P_{99}$ and $P_{99.9}$ percentiles.

### 4.1 Lock the CPU Scaling Governor to `performance`
By default, the Linux `powersave` or `ondemand` governor downclocks idle cores to save energy, introducing a $20\text{--}50\ \mu\text{s}$ wake-up penalty when polling starts:

```bash
# Lock all CPU cores to maximum clock frequency
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "performance" | sudo tee "$gov" > /dev/null
done
```

### 4.2 Disable CPU Turbo Boost
Intel Turbo Boost and AMD Core Performance Boost dynamically alter clock speeds based on thermal headroom and active core counts, creating fluctuating benchmark results:

```bash
# Intel Turbo Boost:
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    echo "1" | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo > /dev/null
fi

# AMD Core Performance Boost:
if [ -f /sys/devices/system/cpu/cpufreq/boost ]; then
    echo "0" | sudo tee /sys/devices/system/cpu/cpufreq/boost > /dev/null
fi
```

### 4.3 Raise Locked Memory Limit (`memlock`)
Mandatory for `ibv_reg_mr()`:
```bash
ulimit -l unlimited
```

### 4.4 Avoid SMT Hyperthread Sibling Contention
When a core executes two hardware threads (SMT), both threads compete for execution ports, branch predictors, and L1 cache lines.
* Adder's `CpuTopology` checks `/sys/devices/system/cpu/cpuX/topology/thread_siblings_list`.
* High-frequency polling threads are pinned strictly to primary core siblings, never to secondary SMT threads.
