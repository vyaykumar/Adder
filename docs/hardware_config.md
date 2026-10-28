# Hardware & System Configuration Log

Use this document to record the exact hardware and environment details of each machine used during the evaluation (local laptop, VM, server rack, and cloud instances).

## Machine Profile 1: Initial Local Testbed
- **Date**: YYYY-MM-DD
- **Target Type**: Local Machine / VM / Cloud Instance
- **CPU Model**: (e.g. 12th Gen Intel Core i3-1220P)
- **Cores / Threads**: 
- **Topology**: P-Cores vs E-Cores breakdown (verify with `lscpu` or `adder::shared::CpuTopology::discover()`)
- **RAM**: 
- **Operating System / Kernel**: (e.g. Linux 6.8.0-generic)
- **RDMA Device**: (e.g. `rxe0` over `eth0`, ConnectX-5 `mlx5_0`)
- **Active MTU**: (e.g. 1024 or 4096)
- **CPU Scaling Governor**: (e.g. `performance`)
- **Turbo State**: (e.g. `disabled`)
- **Compiler**: (e.g. GCC 14.1 / Clang 18.1)

---

## Machine Profile 2: Cloud Instance / Unbridled Run
- **Date**: 
- **Provider & Instance Type**: (e.g. AWS c6i.metal, Azure HB120rs, GCP c2-standard-16)
- **CPU Model**: 
- **Topology & NUMA Layout**: 
- **RDMA Interconnect**: (e.g. Mellanox RoCE / InfiniBand / AWS EFA)
- **Active MTU**: 
- **Scaling Governor & Turbo**: 
