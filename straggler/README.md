# Deliverable D3: Straggler-Aware Chunk Scheduling

## Overview
`adder_straggler` addresses tail latency degradation in distributed collective communication:
- **Detection**: Monitors chunk and round completion latencies via CQ timestamps, maintaining an Exponential Moving Average (EMA) of mean and variance. A node is flagged when completion exceeds $\mu + 2\sigma$ for 3 consecutive rounds.
- **Adaptive Scheduling**: Reassigns chunks dynamically from stragglers to fast nodes while preserving the **Conservation Invariant** ($\sum \text{chunks}_i = \text{Total Chunks}$).
- **Epoch Synchronization**: Rank 0 broadcasts new schedules over TCP with epoch numbers and start rounds $R \ge \text{current} + 2$. Nodes finish their active round with the old schedule and switch synchronously at round $R$.

## Running the Evaluation

### Baseline without Mitigation (Degraded Tail Latency)
```bash
./straggler_bench --world-size 4 --inject-rank 2 --delay-us 500 --iters 1000 --output ../results/straggler_tail_latency.csv
```

### With Adaptive Mitigation
```bash
./straggler_bench --world-size 4 --inject-rank 2 --delay-us 500 --mitigate --iters 1000 --output ../results/straggler_tail_latency.csv
```

## Success Targets
- **P99 Degradation**: $\le 50$% under a $2\times$ straggler with mitigation (vs. $\ge 200$% without mitigation).
- **Detection Latency**: Detect the straggler within 2 all-reduce rounds with $\ge 90$% accuracy.
