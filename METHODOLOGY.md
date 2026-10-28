# Measurement Methodology & Benchmarking Rigour

This document outlines the experimental standards and execution procedures enforced across the **Adder** project to guarantee benchmark reproducibility across varying hardware platforms.

---

## 1. System Preparation & CPU Isolation

### 1.1 CPU Frequency Scaling & Turbo State
Dynamic frequency scaling and Turbo Boost introduce variance that degrades P99/P99.9 latency consistency.
```bash
# Set CPU scaling governor to performance
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "performance" | sudo tee $cpu > /dev/null
done

# Disable Intel Turbo Boost (if supported)
if [ -f /sys/devices/system/cpu/intel_pstate/no_turbo ]; then
    echo "1" | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo > /dev/null
fi
```

### 1.2 Homogeneous Core Pinning
Hybrid CPUs (such as Intel 12th/13th/14th Gen) combine high-IPC Performance cores (P-cores) and energy-efficient Efficiency cores (E-cores). 
- When running an $N=4$ all-reduce benchmark, pinning 2 nodes to P-cores and 2 nodes to E-cores introduces an artificial $2\times$ hardware straggler into the baseline.
- **Rule**: All benchmark processes within a single run must be pinned to cores of identical architecture and frequency. `adder::shared::CpuTopology::get_homogeneous_cores()` automatically enforces this.

### 1.3 Memory Locking (`memlock`)
RDMA memory registration (`ibv_reg_mr`) pins physical pages in RAM. Insufficient `memlock` limits will cause registration failures:
```bash
ulimit -l unlimited
```

---

## 2. Timing Harness & HDR Histograms

1. **Clock Source**: Monotonic raw clock (`CLOCK_MONOTONIC_RAW`) or CPU Time Stamp Counter (`rdtsc`) is used for microsecond/nanosecond interval measurement.
2. **Warmup Discard**: A minimum of $5{,}000$ warmup iterations for D1 and $100$ iterations for D2/D3 are executed and discarded prior to recording samples.
3. **HDR Percentile Calculation**: Rather than reporting simple averages, all latency runs record complete sample distributions, evaluating P50, P90, P99, P99.9, and P99.99 tail percentiles over at least $1{,}000{,}000$ samples.

---

## 3. Provenance Tracking & Data Archival

To guarantee that measurements made on a laptop are never conflated with cloud-based runs, every CSV generated in `results/` must prepend a standardized provenance comment:

```csv
# provenance: date=2026-10-27T18:30:00Z, git_commit=a1b2c3d, hardware_config_id=Intel_Core_i3-1220P_Linux_6.8.0, config_name=pingpong_rc, cpu_model="12th Gen Intel Core i3-1220P", kernel_version="Linux 6.8.0-generic", compiler_version="GCC 14.1", compiler_flags="-O3 -march=native", cpu_governor=performance, turbo_state=disabled, pinned_cores=4,5,6,7
```

### Archival Procedure
- Stable benchmark outputs are maintained in `results/<benchmark_name>.csv`.
- Superseded runs are archived to `results/archive/` using the format:
  `YYYYMMDD_<config>_<metric>.csv`.

---

## 4. Correctness Verification

### 4.1 Bit-Exact Integer Collectives
For Ring and Tree all-reduce on integer data, results must be bit-exact against a single-process reference reduction:
$$\sum_{r=1}^N r = \frac{N(N+1)}{2}$$

### 4.2 Floating-Point Sum Tolerance
Floating-point addition is non-associative. Ring All-Reduce accumulates sequentially $((a + b) + c) + d$, whereas Binary Tree accumulates pairwise $(a + b) + (c + d)$. Results are verified within an allowable epsilon tolerance:
$$\frac{|x_{\text{observed}} - x_{\text{expected}}|}{x_{\text{expected}}} < 10^{-6}$$
Differences in IEEE-754 rounding behavior across topologies are documented as part of the systems evaluation.
