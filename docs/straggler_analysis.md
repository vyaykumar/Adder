# Straggler Scheduling Analysis (D3)

## Objective
Evaluate the detection accuracy and tail-latency mitigation performance of the adaptive chunk scheduler when a synthetic straggler is injected into an $N=4$ all-reduce collective.

## Injected Perturbations
1. **Software Spin/Sleep Delay**: 200μs, 500μs, 1000μs, 2000μs delays injected on Rank 2.
2. **Kernel Network Emulation (tc-netem)**: When using packet-level network delay instead of software busy-sleep, tests must execute across two network namespaces (`veth_srv` <-> `veth_cli`) to bypass the kernel's internal `rxe_loopback()` fast path.
3. **Detection Sensitivity Sweep**: Vary $\alpha$ (EMA smoothing factor) from $0.1$ to $0.5$ and $\sigma$ threshold from $1.5$ to $3.0$.

## Experimental Results Summary

| Target Injected Delay | Baseline P99 (no mitigation) | Mitigated P99 | P99 Improvement (%) | Detection Time (rounds) |
|---|---|---|---|---|
| 200 μs | - | - | - | - |
| 500 μs | - | - | - | - |
| 1000 μs | - | - | - | - |
| 2000 μs | - | - | - | - |

## Observations & Schedule Rebalancing Notes
- *Verify Conservation Invariant: Ensure the total chunk count is preserved across all epochs.*
- *Verify Epoch Synchrony: Confirm that no node switched mid-round.*
