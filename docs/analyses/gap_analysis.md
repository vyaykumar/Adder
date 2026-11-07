# Engineering Gap Analysis Log

The core discipline of this project is hypothesis-driven engineering. Before executing any major benchmark sweep, record your **Hypothesis**. After the test, record the **Result**, **Discrepancy (Gap)**, and **Root Cause Analysis**.

---

## Template

### Experiment: [Short Name]
- **Date**: YYYY-MM-DD
- **Git Commit**: `[hash]`
- **Hardware Config**: [Local / Cloud / etc.]
- **Goal**: [What is being evaluated?]

#### 1. Hypothesis (Written BEFORE running test)
*What latency, bandwidth, or scaling behavior do you expect, and why? What are the theoretical hardware limits?*

#### 2. Experimental Result
*What did the benchmark report? (Reference specific CSV or plot)*

#### 3. Gap Analysis & Root Cause
*Did the observed numbers match the hypothesis? If there was a discrepancy, what architectural or kernel factor caused it (e.g. Soft-RoCE CPU overhead, TLB misses, core pinning mismatch, RNR retry timers)?*

---

## Log Entries

### Entry 000: Perftest Baseline — Loopback vs. Cloud Distributed (D1.1)
- **Date**: 2026-10-31
- **Hardware Config**: Machine Profile 2 (Debian 12 Bare-Metal) + Machine Profile 3 (Azure Linux VM)
- **Goal**: Establish the empirical ground truth for `ib_send_lat`, `ib_write_lat`, and `ib_send_bw` across Standalone Bare-Metal Loopback and Azure Cloud Distributed execution before writing `pingpong.cc`.

#### 1. Hypothesis (Written BEFORE running test)
1. **Loopback Dimension (Bare-Metal Debian 12)**:
   - On the same host with Soft-RoCE (`rxe0`), the kernel detects a local destination IP, applies `RXE_LOOPBACK_MASK`, and bypasses `dev_queue_xmit` via `rxe_loopback()`.
   - Small message (64B) $P_{50}$ latency should be in the range of **3–5 µs** on bare metal without virtualization/hypervisor overhead, dominated by CPU memory copy between kernel worker threads and user/kernel context switches.
   - Bandwidth will be memory-bus and CPU copy-rate bound rather than physical link bound.
2. **Distributed Cloud Dimension (Azure Linux VM)**:
   - ~~*Physical Ethernet Cable Homelab Option*~~: **Marked out / Omitted**. Physical cross-host cable bridging (WSL2 hypervisor traversal, USB-C adapter, ICS routing) has been forgone in favor of clean Azure Linux cloud instances.
   - Packets across cloud nodes traverse the native virtual network (VNet) interface with standard Linux kernel routing.
   - Small message (64B) $P_{50}$ latency is governed by cloud datacenter network fabric latency and vCPU scheduling.
   - Large messages (1MB+) will reflect cloud link throughput.
3. **Acceptance Criteria for D1 `pingpong.cc`**:
   - Once implemented, our custom `pingpong` tool must track within $\le 20$% of these recorded `ib_send_lat` baselines on the same hardware.

#### 2. Experimental Result
- **Debian Bare-Metal Loopback Baseline** (`results/baseline_loopback_send_lat.txt`):
  - **Transport**: RC over `rxe0`, GID index 1 (`::ffff:192.168.137.2`), MTU 1024B.
  - **Small-Message Latency (64B)**:
    - $t_{\text{min}} = 2.95\ \mu\text{s}$
    - $t_{\text{typical}} = 3.11\ \mu\text{s}$ ($P_{50}$ ground-truth target)
    - $t_{\text{avg}} = 3.21\ \mu\text{s}$
    - $P_{99} = 3.60\ \mu\text{s}$
    - $P_{99.9} = 22.43\ \mu\text{s}$
  - **MTU Inflection Point (1024B)**:
    - 1024B: $t_{\text{typical}} = 3.72\ \mu\text{s}$
    - 2048B (2 packets): $t_{\text{typical}} = 6.15\ \mu\text{s}$ ($1.65\times$ jump due to 2nd segment)
    - 4096B (4 packets): $t_{\text{typical}} = 9.99\ \mu\text{s}$
  - **Large-Message Bandwidth Saturation (1MB - 8MB)**:
    - 1MB: $t_{\text{typical}} = 2541.57\ \mu\text{s}$ ($\sim 2.54\text{ ms}$, bandwidth $\approx 412\text{ MB/s}$)
    - 8MB: $t_{\text{typical}} = 22440.85\ \mu\text{s}$ ($\sim 22.44\text{ ms}$, bandwidth $\approx 373\text{ MB/s}$)
  - **Tail Latency Anomalies**:
    - At 256B and 512B, observed transient $t_{\text{max}} \ge 660\ \mu\text{s}$ ($>200\times$ typical), proving kernel worker scheduling jitter under `PREEMPT_DYNAMIC`.
- **Azure Cloud Distributed Baseline**: [Pending Azure VM deployment]

#### 3. Gap Analysis & Root Cause
- **Hypothesis vs. Result**:
  - The hypothesis predicted small-message latency of $10\text{--}18\ \mu\text{s}$ on general Soft-RoCE. The observed $3.11\ \mu\text{s}$ on the AMD A9 dual-core bare-metal CPU was significantly lower because both sender and receiver ran on Cores 0 and 1 with direct shared memory short-circuiting in `rxe_loopback()` without virtual machine hypervisor overhead.
  - **Official Target for D1 `pingpong.cc`**:
    At 64B on the Debian bare-metal node, our custom implementation must achieve $P_{50} \le 3.11 \times 1.20 = \mathbf{3.73\ \mu s}$.

---

### Entry 001: Ping-Pong Inline vs. Registered Sends (D1)
- **Hypothesis**: For small messages ($\le 128\text{ B}$), `IBV_SEND_INLINE` should yield lower latency by eliminating PCIe DMA read transactions.
- **Result**: [Pending run]
- **Gap & Conclusion**: [Pending run]

### Entry 002: Ring vs. Tree All-Reduce Crossover Point (D2)
- **Hypothesis**: Binary tree will achieve lower latency for small messages ($< 64\text{ KB}$) due to fewer step hops ($2\log_2 N$ vs $2(N-1)$), but Ring will dominate for $\ge 256\text{ KB}$ due to full link bandwidth saturation moving only $2\frac{N-1}{N}S$ bytes.
- **Result**: [Pending run]
- **Gap & Conclusion**: [Pending run]
