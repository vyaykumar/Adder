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

### Entry 001: Ping-Pong Inline vs. Registered Sends (D1)
- **Hypothesis**: For small messages ($\le 128\text{ B}$), `IBV_SEND_INLINE` should yield lower latency by eliminating PCIe DMA read transactions.
- **Result**: [Pending run]
- **Gap & Conclusion**: [Pending run]

### Entry 002: Ring vs. Tree All-Reduce Crossover Point (D2)
- **Hypothesis**: Binary tree will achieve lower latency for small messages ($< 64\text{ KB}$) due to fewer step hops ($2\log_2 N$ vs $2(N-1)$), but Ring will dominate for $\ge 256\text{ KB}$ due to full link bandwidth saturation moving only $2\frac{N-1}{N}S$ bytes.
- **Result**: [Pending run]
- **Gap & Conclusion**: [Pending run]
