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

### Entry 000: Perftest Baseline — Loopback vs. Physical Ethernet (D1.1)
- **Date**: 2026-10-31
- **Hardware Config**: Machine Profile 1 (Intel i3-1220P) + Machine Profile 2 (Debian 12 Homelab)
- **Goal**: Establish the empirical ground truth for `ib_send_lat`, `ib_write_lat`, and `ib_send_bw` across both Single-Host Loopback and Cross-Host Physical Ethernet before writing `pingpong.cc`.

#### 1. Hypothesis (Written BEFORE running test)
1. **Loopback Dimension (Dimension 1)**:
   - On the same host with Soft-RoCE (`rxe0`), the kernel detects a local destination IP, applies `RXE_LOOPBACK_MASK`, and bypasses `dev_queue_xmit` via `rxe_loopback()`.
   - Small message (64B) $P_{50}$ latency should be in the range of **10–18 µs**, dominated by CPU memory copy between kernel worker threads and user/kernel context switches.
   - Bandwidth will be memory-bus and CPU copy-rate bound rather than network-card bound.
2. **Cross-Host Physical Ethernet Dimension (Dimension 2)**:
   - Packets to the Debian 12 homelab must traverse the physical Ethernet MAC/PHY, network cable, and switch/NIC FIFO buffers.
   - Small message (64B) $P_{50}$ latency should be in the range of **50–90 µs** over 1GbE (or **15–25 µs** if 2.5GbE/10GbE), exhibiting higher tail latency ($P_{99}$) due to hardware interrupt coalescing on the physical NICs.
   - Large messages (1MB) will show significant serialization delay on 1GbE ($\sim 8.4\text{ ms}$).
3. **Acceptance Criteria for D1 `pingpong.cc`**:
   - Once implemented, our custom `pingpong` tool must track within $\le 20\%$ of these recorded `ib_send_lat` baselines on the same hardware.

#### 2. Experimental Result
- **Loopback Baseline**: [Record from `results/baseline_loopback_send_lat.txt`]
- **Ethernet Baseline**: [Record from `results/baseline_ethernet_send_lat.txt`]

#### 3. Gap Analysis & Root Cause
- [Pending run comparison]

---

### Entry 001: Ping-Pong Inline vs. Registered Sends (D1)
- **Hypothesis**: For small messages ($\le 128\text{ B}$), `IBV_SEND_INLINE` should yield lower latency by eliminating PCIe DMA read transactions.
- **Result**: [Pending run]
- **Gap & Conclusion**: [Pending run]

### Entry 002: Ring vs. Tree All-Reduce Crossover Point (D2)
- **Hypothesis**: Binary tree will achieve lower latency for small messages ($< 64\text{ KB}$) due to fewer step hops ($2\log_2 N$ vs $2(N-1)$), but Ring will dominate for $\ge 256\text{ KB}$ due to full link bandwidth saturation moving only $2\frac{N-1}{N}S$ bytes.
- **Result**: [Pending run]
- **Gap & Conclusion**: [Pending run]
