# P1: RDMA Ping-Pong, Ring All-Reduce, Straggler Scheduling, and In-Network Aggregation over Soft-RoCE

## 1. Background

Project Icarus showed sub-microsecond latency on a single host. It used lock-free SPSC ring buffers, core pinning, and busy-poll loops. This project extends these methods across a network.

Modern AI systems such as NCCL, Mooncake, and GPUDirect use RDMA (Remote Direct Memory Access). RDMA bypasses the kernel network stack and gives microsecond latency at scale. This project teaches verbs programming, queue pair management, memory registration, and collective communication algorithms.

Soft-RoCE runs the RDMA protocol in kernel software on the CPU. The results show the behaviour of the CPU and the kernel, not of a network card. Every README must state this.

## 2. Objectives

### Primary Objective

Build an RDMA communication library from scratch with libibverbs. The library must do two things:

1. Measure ping-pong latency at the microsecond scale.
2. Run a ring all-reduce at near peak bandwidth.

### Secondary Objectives

- Create reusable patterns for RDMA registered memory pools. These apply to KV-cache transport and collective communication projects.
- Write documentation with the method, the benchmarks, and the analysis.
- Confirm that single-node techniques transfer to a network. These techniques are pinning, busy-poll, and P99 measurement.

### Tertiary Objectives (Extensions)

- **D3: Straggler-aware chunk scheduling.** Detect slow nodes from the variance of completion times. Move chunks away from the slow nodes. Measure the effect on tail latency (P99 and P99.9) when a delay is injected.
- **D4: In-network aggregation emulation (stretch goal).** Emulate switch-level gradient reduction with eBPF. Measure the bytes sent per node and the end-to-end time against a socket-based ring. The fallback is a userspace aggregator in C.

## 3. Deliverables

### D1: RDMA Ping-Pong Latency Tool

**File:** `pingpong/pingpong.cc` (single file, about 300 to 400 lines)

**Functions of the tool:**

1. Find RDMA devices with `ibv_get_device_list`.
2. Open the device and allocate a protection domain with `ibv_alloc_pd`.
3. Register a memory region with `ibv_reg_mr` and the chosen access flags.
4. Create a completion queue with `ibv_create_cq`.
5. Create a queue pair with `ibv_create_qp`. Support the RC and UD transports.
6. Move an RC QP through the states INIT, RTR, and RTS. Move a UD QP through the same states with different attributes. For UD, set the Q_Key in INIT. Give no destination in RTR. Give only the send PSN in RTS.
7. Exchange the QP information (QPN, LID or GID, PSN) over a TCP side channel.
8. Run the ping-pong loop. The local side posts a send and polls the CQ. The remote side polls the CQ and posts a send back.
9. Measure the round-trip latency and divide it by 2 for the one-way latency.

**UD rules:**

1. Put the destination QPN and Q_Key in the address handle (AH) of each send.
2. Leave 40 extra bytes at the start of each receive buffer. The hardware writes the global routing header (GRH) there.
3. Query the active MTU with `ibv_query_port` and read `active_mtu`. Convert the value to bytes.
4. Limit the UD message size to the active MTU.
5. Expect an active MTU of 1024 on a 1500-byte Ethernet interface. Set the interface MTU to 9000 to reach 4096. Verify the value.
6. Record the active MTU in the CSV header.

**Measurements to report:**

|Metric|Method|
|---|---|
|One-way latency (P50, P99, P99.9)|HDR histogram, at least 1M samples|
|Message size sweep|64B, 256B, 1KB, 4KB, 16KB, 64KB, 256KB, 1MB|
|One-sided against two-sided|`IBV_WR_RDMA_WRITE` against `IBV_WR_SEND`|
|Inline against registered|Compare with the `IBV_SEND_INLINE` flag|
|RC against UD|Same sweep up to the UD limit. UD messages cannot exceed the active MTU.|
|CPU overhead|`perf stat` cycles per `ibv_poll_cq` call|

**Output format:**

```
CSV: message_size, transport, direction, percentile, latency_ns, samples
Summary table in plain text
```

### D2: Ring All-Reduce

**Files:**

```
allreduce/
├── verbs_qp.h/.cc        (QP setup, state transitions)
├── ring.h/.cc            (chunk schedule, credit loop)
├── tree.h/.cc            (tree all-reduce for comparison)
├── main.cc               (benchmark driver)
└── README.md             (bandwidth against MPI, scaling plots)
```

**Algorithm:**

1. **Scatter-reduce:** Node _i_ sends chunk _(i+k) mod C_ to node _(i+1) mod N_. The receiving node adds the chunk to its own data. This takes _N−1_ steps.
2. **All-gather:** Node _i_ sends its fully reduced chunk around the ring. This takes _N−1_ steps.

Each node sends 2·(_N_−1)/_N_ · _S_ bytes in total. This is the theoretical minimum for a ring topology.

The tree variant uses a binary tree in two phases. First, reduce the data up the tree to the root. Second, broadcast the result down the tree. Each step sends the whole message _S_. The critical path takes 2·log₂(_N_) steps and moves 2·log₂(_N_) · _S_ bytes. The ring takes 2(_N_−1) steps and moves 2(_N_−1)/_N_ · _S_ bytes. Tree should win for small messages and ring for large messages. The tree exists to find the crossover size. Recursive halving and doubling is an optional third variant.

**Measurements to report:**

|Metric|Method|
|---|---|
|Collective bandwidth|_S_ divided by time, as a fraction of theoretical peak|
|Scaling efficiency|_N_ = 2 and 4 nodes as clean results. _N_ = 8 as a stress test.|
|Latency against message size|1KB to 16MB, log scale|
|Ring against tree|Same sizes and nodes. Report the crossover size.|
|Comparison with MPI|`MPI_Allreduce` on the same nodes, sizes, and data|

**MPI transport rule:** MPI on one host uses shared memory by default. Force MPI onto TCP or onto the Soft-RoCE device. State the transport in every result file.

**Core rule:** Run each node on its own pinned core. Report _N_ = 2 and 4 as the clean results. The _N_ = 8 test oversubscribes the CPU. Run it only as a stress test, label it oversubscribed, and still check correctness.

**Target:** Reach 70 to 80 percent of the MPI bandwidth at message sizes above 1MB.

### D3: Straggler-Aware Chunk Scheduling

**Files:**

```
straggler/
├── detector.h/.cc         (latency variance monitor per node)
├── scheduler.h/.cc        (adaptive chunk reassignment and schedule sync)
├── injection.h/.cc        (synthetic straggler: CPU throttle, network delay)
├── main.cc                (benchmark with and without mitigation)
└── README.md              (tail latency gain, detection accuracy)
```

**Functions of the component:**

1. Monitor the completion time of each chunk from the CQ events.
2. Compute a rolling variance for each node with an exponential moving average.
3. Mark a node as a straggler when its completion time exceeds 2σ for 3 consecutive rounds.
4. Give fewer chunks to the straggler and more chunks to the fast nodes.
5. Keep the total chunk count constant. This is the conservation invariant.
6. Agree on the new schedule. Rank 0 computes the schedule and sends it over the TCP side channel. The message carries an epoch number and a start round _R_. Set _R_ at least 2 rounds after the current round.
7. Each node completes its current round with the old schedule. Each node checks for a new epoch before it starts a round. All nodes switch at round _R_. A node must never switch during a round.

**Test rules:** Pin each node to its own core. Limit the straggler tests to _N_ = 4. This stops CPU contention from creating stragglers that hide the injected delay.

**Measurements to report:**

|Metric|Method|
|---|---|
|Tail latency gain|P99 and P99.9 with a straggler, with and without mitigation|
|Detection accuracy|Time to detect against the severity of the injected delay|
|Throughput cost|Bandwidth lost to monitoring and rebalancing|
|False positive rate|Runs with no injected straggler and no mitigation|

**Target:** P99 degrades by no more than 50 percent under a straggler. Without mitigation it degrades by 200 percent or more. Detect the straggler within 2 all-reduce rounds.

### D4: In-Network Aggregation Emulation (Stretch Goal)

**Files:**

```
innet_agg/
├── xdp_agg.bpf.c          (eBPF program)
├── agg_control.c          (libbpf userspace controller)
├── agg_userspace.c        (fallback: userspace aggregator)
├── sock_ring.cc           (socket-based ring baseline)
├── agg_test.cc            (benchmark driver)
└── README.md              (bytes per node, latency comparison)
```

**Design rules:**

1. The eBPF virtual machine has no floating-point support. Quantise the gradients to 32-bit fixed-point integers, as SwitchML does.
2. Use a fixed number of fractional bits. Set the scale so that _N_ times the maximum absolute value fits in 32 bits. Count each overflow. Report the overflow count and the quantisation error.
3. XDP cannot copy a packet. For the all-gather phase, use a TC hook with `bpf_clone_redirect`, or send the result to each node in turn.
4. Size each chunk to fit one MTU. Keep the packet loops within the verifier limits.
5. The protocol is a light custom protocol over UDP. It does not use verbs.
6. Compare the result with the socket-based ring in `sock_ring.cc`. Do not compare it with the RDMA ring.

**Functions of the component:**

1. Attach the program to a software bridge or a veth pair.
2. Parse the incoming chunk packets.
3. Add the values in a `BPF_MAP_TYPE_ARRAY` or a `BPF_MAP_TYPE_PERCPU_ARRAY`.
4. Detect chunk completion with sequence counters in the map.
5. Send the reduced result to the next node, or to all nodes in the all-gather phase.

**Fallback:** If the verifier or the hooks block progress, build `agg_userspace.c`. It runs the same aggregation in a C process on its own core. Report the same metrics.

**Measurements to report:**

|Metric|Method|
|---|---|
|Bytes sent per node|Counters at each node. Expect a fall from 2(_N_−1)/_N_ · _S_ to _S_.|
|End-to-end all-reduce time|Socket ring against the aggregated run|
|Packet-to-packet jitter|Timestamp analysis at the receiver, with and without aggregation|
|Aggregator cost|CPU cycles on the aggregator core, and `bpf_prog_run` time per packet|
|Quantisation error|Maximum and mean error against a floating-point sum|

**Target:** Measure the baseline first. Then set the latency overhead target from the measured XDP cost. Record the target in `innet_agg_analysis.md` before the final run.

## 4. Environment

### Hardware (Soft-RoCE, no special hardware needed)

|Component|Specification|
|---|---|
|CPU|12th Gen Intel Core i3-1220P (12 threads). Check the core layout with `lscpu`, as the chip mixes performance and efficiency cores. Pin all nodes to cores of the same type. Mixed core types create stragglers on their own.|
|RAM|At least 16GB|
|Network|Any Ethernet interface (Soft-RoCE over `rxe`), or network namespaces with veth pairs|
|OS|Native Linux, or a Linux VM. The default WSL2 kernel may lack `rdma_rxe` and XDP support. Run the check in the setup section and record the result in `docs/hardware_config.md`.|

### Software

|Component|Version|Purpose|
|---|---|---|
|Kernel|5.8 or later|`rxe` module for Soft-RoCE|
|`rdma-core`|Latest|libibverbs and perftest tools|
|`perftest`|Latest|Baseline latency and bandwidth numbers|
|MPI|OpenMPI or MPICH|Comparison baseline|
|Compiler|g++ 14 or later, or clang 18 or later|C++23 and C23 support|
|clang, libbpf, bpftool|Latest|Build and load the eBPF program (D4)|

### Setup verification

```bash
# Load Soft-RoCE kernel module
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev eth0

# Verify
ibv_devices          # Should list rxe0
ibv_devinfo          # Port state should be PORT_ACTIVE

# Baseline with perftest (run on two machines, or two namespaces)
ib_send_lat          # Node A: server
ib_send_lat <IP A>   # Node B: client

ib_write_lat         # Same for RDMA_WRITE
ib_send_bw           # Bandwidth test

# WSL2 only: check the kernel before you rely on it
uname -r
zcat /proc/config.gz | grep -E "RDMA_RXE|BPF_SYSCALL"   # Skip if the file is missing
sudo modprobe rdma_rxe                                 # Must succeed

# Check eBPF tools (D4)
bpftool version
clang --version
```

### Measurement rules

1. Set the CPU governor to `performance`.
2. Record the turbo state. Disable turbo for the latency runs if you can.
3. Pin each node to a core of the same type. Avoid the SMT sibling of a busy core.
4. Raise the memlock limit with `ulimit -l unlimited`. Memory registration needs it.
5. Discard a warmup period before you record samples.
6. Record the governor, turbo state, and pinned core IDs in every CSV header.

## 5. Reuse from Project Icarus

|Asset|Use in P1|
|---|---|
|`shared/ring_buffer.h`|Chunk credit loop between ring stages|
|`shared/affinity.h`|Pin CQ polling threads to isolated cores|
|`shared/busy_poll.h`|Compare spin and interrupt modes for CQ polling|
|`shared/latency_harness.h`|P50, P99, and P99.9 measurement, hardware metadata|
|`shared/free_list_pool.h`|Allocation of registered memory chunks|
|`shared/guarded_memory_pool.h`|Buffer overrun protection during development|

## 6. Success Criteria

|Criterion|Target|Verification|
|---|---|---|
|Ping-pong builds and runs|100%|`ibv_devinfo` shows an active port and ping-pong completes|
|Latency measurement|P50 within 20% of `ib_send_lat` on the same setup|Histogram output, at least 1M samples|
|RC against UD|Both transports measured up to the UD limit|CSV with a transport column|
|Bandwidth measurement|Within 20% of `ib_send_bw` on the same setup|Comparison with `ib_send_bw`|
|Ring all-reduce correctness|Bit-exact against MPI on integer data. Float data within a stated tolerance.|Same input gives the same output. The sum order changes float results.|
|Ring all-reduce performance|Within 30% of MPI at 1MB and above|Side-by-side benchmark with the MPI transport stated|
|Ring against tree|Crossover size reported|Benchmark sweep|
|Straggler mitigation|At most 50% P99 degradation under a 2x straggler|Injected delay benchmark at _N_ = 4|
|Straggler detection|At most 2 rounds to detect, at least 90% accuracy|Synthetic degradation sweep|
|In-network aggregation (stretch)|Bytes per node near _S_, latency target set after baseline|Counter comparison with the socket ring|
|Documentation complete|README and METHODOLOGY.md|Peer review|

## 7. Order of Work

This is a self-paced project. Finish each phase before the next one.

|Phase|Deliverable|Milestone|
|---|---|---|
|1|Environment setup|Soft-RoCE works and perftest baseline numbers exist|
|2|Ping-pong tool|D1 complete with RC and UD, and latency curves published|
|3|Ring all-reduce core|D2 algorithm is correct and the single-node test passes|
|4|Ring all-reduce on multiple nodes|Scaling test, tree comparison, and MPI comparison|
|5|Straggler detection|D3 detector, scheduler, and schedule sync built|
|6|Straggler benchmarks|D3 tail latency gain confirmed|
|7|Documentation|README, METHODOLOGY.md, and gap analysis|
|8|Cross-host runs (optional)|D1 and D2 on cloud VMs, published beside the single-host results|
|9|In-network aggregation (stretch)|D4 baseline, then the eBPF program or the fallback|

## 8. Risks and Mitigations

|Risk|Likelihood|Mitigation|
|---|---|---|
|Soft-RoCE performance is poor|High|Document it as emulation. Rent a cloud instance with InfiniBand or RoCE hardware for real numbers. EFA does not offer RC queue pairs, so the RC code would need changes.|
|QP state machine bugs|Medium|Check the `ibv_modify_qp` return codes. Compare with the perftest source.|
|MPI comparison is unfair|Medium|Use the same nodes, sizes, warmup, and time. Force the MPI transport and state it.|
|WSL2 lacks the kernel modules|High|Use native Linux or a Linux VM. Test with network namespaces first.|
|CPU oversubscription hides results|High|Pin one node per core. Label the _N_ = 8 runs as oversubscribed.|
|Straggler detection is unstable|Medium|Use cautious thresholds (2σ, 3 rounds). Smooth the data with an exponential moving average.|
|Nodes disagree on the schedule|Medium|Use an epoch number and a start round. Never switch during a round.|
|The eBPF verifier rejects the program|Medium|Start with a simple packet parser. Iterate with the `bpftool prog load` output.|
|Quantisation error is too large|Medium|Set the scale so that _N_ times the maximum value fits in 32 bits. Report the error and the overflow count.|
|Soft-RoCE and XDP interact badly|High|Test XDP on a veth pair first. D4 does not use verbs.|

## 9. Interview Preparation

|Question|Answer|
|---|---|
|Explain QP state transitions.|INIT, then RTR, then RTS. Describe each step from the implementation.|
|Why RC over UD?|RC gives reliability and ordering. D1 measured both up to the UD limit.|
|How does ring all-reduce compare with tree?|D2 measured both. Report the crossover size from the data.|
|What is your P99, and why?|Fill in the measured P99 and P99.9. Name the cause that a test confirmed.|
|Why not use NCCL?|NCCL is open source and a good reference. This project builds the algorithm on verbs to learn the layers below it.|
|How do you handle a slow node?|D3 detects it, then moves chunks away with an epoch-based schedule.|
|Why does Soft-RoCE show higher latency than hardware RDMA?|Soft-RoCE carries RoCEv2 packets in kernel UDP sockets. The CPU does the protocol work and copies the data in software. A hardware NIC offloads this work.|

## 10. Repository Structure

```
rdma-lab/
├── README.md                 (overview, quickstart, results summary)
├── METHODOLOGY.md            (measurement method, hardware, analysis)
├── CMakeLists.txt
├── shared/                   (moved from Icarus)
│   ├── ring_buffer.h
│   ├── affinity.h
│   ├── busy_poll.h
│   ├── latency_harness.h
│   ├── free_list_pool.h
│   └── guarded_memory_pool.h
├── pingpong/
│   ├── pingpong.cc           (D1: single-file implementation)
│   └── README.md             (latency curves, method)
├── allreduce/
│   ├── verbs_qp.h/.cc        (QP setup, state transitions)
│   ├── ring.h/.cc            (chunk schedule, credit loop)
│   ├── tree.h/.cc            (tree all-reduce)
│   ├── main.cc               (benchmark driver)
│   └── README.md             (bandwidth against MPI, scaling plots)
├── straggler/
│   ├── detector.h/.cc        (latency variance monitor)
│   ├── scheduler.h/.cc       (adaptive chunk reassignment, schedule sync)
│   ├── injection.h/.cc       (synthetic straggler)
│   ├── main.cc               (benchmark with and without mitigation)
│   └── README.md             (tail latency gain, detection accuracy)
├── innet_agg/                (stretch goal)
│   ├── xdp_agg.bpf.c         (eBPF program)
│   ├── agg_control.c         (libbpf controller)
│   ├── agg_userspace.c       (fallback aggregator)
│   ├── sock_ring.cc          (socket ring baseline)
│   ├── agg_test.cc           (benchmark driver)
│   └── README.md             (bytes per node, latency comparison)
├── results/
│   ├── pingpong_latency.csv
│   ├── pingpong_summary.txt
│   ├── allreduce_bandwidth.csv
│   ├── allreduce_scaling.csv
│   ├── ring_vs_tree.csv
│   ├── mpi_comparison.csv
│   ├── straggler_tail_latency.csv
│   ├── straggler_detection.csv
│   ├── innet_bytes_per_node.csv
│   └── innet_latency.csv
└── docs/
    ├── hardware_config.md    (CPU, kernel, compiler, isolation)
    ├── gap_analysis.md       (hypothesis, test, conclusion)
    ├── straggler_analysis.md
    └── innet_agg_analysis.md
```

### Results file rules

1. Files in `results/` keep the stable names shown above. The README links to them.
2. Move each superseded run to `results/archive/`. Name it `YYYYMMDD_<config>_<metric>.csv`.
3. Put the header fields in every CSV. The fields are date, git_commit, hardware_config_id, config_name, cpu_model, kernel_version, compiler_version, compiler_flags, cpu_governor, turbo_state, and pinned_cores.
4. Add the field mpi_transport to every MPI result file.

## 11. Definition of Done

- [ ] Soft-RoCE environment verified with `perftest`
- [ ] Ping-pong tool: latency curves for all message sizes, RC and UD, and all configurations
- [ ] Ring all-reduce: correct on 2, 4, and 8 nodes
- [ ] Ring against tree: crossover size reported
- [ ] MPI comparison: documented with the transport stated and within 30% at large sizes
- [ ] Straggler mitigation: detection validated and tail latency gain benchmarked
- [ ] Optional: cross-host runs published beside the single-host results
- [ ] README.md: build instructions, quickstart, and results summary
- [ ] METHODOLOGY.md: measurement method, hardware, and gap analysis
- [ ] All results committed with the header fields from the results file rules
- [ ] Git tag: `rdma-lab/v1.0` for D1 and D2
- [ ] Git tag: `rdma-lab/v2.0` for D1, D2, and D3
- [ ] Stretch: in-network aggregation measured, then the tag `rdma-lab/v3.0`

## 12. Implementation Languages

Each subsystem has one language. Do not mix languages inside a subsystem.

|Subsystem|Files|Language|Reason|
|---|---|---|---|
|Shared utilities|`shared/*.h`|C++23|These headers come from Icarus and use templates and atomics.|
|Ping-pong tool (D1)|`pingpong/pingpong.cc`|C++23|RAII classes own the verbs resources. The API is C and needs no wrapper.|
|QP setup and state transitions|`allreduce/verbs_qp.h/.cc`|C++23|RAII owns the protection domain, memory region, completion queue, and queue pair.|
|Ring and tree all-reduce|`allreduce/ring.h/.cc`, `allreduce/tree.h/.cc`|C++23|The code reuses the shared headers.|
|Benchmark drivers|`allreduce/main.cc`, `straggler/main.cc`, `innet_agg/agg_test.cc`|C++23|The drivers call the C++ libraries directly.|
|Straggler detector, scheduler, and injection|`straggler/*.h/.cc`|C++23|The code runs on the same hot path as the ring.|
|Socket ring baseline|`innet_agg/sock_ring.cc`|C++23|The baseline shares the benchmark harness.|
|eBPF program|`innet_agg/xdp_agg.bpf.c`|C (gnu11)|The kernel verifier and the clang BPF target accept only restricted C. Do not use C23 here.|
|eBPF controller|`innet_agg/agg_control.c`|C23|libbpf is a C library.|
|Fallback aggregator|`innet_agg/agg_userspace.c`|C23|The fallback must stay close to the eBPF logic and share its packet format.|
|Build system|`CMakeLists.txt`|CMake|One build file covers C and C++ targets.|
|Plots and CSV analysis|`results/` scripts|Python|Plotting is off the hot path. Use matplotlib and pandas.|

**Language standards:**

1. Compile the C++ code with `-std=c++23`.
2. Compile the userspace C code with `-std=c23`.
3. Compile the eBPF program with the BPF target and `-std=gnu11`.
4. In C++, use `std::expected` for error returns on the hot path. This replaces exceptions.
5. In C++, use `std::to_underlying`, `std::byteswap`, and `std::unreachable` where they fit.
6. In C, use `nullptr`, `constexpr`, and `stdckdint.h`. Use `ckd_add` to count quantisation overflows.
7. Do not use `std::print` or `std::flat_map` until the compiler support is confirmed.

**Hot path rules for all C++ code:**

1. Do not use exceptions in the polling loop or the send path.
2. Do not use virtual calls in the polling loop or the send path.
3. Do not allocate heap memory in the timed section.
4. Allocate and register all buffers before the timed section starts.

## 13. Working Recommendations

1. **Match perftest first.** Read the `ib_send_lat` source before you write `pingpong.cc`. A tool that disagrees with perftest has a bug.
2. **Write the hypothesis before each measurement.** Record it in `docs/gap_analysis.md`. Measure, then write the conclusion.
3. **Build the simplest path first.** Build RC SEND, then RDMA WRITE, then UD. Post the receives before the first send. Signal one send in every N sends to reduce CQ work.
4. **Test correctness with integer data.** Compare against a single-process reference sum before any benchmark.
5. **Commit early.** Commit the CSV headers and the build flags from the first run.
6. **Decide on D4 at the end of phase 7.** Run the cross-host phase before D4. Skip D4 if time is short.
7. **Draft the README results outline early.** Leave the plots empty. The gaps show which measurements the final story needs.