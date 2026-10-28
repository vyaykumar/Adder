# Adder 🐍
> **A High-Performance Low-Latency RDMA Communication Engine & Collective Framework**
> *Named after the Ouroboros (the serpent devouring its own tail, embodying the Ring All-Reduce) and the fundamental arithmetic compute primitive.*

[![C++ Standard](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![C Standard](https://img.shields.io/badge/C-23-green.svg)](https://en.cppreference.com/w/c/23)
[![eBPF Target](https://img.shields.io/badge/eBPF-XDP-purple.svg)](https://ebpf.io/)
[![Transport](https://img.shields.io/badge/Transport-Soft--RoCE%20%7C%20RoCEv2%20%7C%20InfiniBand-orange.svg)]()

---

## 1. Overview

**Adder** is an RDMA collective communication library and latency profiling harness engineered from scratch using `libibverbs`. Designed for disaggregated AI serving architectures (such as distributed KV-cache streaming and gradient synchronization), Adder bridges single-node lock-free IPC with high-throughput remote direct memory access.

### Core Deliverables
* **D1: Microsecond-Scale Ping-Pong Latency Harness (`pingpong/`)**:
  * Evaluates Reliable Connection (RC) vs. Unreliable Datagram (UD) transports.
  * Measures one-sided (`RDMA_WRITE`) vs. two-sided (`SEND/RECV`) transactions, inline optimizations (`IBV_SEND_INLINE`), and active MTU limits over 1M+ samples using high-resolution HDR histograms.
* **D2: Bandwidth-Optimal Ring All-Reduce (`allreduce/`)**:
  * Implements Scatter-Reduce and All-Gather phases transferring the theoretical minimum $2\frac{N-1}{N}S$ bytes.
  * Features a Binary Tree all-reduce ($2\log_2 N \cdot S$) to benchmark and identify message-size crossover points against `MPI_Allreduce`.
  * Incorporates lock-free SPSC credit loops to prevent hardware Receiver Not Ready (RNR) retry deadlocks.
* **D3: Straggler-Aware Adaptive Chunk Scheduling (`straggler/`)**:
  * Online EMA latency and variance tracker flagging stragglers exceeding $\mu + 2\sigma$ over 3 consecutive rounds.
  * Rebalances chunk assignments dynamically to fast nodes while strictly preserving the **Conservation Invariant** ($\sum \text{chunks}_i = \text{total}$).
  * Out-of-band TCP epoch synchronization preventing intra-round state divergence.
* **D4: In-Network Aggregation Emulation via eBPF/XDP (`innet_agg/`)**:
  * Emulates programmable network switch aggregation using XDP with 32-bit fixed-point gradient quantization (SwitchML style) and C23 overflow checking (`ckd_add`).
  * Cuts node egress volume by up to $50\%$ compared to standard rings. Includes a portable C23 userspace fallback.

---

## 2. Repository Layout

```text
Adder/
├── CMakeLists.txt              # Unified build system (C++23, C23, eBPF clang target)
├── README.md                   # Project overview, quickstart, and benchmark summaries
├── METHODOLOGY.md              # Experimental rigour, core topology discovery, and provenance rules
│
├── shared/                     # Zero-overhead low-latency primitives
│   ├── affinity.h              # Dynamic CPU topology discovery & core pinning
│   ├── busy_poll.h             # Zero-allocation CQ spin-polling & event channel wait
│   ├── latency_harness.h       # Nanosecond clock, HDR histograms, and CSV metadata headers
│   ├── ring_buffer.h           # SPSC circular credit buffer with 64-byte cacheline isolation
│   ├── free_list_pool.h        # Pre-registered RDMA memory pool (supports 2MB hugepages)
│   └── guarded_memory_pool.h   # Memory pool with PROT_NONE guard pages for overrun detection
│
├── pingpong/                   # Deliverable D1: Ping-Pong tool
│   ├── pingpong.cc             # Standalone C++23 latency tool
│   └── README.md               # Usage, command flags, and perftest comparison
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
│   └── README.md               # Tail latency gains and detection benchmarks
│
├── innet_agg/                  # Deliverable D4: In-Network Aggregation
│   ├── xdp_agg.bpf.c           # eBPF/XDP kernel parser & fixed-point accumulator
│   ├── agg_control.c           # libbpf userspace lifecycle controller
│   ├── agg_userspace.c         # C23 userspace fallback aggregator
│   ├── sock_ring.cc            # Socket ring baseline for fair comparison
│   ├── agg_test.cc             # Test harness measuring bytes per node & jitter
│   └── README.md               # SwitchML-style in-network aggregation guide
│
├── scripts/                    # Visualization & plotting
│   ├── plot_pingpong.py        # Latency curves across transports
│   ├── plot_allreduce.py       # Bandwidth scaling & crossover curves
│   └── plot_straggler.py       # Tail latency CDFs (with vs without mitigation)
│
├── results/                    # Standardized benchmark CSV data
│   ├── archive/                # Versioned historical runs
│   └── .gitkeep
│
└── docs/                       # Engineering logs & specifications
    ├── hardware_config.md      # Multi-machine testbed documentation
    ├── gap_analysis.md         # Hypothesis -> Test -> Conclusion logs
    ├── straggler_analysis.md   # Tail-latency evaluation records
    └── innet_agg_analysis.md   # Verifier logs and fixed-point precision notes
```

---

## 3. Quickstart & Build Instructions

### Prerequisites
* **Compiler**: GCC 14+ or Clang 18+ (C++23 & C23 support)
* **Libraries**: `libibverbs-dev`, `librdmacm-dev`, `libbpf-dev`
* **Tools**: `bpftool`, `clang`, `perftest`, `iproute2`
* **Optional**: OpenMPI / MPICH (for MPI comparison)

### 1. Configure Soft-RoCE (if testing without dedicated RDMA NICs)
```bash
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev eth0

# Verify port is ACTIVE
ibv_devinfo -d rxe0
```

### 2. Build Adder
```bash
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)
```

---

## 4. Hot-Path Constraints & Coding Standards

Adder enforces strict low-latency systems discipline across all hot paths:
1. **Zero Heap Allocation**: All memory buffers, queue pairs, and tracking structures are pre-allocated and registered before timed loops commence.
2. **Zero Virtual Dispatch**: High-frequency execution paths use templates, inlined calls, and flat stack-allocated state machines.
3. **Zero Exceptions on the Hot Path**: Errors are propagated via `std::expected` and `std::optional`.
4. **Homogeneous Core Pinning**: `adder::shared::CpuTopology` automatically discovers CPU core types (e.g. separating P-cores from E-cores on Intel hybrid chips) to prevent asymmetric hardware noise from corrupting benchmark data.

---

## 5. Engineering Discipline & Gap Analysis

Every benchmark sweep adheres to the **Hypothesis-Driven Workflow**:
1. Formulate and record a hypothesis in [docs/gap_analysis.md](file:///d:/Code/MultiArch/Adder/docs/gap_analysis.md) before execution.
2. Execute the test and export CSV results with full provenance headers (git commit, CPU governor, turbo state, pinned cores).
3. Record observations, identify the root cause of discrepancies, and document findings.
