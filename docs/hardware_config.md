# Hardware & System Configuration Log

Use this document to record the exact hardware and environment details of each machine used during the evaluation (local laptop, VM, server rack, and cloud instances).

---

## Machine Profile 1: Initial Local Testbed (Heterogeneous Laptop)
- **Target Type**: Local Bare-Metal / Native Linux (or VM)
- **CPU Model**: 12th Gen Intel Core i3-1220P (Alder Lake)
- **Cores / Threads**: 10 Cores / 12 Threads
- **Topology**: 
  - 2 Golden Cove Performance Cores (P-cores): Cores 0–1 (4 threads with SMT)
  - 8 Gracemont Efficiency Cores (E-cores): Cores 2–9 (Single-threaded, 2 clusters of 4 sharing L2)
  - *Benchmark Pinning Rule*: Pin all $N=4$ runs to homogeneous E-cores (e.g., Cores 4, 5, 6, 7) to eliminate artificial $2\times$ hardware stragglers.
- **RAM**: 16 GB DDR4/DDR5
- **Operating System / Kernel**: Linux (e.g. 6.8.0-generic)
- **RDMA Device**: `rxe0` (Soft-RoCE over loopback / local netdev)
- **Active MTU**: 1024 bytes (standard 1500B Ethernet ceiling)
- **CPU Scaling Governor**: `performance`
- **Turbo State**: `disabled` (via `/sys/devices/system/cpu/intel_pstate/no_turbo`)
- **Compiler**: GCC 14.1 / Clang 18.1 (`-std=c++23 -O3 -march=native`)

---

## Machine Profile 2: Physical Homelab Node (Debian 12)
- **Target Type**: Bare-Metal Physical Homelab Server
- **Operating System / Kernel**: Debian 12 (Bookworm) / Linux Kernel 6.1+
- **CPU Model**: [Fill in Debian Host CPU, e.g. Intel / AMD]
- **Cores / Threads**: [Fill in core/thread count]
- **RAM**: [Fill in RAM]
- **Network Interconnect**: Dedicated Ethernet link (1GbE / 2.5GbE / 10GbE)
- **RDMA Device**: `rxe0` (Soft-RoCE bound to physical Ethernet netdev)
- **Active MTU**: 
  - Default: 1024 bytes (with interface MTU 1500)
  - Jumbo Frames: 4096 bytes (with interface MTU 9000)
- **CPU Scaling Governor**: `performance`
- **Locked Memory Limit (`ulimit -l`)**: `unlimited`

---

## Machine Profile 3: Cloud HPC / Bare-Metal Instance (Optional Extension)
- **Date**: [Pending Run]
- **Provider & Instance Type**: Azure Linux / AWS Metal / GCP
- **CPU Model**: [e.g. AMD EPYC / Intel Xeon]
- **RDMA Interconnect**: Mellanox ConnectX (RoCEv2 / InfiniBand)
- **Active MTU**: 4096 bytes 
