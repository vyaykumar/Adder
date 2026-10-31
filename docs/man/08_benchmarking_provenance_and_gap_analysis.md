# Man 08: Benchmarking Rigour, HDR Histograms, & Provenance

## 1. High-Resolution Timing & Clock Calibration

Measuring sub-microsecond RDMA latency requires nanosecond-scale timer precision and minimal clock read overhead:

```
Method                         Resolution  Overhead    Monotonic?  Invariant?
std::chrono::system_clock      ~10-100 ns  High (Sys)  NO (NTP)    NO
std::chrono::steady_clock      ~1-10 ns    ~15-25 ns   YES         YES
clock_gettime(MONOTONIC_RAW)   ~1 ns       ~15-20 ns   YES         YES (Unskewed by NTP)
x86 __rdtsc() / __rdtscp()     Cycle       ~5-8 ns     YES         Requires invariant TSC
```

Adder's `NanosecondClock` (`shared/latency_harness.h`) uses `clock_gettime(CLOCK_MONOTONIC_RAW)`:
* It reads the CPU hardware clock counter directly without taking kernel locks.
* It is immune to frequency slewing or time-jumping caused by NTP daemon adjustments.

### Warmup Iteration Discard Policy
During the first several hundred iterations:
* Instruction cache lines are cold, causing initial instruction fetch misses.
* CPU branch target buffers (BTB) are untrained.
* Soft-RoCE kernel worker threads are initializing internal connection contexts.

> [!IMPORTANT]
> **Measurement Standard**: Always execute and discard a minimum of **$5{,}000$ warmup iterations** for ping-pong (D1) and **$100$ warmup iterations** for all-reduce (D2/D3) before accumulating samples into histograms.

---

## 2. HDR Histograms & The Flaw of Averages

In distributed systems, **reporting the average latency is meaningless and deceptive**:
* If 999 requests complete in $1\ \mu\text{s}$, and 1 request stalls in an RNR retry timer for $1{,}000\ \mu\text{s}$:
  $$\text{Mean} = \frac{999 \times 1 + 1 \times 1000}{1000} \approx 2\ \mu\text{s}$$
* The average suggests fast performance, while completely hiding that the tail $P_{99.9}$ latency collapsed by **$1000\times$**!

### 2.1 Calculating High Dynamic Range (HDR) Percentiles
Adder's `LatencyHistogram` pre-allocates an internal vector for $1{,}000{,}000+$ samples, sorts the distribution post-run, and computes linearly interpolated percentiles:

Given sample size $n$ and desired percentile $p \in [0, 100]$:
$$\text{Rank} = \left(\frac{p}{100}\right) \times (n - 1)$$
$$i = \lfloor \text{Rank} \rfloor, \quad f = \text{Rank} - i$$
$$\text{Percentile}(p) = \text{Sample}[i] + f \times \left(\text{Sample}[i + 1] - \text{Sample}[i]\right)$$

* Reports $P_{50}$ (Median), $P_{90}$, $P_{95}$, $P_{99}$, $P_{99.9}$, and $P_{99.99}$.

---

## 3. Automated Scientific Provenance Standard

Systems benchmarks are invalid without exhaustive hardware metadata. A latency curve of $3.5\ \mu\text{s}$ means nothing unless the reader knows whether it ran on an Intel Core i3, an AMD EPYC server, or a virtualized cloud VM with CPU turbo enabled.

### 3.1 Standardized CSV Header Format
Every benchmark output in `results/` prepends an automated machine-readable comment header capturing the physical execution environment:

```csv
# provenance: date=2026-10-31T12:00:00Z, git_commit=a1b2c3d, hardware_config_id=AMD_A9-9425_Linux_6.1.0, config_name=pingpong_rc, cpu_model="AMD A9-9425 RADEON R5", kernel_version="Linux 6.1.0-48-amd64", compiler_version="GCC 14.1", compiler_flags="-O3 -march=native", cpu_governor=performance, turbo_state=disabled, pinned_cores=0,1, mpi_transport=tcp
message_size,transport,direction,percentile,latency_ns,samples,active_mtu
64,rc,tx,50.0,3110.0,1000000,1024
```

### 3.2 Automated Capture in `shared/latency_harness.h`
Adder automatically queries:
* **Git Hash**: `git rev-parse --short HEAD`
* **Kernel Version**: POSIX `uname()`
* **CPU Model**: `/proc/cpuinfo`
* **CPU Governor**: `/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor`
* **Turbo State**: `/sys/devices/system/cpu/intel_pstate/no_turbo` or `/sys/devices/system/cpu/cpufreq/boost`

---

## 4. The Hypothesis-Driven Engineering Workflow

Scientific rigor is maintained through the three-phase **Gap Analysis** protocol in `docs/gap_analysis.md`:

```
┌────────────────────────────────────────────────────────┐
│ Phase 1: Formulate Quantitative Hypothesis             │
│ (Written in docs/gap_analysis.md BEFORE running test)  │
│ - Expected P50 latency or bandwidth based on physics.  │
│ - Hardware limits (e.g. PCIe bandwidth, MTU size).     │
└───────────────────────────┬────────────────────────────┘
                            │ Run Benchmark
                            ▼
┌────────────────────────────────────────────────────────┐
│ Phase 2: Experimental Measurement                      │
│ - Collect 1M+ samples and export standardized CSV.     │
│ - Plot latency CDF or bandwidth scaling curves.        │
└───────────────────────────┬────────────────────────────┘
                            │ Compare
                            ▼
┌────────────────────────────────────────────────────────┐
│ Phase 3: Gap Analysis & Root Cause Determination       │
│ - Calculate delta: Discrepancy = |Observed - Expected| │
│ - Isolate architectural root cause (e.g. TLB misses,   │
│   RNR timer retry, cache line bouncing, SoftIRQs).     │
└────────────────────────────────────────────────────────┘
```

---

## 5. Comparative Validation Against Standard Tools

### 5.1 Validating Ping-Pong against `perftest`
Deliverable D1 targets matching `ib_send_lat` within $\le 20\%$:
```bash
# Node A (Server)
ib_send_lat -d rxe0 -i 1 -x 1 -F -a

# Node B (Client)
ib_send_lat -d rxe0 -i 1 -x 1 -F -a <SERVER_IP>
```

### 5.2 Validating All-Reduce against `MPI_Allreduce`
Deliverable D2 targets reaching $70\%\text{ to }80\%$ of `MPI_Allreduce` at $\ge 1\text{ MB}$:
```bash
# Force MPI onto TCP / Soft-RoCE device
mpirun -np 4 --mca btl tcp,self ./allreduce_bench --compare-mpi
```
