# Deliverable D2: Ring All-Reduce & Binary Tree

## Overview
`adder_allreduce` implements collective all-reduce algorithms directly on `libibverbs` over Soft-RoCE or hardware RDMA:
1. **Ring All-Reduce**: Theoretical minimum communication volume of $2 \frac{N-1}{N} \cdot S$ bytes across $2(N-1)$ steps.
2. **Binary Tree All-Reduce**: $2 \log_2(N)$ steps moving $2 \log_2(N) \cdot S$ bytes.
3. **Comparative Analysis**: Evaluates the crossover message size where ring throughput overtakes tree latency, and benchmarks performance against `MPI_Allreduce`.

## Core Allocation & Pinning Rules
- **Homogeneous Core Pinning**: Run each node on its own pinned core. If testing on hybrid chips (like Intel 12th/13th Gen i3-1220P), pin all 4 nodes strictly to E-cores to prevent artificial hardware-induced stragglers.
- **Reporting**: Report $N=2$ and $N=4$ as clean results. $N=8$ is reported as an oversubscribed stress test.

## Running the Benchmark

### 1. Single-Node Emulation (4 Processes via Network Namespaces or Pinned Ports)
```bash
# Node 0
./allreduce_bench --rank 0 --world-size 4 --size 1048576 --pin-core 4 --output ../results/allreduce_bandwidth.csv

# Node 1
./allreduce_bench --rank 1 --world-size 4 --size 1048576 --pin-core 5

# Node 2
./allreduce_bench --rank 2 --world-size 4 --size 1048576 --pin-core 6

# Node 3
./allreduce_bench --rank 3 --world-size 4 --size 1048576 --pin-core 7
```

### 2. Message Size Sweep (Ring vs. Tree Crossover)
Sweep buffer sizes: 1KB, 4KB, 16KB, 64KB, 256KB, 1MB, 4MB, 16MB.
```bash
for size in 1024 4096 16384 65536 262144 1048576 4194304 16777216; do
    ./allreduce_bench --algorithm ring --size $size --output ../results/ring_vs_tree.csv
    ./allreduce_bench --algorithm tree --size $size --output ../results/ring_vs_tree.csv
done
```

### 3. Comparison with MPI
Ensure MPI is forced onto TCP or Soft-RoCE:
```bash
mpirun -np 4 --mca btl tcp,self ./allreduce_bench --compare-mpi --output ../results/mpi_comparison.csv
```
Target: Reach 70% to 80% of MPI bandwidth at message sizes $\ge 1\text{ MB}$.
