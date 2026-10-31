# Part 3: Measuring Latency Correctly

---

### 3.1 High-Resolution Clock Sources: `CLOCK_MONOTONIC_RAW` vs. `rdtsc`

#### Why it matters here
In Deliverable D1, one round-trip ping-pong over Soft-RoCE takes $\approx 10\ \mu\text{s}$, while on hardware RDMA it takes $\approx 1.5\ \mu\text{s}$. If your timing mechanism incurs a $50\ \text{ns}$ measurement overhead or suffers from CPU frequency drift, your P50 measurement will be distorted, and your tail latency will report phantom spikes.

#### Mental Model
Measuring microsecond network events with a wall clock (`gettimeofday`) is like trying to time an Olympic 100-meter dash using the sun's position in the sky. If Network Time Protocol (NTP) slews or steps the clock mid-run, time can move backward. Even `CLOCK_MONOTONIC` undergoes NTP frequency adjustments (adjtime). You need a clock tied directly to hardware oscillator ticks.

```
Hardware Crystal Oscillator (e.g. 24 MHz Base Clock)
         │
         ▼
[ CPU Invariant TSC Counter ] (Ticking at constant nominal frequency, e.g. 2.4 GHz)
         │
         ├─── Direct Instruction: rdtsc / rdtscp (Cycles: ~15-25 cycles overhead)
         │
         ▼
[ Linux Kernel vDSO ] ────► clock_gettime(CLOCK_MONOTONIC_RAW) (~20-40 ns overhead)
```

#### Core Details: The Three Clock Options

| Clock Source | Call Mechanism | Resolution | Overhead (ns) | Subject to NTP Slewing? | Invariant Across Frequency Changes? |
|---|---|---|---|---|---|
| `std::chrono::system_clock` / `gettimeofday` | Syscall / vDSO | $\approx 1\ \mu\text{s}$ | 30–50 ns | **Yes (Can jump or run backwards)** | N/A |
| `clock_gettime(CLOCK_MONOTONIC_RAW)` | vDSO | $\approx 1\ \text{ns}$ | 18–35 ns | **No (Monotonic ticks only)** | Yes (Kernel converts via TSC math) |
| `__rdtsc()` / `__rdtscp()` | CPU ASM Instruction | Sub-nanosecond (Cycles) | **~7–12 ns** | **No** | **Yes (on modern CPUs with `constant_tsc`)** |

#### Pitfalls of `rdtsc` on Modern Hybrid CPUs
1. **Out-of-Order Instruction Execution**: Modern superscalar CPUs execute instructions speculatively. If you write:
   ```cpp
   uint64_t t0 = __rdtsc();
   ibv_post_send(qp, &wr, &bad);
   uint64_t t1 = __rdtsc();
   ```
   The CPU pipeline can reorder the execution such that the second `__rdtsc()` instruction executes *before* `ibv_post_send()` finishes its memory stores!
   * **Fix**: You must serialize the pipeline using `_mm_lfence()` before and after `__rdtsc()`, or use `__rdtscp()` which forces in-order retirement of earlier instructions.
2. **Core Hopping**: If your thread migrates from Core 0 to Core 5 between `t0` and `t1`, the two cores might have slightly desynchronized TSC registers.
   * **Fix**: Always enforce strict thread affinity (`pthread_setaffinity_np`) before reading TSC.
3. **Cycle-to-Nanosecond Frequency Conversion**: The TSC counts nominal cycles, not nanoseconds. To convert cycles to nanoseconds, you must calibrate the TSC frequency against `clock_gettime(CLOCK_MONOTONIC_RAW)` over a known warmup interval (e.g., 50 ms) at startup.

#### Minimal Illustration: Serialized Invariant TSC Timing Harness (<25 lines)

```cpp
#include <x86intrin.h>
#include <cstdint>

class TscClock {
public:
    [[nodiscard]] static inline uint64_t start() noexcept {
        _mm_lfence();
        return __rdtsc();
    }

    [[nodiscard]] static inline uint64_t stop() noexcept {
        unsigned int aux;
        uint64_t tsc = __rdtscp(&aux); // Serializing read: drains pipeline
        _mm_lfence();
        return tsc;
    }

    // Convert raw cycles to nanoseconds using pre-calibrated nominal frequency (GHz)
    [[nodiscard]] static inline double to_ns(uint64_t cycles, double ghz_freq) noexcept {
        return static_cast<double>(cycles) / ghz_freq;
    }
};
```

---

### 3.2 Tail Latency, Sample Counts, and Coordinated Omission

#### Why it matters here
In distributed AI training (e.g., Ring All-Reduce across 1,024 GPUs), the step time is bounded by the **slowest rank** in the cluster. If your P99.9 latency spikes by $10\times$, your entire \$50M GPU cluster stalls waiting for that straggler. Reporting averages in systems engineering hides this reality.

#### Mental Model: The Flaw of Averages
Imagine walking across a river with an "average" depth of 3 feet, but the middle 50 feet drops into a 30-foot chasm. You will drown.
If an all-reduce library has an average latency of $15\ \mu\text{s}$, but its P99.9 is $12\ \text{ms}$, one out of every 1,000 steps will pause training for 12 milliseconds. At scale, where thousands of chunk transfers occur per second, you live permanently in the tail.

```
Frequency
  ▲
  │   P50 (Median)
  │    │
  │  ┌─┴─┐
  │  │   │        P99
  │  │   │         │            P99.9 (Tail Latency - Where Distributed Systems Fail)
  │  │   │         │               │
  └──┴───┴─────────┴───────────────┴──────────────► Latency (µs)
     Typical     Outlier       Systemic Jitter
```

#### Core Details: Sample Size Statistical Rigour
How many samples do you need to report P99.9 with statistical confidence?
* To observe a single P99.9 event (the 99.9th percentile), you need at least 1,000 samples ($1 / (1 - 0.999) = 1,000$).
* In statistics, a single sample has an error margin of nearly 100%. To construct an empirically robust distribution with a tight confidence interval around P99.9, you need at least **1,000 occurrences** of that tail event.
* **The Rule**: **$1,000 \times 1,000 = 1,000,000$ samples minimum.** That is why the Project Adder specification strictly mandates $1\text{M}+$ samples for all latency evaluations.

#### The Warmup Phase
The first 5,000 iterations of any benchmark will suffer from:
1. Cold CPU instruction caches (I-cache) and data caches (D-cache).
2. Cold Translation Lookaside Buffers (TLB).
3. Linux kernel page faulting for lazy memory allocation.
4. Soft-RoCE driver workqueue initialization.
* **The Rule**: Always run and discard **at least 5,000 warmup iterations** before recording a single sample into your histogram.

#### The Coordinated Omission Fallacy (Gil Tene's Principle)
Coordinated Omission occurs when your benchmark harness coordinates its request generation with the response of the service being tested.

* **How it happens in Ping-Pong**:
  ```cpp
  // The Classic Coordinated Omission Bug:
  for (int i = 0; i < 1'000'000; ++i) {
      t0 = now();
      send();
      wait_for_recv(); // If this stalls for 100ms...
      t1 = now();
      record(t1 - t0); // ...you record ONE 100ms sample!
  }
  ```
  If the network stalls for $100\ \text{ms}$, a request that should have completed in $10\ \mu\text{s}$ blocks the entire loop. You record **one** bad sample. But in a real system running at a rate of 100,000 requests/sec, **10,000 requests** were actually delayed behind that stall!
* **The Impact**: Traditional ping-pong harnesses artificially under-represent tail latency by orders of magnitude.
* **How to account for it**: In Deliverable D3, evaluate whether delays are bursty or persistent, and note in `docs/gap_analysis.md` whether closed-loop (ping-pong) vs. open-loop (arrival-rate driven) testing was used.

---

### 3.3 CPU Hygiene on the Intel Core i3-1220P Hybrid Architecture

#### Why it matters here
Your Intel Core i3-1220P CPU is an asymmetric hybrid processor:
* **2 Performance Cores (Golden Cove)**: High IPC, ~4.4 GHz Max Turbo, SMT enabled (4 logical threads: Cores 0, 1, 2, 3).
* **8 Efficiency Cores (Gracemont)**: Lower IPC, ~3.3 GHz Max Turbo, no SMT (8 logical threads: Cores 4 through 11).
* **Cache Partitioning**: The 8 E-cores are split into **two distinct 4-core clusters**. Cores 4–7 share one 2 MB L2 cache; Cores 8–11 share a separate 2 MB L2 cache.

If your benchmark thread hops between a P-core and an E-core, your latency histogram will show a bimodal distribution with two distinct peaks (one at $6\ \mu\text{s}$ and one at $14\ \mu\text{s}$), destroying your P99.

```
Intel i3-1220P Hardware Topology:
┌───────────────────────────────┐ ┌───────────────────────────────┐
│     P-Core Cluster (SMT)      │ │   E-Core Cluster 0 (No SMT)   │
│  Core 0 (CPU 0, 1) - 4.4 GHz  │ │  Core 2 (CPU 4)   Core 3 (CPU 5)│
│  Core 1 (CPU 2, 3) - 4.4 GHz  │ │  Core 4 (CPU 6)   Core 5 (CPU 7)│
│       Shared 2.5 MB L2        │ │       Shared 2.0 MB L2        │
└───────────────────────────────┘ └───────────────────────────────┘
                                  ┌───────────────────────────────┐
                                  │   E-Core Cluster 1 (No SMT)   │
                                  │  Core 6 (CPU 8)   Core 7 (CPU 9)│
                                  │  Core 8 (CPU 10)  Core 9 (CPU 11│
                                  │       Shared 2.0 MB L2        │
                                  └───────────────────────────────┘
```

#### The Six Rules of Hardware CPU Hygiene

#### 1. Set the Frequency Governor to `performance`
Dynamic frequency scaling (p-states) adjusts CPU clock frequency based on load. When an RDMA ping-pong test pauses between iterations, the CPU drops to 800 MHz; when the packet arrives, the CPU takes 20 microseconds to ramp back to 3 GHz, registering as a massive latency spike.
```bash
# Force all cores to maximum clock
for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo "performance" | sudo tee $gov > /dev/null
done
```

#### 2. The Turbo Dilemma: Pin Frequencies Instead of `no_turbo=1`
* *The Spec Trap*: The project spec recommends disabling turbo via `/sys/devices/system/cpu/intel_pstate/no_turbo`.
* *The Reality*: On a mobile laptop CPU like the i3-1220P, disabling turbo locks the CPU at its base frequency: **1.5 GHz on P-cores and 1.1 GHz on E-cores**. This makes memory copies and kernel socket processing crawl.
* *The Professional Fix*: Keep turbo enabled, but lock the minimum scaling frequency equal to a thermally sustainable maximum (e.g., 2.2 GHz):
```bash
sudo cpupower frequency-set -u 2400MHz -d 2400MHz
```

#### 3. Strict Homogeneous Core Pinning
* For $N=2$ benchmarks: Pin to Cores 0 and 2 (the primary threads of the two P-cores). Avoid Cores 1 and 3 (their SMT siblings).
* For $N=4$ benchmarks: Pin exclusively to **Cores 4, 5, 6, and 7** (E-Core Cluster 0). Do not use Cores 8–11; spanning across E-core clusters introduces cross-L2 cache coherency penalties.

#### 4. Avoid SMT Siblings (Hyper-Threading Contention)
An SMT thread shares execution ports, L1 data caches, and TLBs with its sibling. If a background process runs on CPU 1 while your benchmark spins on CPU 0, your polling loop latency will double.

#### 5. Isolate Cores via Kernel Boot Parameters (`isolcpus` & `nohz_full`)
On native Linux, edit `/etc/default/grub` and add:
```text
GRUB_CMDLINE_LINUX_DEFAULT="isolcpus=4,5,6,7 nohz_full=4,5,6,7 rcu_nocbs=4,5,6,7"
```
Run `sudo update-grub` and reboot. The Linux OS scheduler will **never** schedule background tasks, worker threads, or timer ticks onto Cores 4–7.

#### 6. IRQ Affinity Steering
Network interrupts will ruin latency if delivered to your benchmark core. Check where interrupts land:
```bash
# Verify which cores handle network interrupts
cat /proc/interrupts | grep -E "eth0|rxe"
# Stop irqbalance from moving interrupts onto your isolated cores
sudo systemctl stop irqbalance
```

---

### 3.4 Matching `perftest`: How `ib_send_lat` Actually Measures Time

#### Why it matters here
The Definition of Done mandates: *"P50 latency within 20% of `ib_send_lat` on the same setup."* If your measurement methodology differs from `ib_send_lat`, your comparison will be scientifically invalid.

#### How `ib_send_lat` Measures Latency
If you inspect the source code of `perftest/src/perftest_resources.c` and `ib_send_lat.c`:
1. **Round-Trip Division**: `ib_send_lat` measures the **Round-Trip Time (RTT)** and divides by 2:
   $$\text{One-Way Latency} = \frac{T_{\text{end}} - T_{\text{start}}}{2}$$
   It does *not* attempt to synchronize clocks between two different machines (which introduces nanosecond clock skew errors).
2. **Ping-Pong Alternation**:
   * Client: Posts Send $\to$ Spins on Send CQE $\to$ Spins on Recv CQE $\to$ Takes timestamp.
   * Server: Spins on Recv CQE $\to$ Posts Send back $\to$ Spins on Send CQE.
3. **Timer Placement**:
   `ib_send_lat` starts the timer **immediately before** `ibv_post_send()` and stops the timer **immediately after** the final CQE is extracted from `ibv_poll_cq()`. It measures the software overhead of posting + local PCIe DMA + network transit + remote processing + return transit + local CQ polling.

```
Client Timeline (ib_send_lat):
t0 = get_time();
ibv_post_send(qp, ...);
while (poll_cq(&send_wc) == 0); // Wait for local send completion
while (poll_cq(&recv_wc) == 0); // Wait for remote reply
t1 = get_time();
latency = (t1 - t0) / 2.0;
```

#### Why Custom Tools Often Disagree with `perftest`
1. **Polling Send CQ inside the loop**: If you set `sq_sig_all = 1`, you must poll two completions per round-trip (Send completion + Recv completion). If `ib_send_lat` uses unsignaled sends or polls them opportunistically, its loop does half the CQ work.
2. **Buffer Reuse & Cache Warmth**: `ib_send_lat` reuses the exact same cache lines across iterations. The buffer remains permanently resident in CPU L1 cache ($0.5\ \text{ns}$ access time). If your code reallocates buffers or cycles through large arrays, you pay L3 cache or RAM miss penalties ($50–80\ \text{ns}$).
3. **Inline Flag Mismatch**: `ib_send_lat` defaults to inline sends for small messages ($\le 400$ bytes). If you omit `IBV_SEND_INLINE`, you incur PCIe read DMA overhead on every packet.

---

### 3.5 Cycle Attribution with `perf stat` and `perf record`

#### Why it matters here
In Deliverable D1, you must report *"CPU overhead: `perf stat` cycles per `ibv_poll_cq` call."* You need to prove whether your polling loop is spinning wastefully or retiring useful instructions.

#### 1. Quantifying Cycles with `perf stat`
Run `perf stat` attached specifically to the core executing your ping-pong benchmark:

```bash
taskset -c 4 perf stat -e cycles,instructions,cache-references,cache-misses,context-switches \
    ./build/pingpong --client 127.0.0.1 --size 64 --iters 1000000
```

#### How to calculate Cycles per `ibv_poll_cq`:
1. Record the total cycles elapsed from `perf stat` ($C_{\text{total}}$).
2. Record the total number of `ibv_poll_cq()` invocations in your code ($N_{\text{polls}}$) using a 64-bit counter outside the timed loop:
   $$\text{Cycles per Poll} = \frac{C_{\text{total}}}{N_{\text{polls}}}$$
* **Healthy Target**: A tight assembly spin loop checking an mmapped CQ status flag takes **5 to 15 CPU cycles** per empty poll.
* **Problem Symptom**: If cycles per poll exceeds 100 cycles, you have function call overhead, memory barrier thrashing, or virtual dispatch polluting the hot loop.

#### 2. Profiling the Hot Path with `perf record`
To verify that no hidden kernel calls or memory allocations exist in your timed path:

```bash
# 1. Profile at 999 Hz with call graphs enabled
taskset -c 4 perf record -F 999 -g -- ./build/pingpong --client 127.0.0.1 --size 64 --iters 1000000

# 2. View the interactive terminal report
perf report --stdio --no-children
```

* **What you should see**: $> 90\%$ of cycles concentrated in `ibv_poll_cq` (specifically `rxe_poll_cq` in userspace).
* **What you should NOT see**: Any traces of `__libc_malloc`, `page_fault`, `sys_enter`, or C++ runtime exception unwinding functions.

---

### 3.6 The Scientific Method & Writing Impressive `gap_analysis.md` Entries

#### Why it matters here
Hiring managers and senior infrastructure engineers look at `docs/gap_analysis.md` before they read your code. A document showing that your code ran $3\times$ slower than expected, where you formed an architectural hypothesis, ran a test to isolate the hardware cause, and proved the resolution, proves high-level engineering maturity.

#### The 4-Step Analytical Framework
Every entry in `docs/gap_analysis.md` must follow this structure:
1. **Hypothesis**: Formulate a falsifiable technical prediction based on hardware or protocol mechanics *before* running the test.
2. **Experimental Prediction**: State the exact numerical metric expected (e.g., *"P50 latency will increase by $1.8\times$ because..."*).
3. **Measured Result**: Present the actual CSV data with complete provenance metadata.
4. **Root Cause Analysis & Engineering Conclusion**: Explain why the observation matched or diverged from the prediction.

---

#### Worked Example Entry for `docs/gap_analysis.md`

```markdown
### Experiment Log: GAP-004 - Evaluation of Inline Sends vs Registered DMA on Soft-RoCE

* **Date**: 2026-10-28
* **Git Commit**: `b3e4f1a`
* **Hardware Config**: Intel Core i3-1220P, Linux 6.8.0-generic, Core 4 (E-core, 2.4 GHz fixed)
* **Deliverable**: D1 (Ping-Pong Latency Harness)

#### 1. Hypothesis
In hardware RDMA (ConnectX-5), setting `IBV_SEND_INLINE` for message sizes <= 256 bytes eliminates the PCIe read DMA transaction, reducing one-way latency by ~300 ns. On Soft-RoCE (`rdma_rxe`), because the driver executes in software on the CPU, we hypothesize that inline sends will show a smaller relative benefit, but will still outperform registered memory transfers below 128 bytes by avoiding `ib_umem` virtual-to-physical address translation lookups in the kernel worker thread.

#### 2. Quantitative Prediction
* For 64-byte payloads: `IBV_SEND_INLINE` will reduce P50 one-way latency from ~11.5 µs to ~9.5 µs (a ~17% improvement).
* For 1024-byte payloads: Inline sends will fail with `EINVAL` because `max_inline_data` is configured to 256 bytes.

#### 3. Measured Results (1,000,000 Samples per Run)
```csv
# provenance: date=2026-10-28T14:30:00Z, git_commit=b3e4f1a, hardware_config_id=i3-1220P_E4, governor=performance, turbo=fixed_2.4G
message_size,transport,inline_mode,p50_us,p90_us,p99_us,p99.9_us
64,rc,disabled,11.42,12.80,18.91,32.10
64,rc,enabled,9.21,10.45,15.20,24.80
256,rc,disabled,13.10,14.60,21.05,36.40
256,rc,enabled,11.85,13.10,18.40,31.20
512,rc,enabled,FAILED (ibv_post_send rejected with EINVAL: max_inline_data=256)
```

#### 4. Engineering Conclusion & System Insights
1. **Observation**: Inline sends reduced 64-byte P50 latency by 2.21 µs (19.3%), matching our prediction. 
2. **Root Cause Analysis**: Inspecting `rdma-core/providers/rxe/rxe.c` confirmed that when `IBV_SEND_INLINE` is set, `rxe_post_send` copies the user buffer directly into the WQE payload array (`wqe->dma.sge[0].addr`). When disabled, the kernel thread `rxe_worker` must invoke `copy_from_user` or walk the `ib_umem` scatterlist to access user pages, incurring cache misses.
3. **Action Item**: Configured `max_inline_data = 256` as the default in `pingpong/pingpong.cc` for all messages $\le 256$ bytes.
```

---

### Self-Check Questions (Part 3)

1. Why does executing `__rdtsc()` without memory fences (`_mm_lfence()`) produce invalid latency intervals on modern out-of-order processors?
2. Why is measuring $10{,}000$ iterations insufficient to report a statistically sound P99.9 latency metric?
3. On an Intel Core i3-1220P, why will running a benchmark on Core 0 and Core 1 simultaneously cause high tail latency, even if both cores are P-cores?
4. How does `ib_send_lat` avoid clock synchronization issues between two physically separate computers?

<details>
<summary><b>Click to View Answers</b></summary>

1. **Answer**: Modern out-of-order execution engines can schedule instructions speculatively. Without a serializing instruction (`_mm_lfence` or `rdtscp`), the second `__rdtsc()` read can be executed by the pipeline before the preceding network calls or memory stores complete, leading to under-reported or negative latency intervals.
2. **Answer**: At $10{,}000$ iterations, the 99.9th percentile is represented by only $10$ outlier data points ($10{,}000 \times 0.001 = 10$). A sample size of 10 events is statistically noisy and susceptible to random OS jitter. To obtain an empirically stable P99.9 distribution, you need at least $1{,}000$ occurrences of the tail event, requiring a minimum of $1{,}000{,}000$ total iterations.
3. **Answer**: Core 0 and Core 1 on Linux usually map to CPU 0 and CPU 1, which are **SMT (Hyper-Threading) siblings on the same physical P-core**. They share physical execution units, L1 data caches, and TLBs. Heavy contention between the two hardware threads causes pipeline stalls and cache line evictions that ruin tail latency.
4. **Answer**: `ib_send_lat` measures the full Round-Trip Time (RTT) locally on a single machine's clock (Client $\to$ Server $\to$ Client) and divides the resulting elapsed time by 2. It never compares timestamps taken on different machines, eliminating clock drift and synchronization errors.
</details>

---

### 3.7 If You Remember Nothing Else (Part 3)
1. Use `CLOCK_MONOTONIC_RAW` or serialized `rdtsc` (`_mm_lfence()`); standard monotonic clocks can slew during NTP frequency corrections.
2. Never report averages: evaluate P50, P99, and P99.9 over at least 1,000,000 samples, and always discard the first 5,000 warmup iterations.
3. Pin your benchmark processes to the homogeneous E-core cluster (Cores 4, 5, 6, 7) and lock the frequency governor to `performance`.
4. Match `perftest` conventions: calculate one-way latency by dividing Round-Trip Time by 2, and use identical inline flags and buffer reuse policies.
5. In `docs/gap_analysis.md`, formulate your hypothesis and numerical prediction *before* running the benchmark, and verify cycle counts using `perf stat`.
