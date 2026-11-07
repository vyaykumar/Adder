# Project Adder — Systems Reference Manual (`man`)

Welcome to the **Adder Systems Reference Manual**. This manual serves as the definitive, offline technical guide for the entire project lifecycle—from low-level `libibverbs` programming and kernel-bypass networking to distributed collective algorithms, straggler scheduling, and cloud deployment.

---

## Manual Index & Reading Tracks

```
docs/man/
├── 00_index.md                                # This document (Master Index & Navigation)
├── 01_rdma_and_verbs_core.md                  # RDMA Architecture, Kernel-Bypass, Object Model & Verbs API
├── 02_queue_pairs_and_transports.md           # RC vs. UD, QP State Machine (RESET->RTS), One-Sided vs. Two-Sided
├── 03_memory_registration_and_pools.md        # Memory Pinning, Keys (lkey/rkey), Hugepages, FreeList & Guarded Pools
├── 04_collective_communication_algorithms.md  # Ring vs. Tree All-Reduce, Communication Volume Proofs, AVX SIMD Math
├── 05_straggler_mitigation_and_tail_latency.md# Tail Latency, Online EMA Variance Detection, Epoch Synchronization
├── 06_in_network_aggregation_ebpf_xdp.md      # SwitchML Paradigm, eBPF/XDP Ingress, Fixed-Point Quantization, C23
├── 07_hardware_topology_and_tuning.md         # NUMA, Intel Hybrid P/E-Cores, L2 Cache Clusters, CPU Governors & Turbo
├── 08_benchmarking_provenance_and_gap_analysis.md # Nanosecond Clocks, HDR Histograms, Provenance CSVs, Gap Analysis
├── 09_troubleshooting_and_error_codes.md      # Diagnostic Dictionary: IBV_WC_* Codes, RNR Spiral, Kernel Quirks
├── 10_cloud_and_environment_deployment.md    # Azure Linux Cloud VMs, Debian 12 Bare-Metal, NSG Port Rules, CI
└── 11_common_functions_pitfalls_and_best_practices.md # Operational Reference: libibverbs APIs, Traps & Best Practices
```

---

## Recommended Developer Tracks

### Track 1: First-Time RDMA Developer (Starting D1)
1. Read [01_rdma_and_verbs_core.md](file:///d:/Code/MultiArch/Adder/docs/man/01_rdma_and_verbs_core.md) to build the mental model of PCIe DMA, doorbells, and the verbs object hierarchy.
2. Read [02_queue_pairs_and_transports.md](file:///d:/Code/MultiArch/Adder/docs/man/02_queue_pairs_and_transports.md) for the exact step-by-step QP state machine transitions (`INIT` $\to$ `RTR` $\to$ `RTS`).
3. Read [03_memory_registration_and_pools.md](file:///d:/Code/MultiArch/Adder/docs/man/03_memory_registration_and_pools.md) for `ibv_reg_mr` and `ulimit -l` rules.
4. Keep [11_common_functions_pitfalls_and_best_practices.md](file:///d:/Code/MultiArch/Adder/docs/man/11_common_functions_pitfalls_and_best_practices.md) open as your function-by-function reference.
5. Implement `pingpong/pingpong.cc` following the step-by-step comments.
6. Refer to [09_troubleshooting_and_error_codes.md](file:///d:/Code/MultiArch/Adder/docs/man/09_troubleshooting_and_error_codes.md) if work completions fail.

### Track 2: Collective Communication & Bandwidth Scaling (Starting D2)
1. Read [04_collective_communication_algorithms.md](file:///d:/Code/MultiArch/Adder/docs/man/04_collective_communication_algorithms.md) for the mathematical proof of Ring All-Reduce ($2 \frac{N-1}{N} S$) and the Binary Tree crossover equation.
2. Read [07_hardware_topology_and_tuning.md](file:///d:/Code/MultiArch/Adder/docs/man/07_hardware_topology_and_tuning.md) to ensure your $N=4$ nodes are pinned to homogeneous cores sharing a single L2 cache cluster.
3. Review [03_memory_registration_and_pools.md](file:///d:/Code/MultiArch/Adder/docs/man/03_memory_registration_and_pools.md) for `FreeListMemoryPool` chunk slicing.
4. Benchmark `allreduce/main.cc` and record hypotheses in `docs/gap_analysis.md`.

### Track 3: Tail Latency & Adaptive Scheduling (Starting D3)
1. Read [05_straggler_mitigation_and_tail_latency.md](file:///d:/Code/MultiArch/Adder/docs/man/05_straggler_mitigation_and_tail_latency.md) for online Exponential Moving Average (EMA) variance mathematics.
2. Review the out-of-band TCP epoch synchronization mechanism to ensure nodes switch schedules synchronously at round boundary $R$.
3. Run benchmarks with synthetic delay injection and plot tail latency percentiles ($P_{99}, P_{99.9}$).

### Track 4: In-Network Aggregation & eBPF (Starting D4)
1. Read [06_in_network_aggregation_ebpf_xdp.md](file:///d:/Code/MultiArch/Adder/docs/man/06_in_network_aggregation_ebpf_xdp.md) for the SwitchML architecture, verifier bounds, and C23 overflow checking (`ckd_add`).
2. Build and attach `innet_agg/xdp_agg.bpf.c` with libbpf or run the userspace fallback `innet_agg/agg_userspace.c`.

### Track 5: Cloud Deployment & Final Results (Azure)
1. Read [10_cloud_and_environment_deployment.md](file:///d:/Code/MultiArch/Adder/docs/man/10_cloud_and_environment_deployment.md) for provisioning Azure Linux VMs, NSG firewall rules, and multi-node execution.
2. Follow [08_benchmarking_provenance_and_gap_analysis.md](file:///d:/Code/MultiArch/Adder/docs/man/08_benchmarking_provenance_and_gap_analysis.md) to output standardized CSVs and publish GitHub release assets.
