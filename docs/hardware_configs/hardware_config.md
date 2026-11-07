# Hardware & System Configuration Log

Use this document to record the exact hardware and environment details of each machine used during the evaluation (local laptop, VM, server rack, and cloud instances).

---

## Machine Profile 1: Initial Local Testbed (VijayKumar Laptop)
- **Target Type**: Windows 11 Host + CLion Linux Toolchain (Intel Core i3-1220P)
- **CPU Model**: 12th Gen Intel Core i3-1220P (Alder Lake)
- **Cores / Threads**: 10 Cores / 12 Threads
- **Topology**: 
  - 2 Golden Cove Performance Cores (P-cores): Cores 0-1 (4 threads with SMT)
  - 8 Gracemont Efficiency Cores (E-cores): Cores 2-9 (Single-threaded, 2 clusters of 4 sharing L2)
  - *Benchmark Pinning Rule*: Pin all $N=4$ runs to homogeneous E-cores (e.g., Cores 4, 5, 6, 7) to eliminate artificial $2\times$ hardware stragglers.
- **RAM**: 16 GB DDR4/DDR5
- **Compiler**: GCC 14.1 / Clang 18.1 (`-std=c++23 -O3 -march=native`)
- **Execution Role**: Local code authoring, build verification, and IDE debugging.

---

## Machine Profile 2: Bare-Metal Physical Node (Debian 12)
> **Role**: Dedicated bare-metal reference host for unthrottled Linux kernel execution, loopback baselines, and local performance benchmarking without virtualization overhead.
- **Target Type**: Bare-Metal Physical Server (Debian 12 Bookworm)
- **Operating System / Kernel**: Debian 12 (Bookworm) / `Linux homelab 6.1.0-48-amd64 #1 SMP PREEMPT_DYNAMIC Debian 6.1.172-1 (2026-05-15) x86_64`
- **CPU Model**: AMD A9-9425 RADEON R5, 5 COMPUTE CORES 2C+3G (Stoney Ridge)
- **Cores / Threads**: 2 Cores / 2 Threads (Single-socket, 1 thread/core, NUMA node 0: CPUs 0,1)
- **RAM**: 3.7 GiB total (DDR4)
- **RDMA Device**: `rxe0` (Soft-RoCE bound to physical netdev, node_guid: `dad0:90ff:fe52:5234`)
- **Port State**: `PORT_ACTIVE (4)`, link_layer: `Ethernet`
- **Active MTU**: 1024 bytes (max_mtu 4096, constrained by standard 1500B Ethernet frame)
- **RoCEv2 GID**: `::ffff:192.168.137.2` (GID Table Index 1)
- **CPU Scaling Governor**: `performance`
- **Locked Memory Limit (`ulimit -l`)**: `unlimited`
- **Scope Note**: Used strictly for bare-metal standalone loopback profiling and baseline verification. Physical cross-host Ethernet cable links (e.g., USB-C to Ethernet adapter bridging, ICS routing) are **omitted/marked out** in favor of cloud-based distributed runs (Machine Profile 3).

---

## Machine Profile 3: Dedicated Azure Cloud Instance (Primary Distributed Target)
- **Target Type**: Dedicated Azure Linux Cloud VM (e.g. `Standard_D4s_v5` or `Standard_F4s_v2`)
- **Role**: Primary distributed multi-node execution target, replacing local cross-host physical Ethernet cables. Provides clean native Linux kernel routing, unthrottled vCPUs, and multi-node collective scaling without hypervisor/adapter friction.
- **Provider & Instance Type**: Azure Linux (Ubuntu 24.04 LTS / 22.04 LTS)
- **CPU Architecture**: Dedicated x86_64 vCPUs (Intel Xeon / AMD EPYC)
- **RDMA Interconnect**: Soft-RoCE (`rdma_rxe`) / Accelerated Networking
- **Active MTU**: Configurable (supports 1500 to 9000 jumbo frames)
- **Locked Memory Limit (`ulimit -l`)**: `unlimited`
