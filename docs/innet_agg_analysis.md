# In-Network Aggregation & eBPF Analysis (D4)

## Objective
Evaluate the gradient volume reduction and end-to-end all-reduce latency using switch-level eBPF/XDP aggregation versus a standard UDP/TCP socket ring.

## Verification & Verifier Log Notes
- Program: `innet_agg/xdp_agg.bpf.c`
- Kernel Verifier Output / Bounds Checks:
  - Loop unrolling: `#pragma unroll` applied to bounded loops ($< 256$ elements).
  - Packet boundary assertions verified with `ctx->data_end`.

## Quantization Accuracy
- Gradient representation: 32-bit fixed point with 16 fractional bits.
- Target Scale: $N \times \text{max\_abs\_val} < 2^{31}-1$.
- Maximum observed quantization error vs IEEE-754 FP32 sum: [Pending]
- Overflow count (tracked via C23 `ckd_add`): [Pending]

## Traffic & Latency Comparison

| Nodes ($N$) | Buffer Size ($S$) | Socket Ring Egress (bytes/node) | In-Network Egress (bytes/node) | Traffic Reduction (%) | E2E Latency Overhead |
|---|---|---|---|---|---|
| 2 | 1024 B | 1024 B | 1024 B | 0% | - |
| 4 | 1024 B | 1536 B | 1024 B | 33.3% | - |
| 8 | 1024 B | 1792 B | 1024 B | 42.8% | - |
