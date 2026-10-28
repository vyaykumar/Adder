# Deliverable D4: In-Network Aggregation Emulation (Stretch Goal)

## Overview
`innet_agg` emulates switch-level gradient reduction using eBPF/XDP with a C23 userspace fallback:
- **Gradient Quantization**: Fixed-point 32-bit integer representation (SwitchML style) to operate within eBPF verifier and kernel limitations.
- **Traffic Reduction**: Decreases egress data movement from $2 \frac{N-1}{N} \cdot S$ (standard ring) down to $S$ bytes per node.
- **Comparison Baseline**: Evaluated against the UDP/TCP socket ring (`sock_ring.cc`), avoiding unfair comparisons against kernel-bypass RDMA verbs.

## Building and Running

### 1. eBPF / XDP Target (Requires clang and libbpf)
```bash
# Attach XDP program to interface (e.g. eth0 or veth)
sudo ./agg_control eth0 xdp_agg.bpf.o
```

### 2. C23 Userspace Fallback
```bash
# Run userspace aggregator on dedicated core
./agg_userspace 9999
```

### 3. Comparison Benchmark
```bash
./sock_ring_bench --world-size 4 --output ../results/innet_bytes_per_node.csv
```
