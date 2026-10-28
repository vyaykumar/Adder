# Deliverable D1: RDMA Ping-Pong Latency Tool

## Overview
`pingpong` is a microsecond-scale RDMA ping-pong latency benchmark tool built from scratch using `libibverbs`. It evaluates:
- **Transports**: Reliable Connection (RC) vs. Unreliable Datagram (UD)
- **Data Paths**: Two-sided (`IBV_WR_SEND`/`RECV`) vs. One-sided (`IBV_WR_RDMA_WRITE`)
- **Memory Modes**: Standard registered memory vs. `IBV_SEND_INLINE`
- **Active MTUs**: Verifies message size clamping against port MTU boundaries

## Building
```bash
mkdir -p build && cd build
cmake ..
cmake --build . --target pingpong
```

## Running the Benchmark

### 1. Reliable Connection (RC) - Baseline
```bash
# Server / Receiver (Node A)
./pingpong --server --port 18515 --transport rc --size 64 --iters 1000000 --pin-core 2

# Client / Sender (Node B)
./pingpong --client <SERVER_IP> --port 18515 --transport rc --size 64 --iters 1000000 --pin-core 3 --output ../results/pingpong_latency.csv
```

### 2. Unreliable Datagram (UD)
Note: UD messages cannot exceed the active MTU.
```bash
# Server
./pingpong --server --transport ud --size 1024 --pin-core 2

# Client
./pingpong --client <SERVER_IP> --transport ud --size 1024 --pin-core 3 --output ../results/pingpong_latency.csv
```

### 3. One-Sided RDMA Write vs. Two-Sided Send
```bash
# Compare two-sided send
./pingpong --client <SERVER_IP> --size 4096 --output ../results/pingpong_latency.csv

# Compare one-sided RDMA write
./pingpong --client <SERVER_IP> --size 4096 --write --output ../results/pingpong_latency.csv
```

### 4. Inline Sends
```bash
./pingpong --client <SERVER_IP> --size 64 --inline --output ../results/pingpong_latency.csv
```

## Validation Against `perftest`
The results of this tool should match within 20% of `ib_send_lat`:
```bash
# Server
ib_send_lat

# Client
ib_send_lat <SERVER_IP>
```
