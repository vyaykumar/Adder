# Adder 🐍
> **A Low-Latency RDMA Collective Communication Engine & Latency Profiling Suite**  
> *Named after the Ouroboros (the serpent consuming its own tail, symbolizing the Ring All-Reduce) and the fundamental arithmetic addition operation.*

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![C Standard](https://img.shields.io/badge/C-23-green.svg)](https://en.cppreference.com/w/c/23)
[![eBPF Target](https://img.shields.io/badge/eBPF-XDP-purple.svg)](https://ebpf.io/)
[![Transport](https://img.shields.io/badge/Transport-Soft--RoCE%20%7C%20RoCEv2%20%7C%20InfiniBand-orange.svg)]()
[![Status](https://img.shields.io/badge/Status-In%20Active%20Development-yellow.svg)]()

---

> [!IMPORTANT]
> **Soft-RoCE Execution Note**: Soft-RoCE (`rdma_rxe`) executes the RDMA protocol in kernel software on the CPU. Latency and bandwidth measurements over Soft-RoCE reflect CPU, memory bus, and Linux kernel networking characteristics rather than dedicated ASIC offload. Hardware baseline numbers will be acquired through cloud/bare-metal RDMA deployments.

---

## 1. Project Motivation & Scope

Project **Adder** extends the lock-free single-host IPC techniques developed in Project Icarus (SPSC queues, core pinning, busy-polling) across distributed network nodes using `libibverbs`.

Designed with the primitives underpinning modern AI infrastructure—such as **NCCL**, **Mooncake** (disaggregated KV-cache streaming), and **GPUDirect RDMA**—Adder implements low-level Queue Pair management, memory registration pools, and distributed collective algorithms from first principles.

---

## 2. Deliverables & Performance Targets

Adder is organized into four sequential deliverables, each governed by quantitative engineering targets:

```
┌────────────────────────────────────────────────────────────────────────┐
│ D1: Ping-Pong Tool     │ Target: Match ib_send_lat within 20% (1M+ smp)│
│ D2: Ring All-Reduce    │ Target: 70–80% of MPI_Allreduce (>= 1MB)      │
│ D3: Straggler Schedule │ Target: <= 50% P99 degradation under 2x delay │
│ D4: In-Network Agg     │ Target: Reduce egress from 2(N-1)/N * S to S  │
└────────────────────────────────────────────────────────────────────────┘
```

### D1: RDMA Ping-Pong Latency Harness (`pingpong/`)
* **Objective**: Measure one-way microsecond-scale network latency across transports and memory modes.
* **Scope**:
  * Reliable Connection (RC) vs. Unreliable Datagram (UD).
  * Two-sided (`IBV_WR_SEND`/`RECV`) vs. One-sided (`IBV_WR_RDMA_WRITE`).
  * Registered Memory vs. Inline (`IBV_SEND_INLINE`).
  * Message size sweep from 64 B to 1 MB.
* **Target Milestones**:
  * **P50 Latency Target**: Match `ib_send_lat` on the same testbed within $\le 20\%$.
  * **Sampling Rigour**: Full HDR percentile distribution ($P_{50}, P_{90}, P_{99}, P_{99.9}, P_{99.99}$) over $\ge 1{,}000{,}000$ timed iterations.
  * **UD Compliance**: Enforce 40-byte Global Routing Header (GRH) buffer offsets and strict active MTU clamping.

### D2: Bandwidth-Optimal Ring All-Reduce (`allreduce/`)
* **Objective**: Implement collective communication moving the theoretical minimum $2\frac{N-1}{N}S$ bytes across $2(N-1)$ steps.
* **Scope**:
  * Two-phase pipeline: Scatter-Reduce followed by All-Gather.
  * Binary Tree All-Reduce implementation ($2\log_2 N \cdot S$ bytes) to identify message-size crossover boundaries.
  * Credit-loop flow control over lock-free SPSC rings to eliminate Receiver Not Ready (RNR) hardware retries.
* **Target Milestones**:
  * **Throughput Target**: Achieve $70\%\text{ to }80\%$ of `MPI_Allreduce` bandwidth at buffer sizes $\ge 1\text{ MB}$.
  * **Correctness Target**: Bit-exact sum verification on integer buffers; floating-point verification within defined rounding epsilon.
  * **Scaling Target**: Clean, non-oversubscribed verification across $N=2$ and $N=4$ nodes, with $N=8$ as a stress test.

### D3: Straggler-Aware Adaptive Chunk Scheduling (`straggler/`)
* **Objective**: Prevent tail-latency collapse ($P_{99} / P_{99.9}$) in distributed collectives caused by slow nodes.
* **Scope**:
  * Online monitoring of chunk and round completion times via Completion Queue timestamps.
  * Exponential Moving Average (EMA) mean and variance estimation flagging nodes exceeding $\mu + 2\sigma$ for 3 consecutive rounds.
  * Dynamic schedule rebalancing that redistributes chunk workloads while strictly maintaining the **Conservation Invariant** ($\sum \text{chunks}_i = \text{total\_chunks}$).
  * Out-of-band TCP epoch synchronization with start round $R \ge \text{current} + 2$ to guarantee zero intra-round schedule divergence.
* **Target Milestones**:
  * **Tail Latency Target**: Under an injected $2\times$ straggler delay, $P_{99}$ degradation must not exceed $50\%$ (compared to $\ge 200\%$ without mitigation).
  * **Detection Target**: Flag the straggler within $\le 2$ all-reduce rounds with $\ge 90\%$ accuracy.

### D4: In-Network Aggregation Emulation (Stretch Goal, `innet_agg/`)
* **Objective**: Emulate programmable network switch aggregation using eBPF/XDP with a C23 userspace fallback.
* **Scope**:
  * Ingress XDP packet parsing with SwitchML-style 32-bit fixed-point integer quantization.
  * BPF array map gradient accumulation with bounded unrolled loops.
  * Packet replication in all-gather via TC clone-redirect or unicast fanout.
* **Target Milestones**:
  * **Traffic Target**: Reduce node egress volume from $2\frac{N-1}{N}S$ down to $S$ bytes per node.
  * **Baseline Comparison**: Benchmark against an isolated socket-based UDP/TCP ring (`sock_ring.cc`).

---

## 3. Repository Structure

```text
Adder/
├── CMakeLists.txt              # Unified build configuration (C++23, C23, eBPF target)
├── README.md                   # Project roadmap, deliverables, and performance goals
├── METHODOLOGY.md              # Experimental protocol, core isolation, and provenance
│
├── shared/                     # Zero-overhead low-latency primitives
│   ├── adder.h                 # Master umbrella header
│   ├── affinity.h              # Dynamic CPU topology discovery & L2 cluster-aware pinning
│   ├── busy_poll.h             # Zero-allocation CQ spin-polling & event-channel wait
│   ├── latency_harness.h       # Nanosecond clock, HDR histograms, and CSV metadata headers
│   ├── ring_buffer.h           # SPSC circular credit buffer with 64-byte cacheline isolation
│   ├── free_list_pool.h        # Pre-registered RDMA memory pool (supports 2MB hugepages)
│   └── guarded_memory_pool.h   # Per-chunk PROT_NONE guard pages for buffer overrun traps
│
├── pingpong/                   # Deliverable D1: Ping-Pong tool
│   ├── pingpong.cc             # C++23 latency tool (RC & UD, 1-sided vs 2-sided)
│   └── README.md               # Usage instructions and perftest comparison guide
│
├── allreduce/                  # Deliverable D2: Collectives
│   ├── verbs_qp.h/.cc          # RAII wrappers for PD, MR, CQ, and QP state transitions
│   ├── ring.h/.cc              # Ring all-reduce data path and credit loop
│   ├── tree.h/.cc              # Binary tree all-reduce for crossover analysis
│   ├── main.cc                 # Benchmark driver and correctness validator
│   └── README.md               # Scaling documentation and MPI comparison
│
├── straggler/                  # Deliverable D3: Tail Latency Mitigation
│   ├── detector.h/.cc          # Online EMA latency variance monitor
│   ├── scheduler.h/.cc         # Adaptive chunk redistribution & epoch sync
│   ├── injection.h/.cc         # Synthetic delay injection (spin/sleep/throttle)
│   ├── main.cc                 # Comparative benchmark driver
│   └── README.md               # Tail latency targets and detection validation
│
├── innet_agg/                  # Deliverable D4: In-Network Aggregation
│   ├── xdp_agg.bpf.c           # eBPF/XDP kernel parser & fixed-point accumulator
│   ├── agg_control.c           # libbpf userspace lifecycle controller
│   ├── agg_userspace.c         # C23 userspace fallback aggregator
│   ├── sock_ring.cc            # Socket ring baseline for fair comparison
│   ├── agg_test.cc             # Test harness measuring bytes per node & jitter
│   └── README.md               # SwitchML-style in-network aggregation guide
│
├── scripts/                    # Offline visualization & data analysis
│   ├── plot_pingpong.py        # Latency curves across transports
│   ├── plot_allreduce.py       # Bandwidth scaling & crossover curves
│   └── plot_straggler.py       # Tail latency CDFs (with vs without mitigation)
│
├── results/                    # Standardized benchmark CSV data
│   └── archive/                # Historical versioned run archives
│
└── docs/                       # Engineering logs & specifications
    ├── manual/                 # In-depth RDMA, verbs, & latency theory handbooks
    ├── hardware_config.md      # Multi-machine testbed documentation
    ├── gap_analysis.md         # Hypothesis -> Test -> Conclusion logs
    ├── straggler_analysis.md   # Tail-latency evaluation records
    └── innet_agg_analysis.md   # Verifier logs and fixed-point precision notes
```

---

## 4. Hardware Environment & Deployment Targets

Adder is architected to evaluate performance across two distinct environments:

### Local Testbed (Heterogeneous Laptop / VM)
* **Processor**: 12th Gen Intel Core i3-1220P (2 P-cores / 4 threads, 8 E-cores / 8 threads).
* **Topology Rule**: `adder::shared::CpuTopology` automatically inspects `/sys/devices/system/cpu/` to cluster homogeneous cores. For $N=4$ benchmarks, it selects 4 E-cores sharing the **same physical L2 cache cluster** (e.g. Cores 4–7), preventing artificial cache-bouncing stragglers.
* **Driver**: Soft-RoCE (`rdma_rxe`) over kernel loopback or veth pairs.

### Cloud Target (Azure Linux / Bare-Metal Instance)
* **Target VMs**: Standard Azure Linux VMs (e.g. `Standard_D4s_v5` with 4 dedicated vCPUs, 16 GB RAM) or HPC instances with Mellanox ConnectX adapters.
* **Role**: Provides unthrottled upstream Linux kernel execution with native network interfaces, allowing cross-host multi-node testing without WSL hypervisor restrictions.

---

## 5. Hot-Path Constraints & Systems Discipline

To achieve deterministic microsecond-scale measurements, all timed paths strictly enforce:
1. **Zero Heap Allocation**: Memory buffers, queue pairs, and tracking structs are pre-allocated and registered before measurement loops begin.
2. **Zero Virtual Calls**: Execution loops avoid runtime polymorphism and `std::function` type erasure in favor of templates and inlined calls.
3. **Zero Exceptions on the Data Path**: Errors are propagated via `std::expected` and `std::optional`.
4. **Stream-Safe TCP Control**: Out-of-band QP exchanges use bounded `read_exact()`/`write_exact()` loops to eliminate TCP stream fragmentation bugs.
5. **Ordered Verbs RAII**: Protection Domains are only deallocated after all associated Memory Regions and Address Handles have been explicitly destroyed.

---

## 6. Build Instructions

### Prerequisites
```bash
# Ubuntu / Debian packages
sudo apt update && sudo apt install -y \
    build-essential cmake ninja-build \
    gcc-14 g++-14 clang llvm \
    libibverbs-dev librdmacm-dev libbpf-dev bpftool perftest \
    libopenmpi-dev openmpi-bin
```

### Build Steps
```bash
# Configure with Release optimizations (-O3 -march=native)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all deliverables
cmake --build build -j$(nproc)
```

Available executables upon compilation:
* `build/pingpong` — Deliverable D1 latency tool.
* `build/allreduce_bench` — Deliverable D2 collective benchmark.
* `build/straggler_bench` — Deliverable D3 straggler mitigation harness.
* `build/sock_ring_bench` — Deliverable D4 socket ring baseline.
* `build/agg_userspace` — Deliverable D4 C23 fallback aggregator.

---

## 7. Progress & Definition of Done

- [x] **Phase 0: Architecture & Scaffolding** — Complete project structure, unified CMake build system, and hardware-agnostic shared primitives.
- [x] **Phase 0.5: Systems & Protocol Hardening** — Fixed TCP stream fragmentation, UD GRH padding, verbs destruction sequencing, and L2 cache cluster discovery.
- [ ] **Phase 1: Environment Baseline** — Verify `rxe0` Port Active status; record `ib_send_lat` and `ib_send_bw` baseline numbers.
- [ ] **Phase 2: D1 Ping-Pong Completion** — Implement verbs state machine (INIT $\to$ RTR $\to$ RTS); generate latency curves across RC and UD.
- [ ] **Phase 3: D2 Ring All-Reduce Core** — Implement scatter-reduce and all-gather steps; verify integer bit-exact sum.
- [ ] **Phase 4: D2 Collective Scaling** — Benchmark $N=2$ and $N=4$ nodes; report Ring vs. Tree crossover size; compare against `MPI_Allreduce`.
- [ ] **Phase 5: D3 Straggler Mitigation** — Validate online EMA detection ($\le 2$ rounds) and verify P99 tail latency improvement under injected delay.
- [ ] **Phase 6: Methodology & Gap Analysis** — Complete hypothesis-versus-result entries in `docs/gap_analysis.md`.
- [ ] **Phase 7: Cloud Cross-Host Runs** — Execute cross-node benchmarks on Azure instances; publish multi-node results.
- [ ] **Phase 8: D4 In-Network Aggregation** (Stretch) — Evaluate eBPF/XDP gradient reduction against socket ring baseline.
