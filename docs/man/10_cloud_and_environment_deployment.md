# Man 10: Cloud Deployment, Azure Setup, & Host Architecture

## 1. Multi-Environment Deployment Architecture

Adder is designed to execute portably across three distinct hardware environments:

```
┌────────────────────────────────────────────────────────────────────────┐
│ Environment 1: Local Development (Windows 11 + WSL2 + CLion)           │
│ • Role: Code authoring, static analysis, fast rebuilds, Git workflow. │
├────────────────────────────────────────────────────────────────────────┤
│ Environment 2: Bare-Metal Reference Node (Debian 12 Bookworm)          │
│ • Role: Native Linux kernel baseline execution (zero hypervisor jitter)│
│ • Profiling standalone loopback latencies without virtualization.      │
├────────────────────────────────────────────────────────────────────────┤
│ Environment 3: Primary Distributed Target (Dedicated Azure Cloud VM)   │
│ • Role: Multi-node collective communication, Ring All-Reduce scaling,  │
│   and cross-host network benchmarks.                                   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Dedicated Azure Cloud Deployment Guide

Using an **Azure Linux Virtual Machine** provides clean native Linux kernel execution with accelerated networking, unthrottled vCPUs, and no hypervisor/adapter friction.

### 2.1 Recommended Azure VM SKUs
Under an Azure for Students subscription (\$100 credit):
* **General Purpose**: **`Standard_D4s_v5`** (4 vCPUs, 16 GB RAM, $\sim \$0.19/\text{hr}$).
* **Compute Optimized**: **`Standard_F4s_v2`** (4 vCPUs, 8 GB RAM, $\sim \$0.16/\text{hr}$).
* Both SKUs provide 4 dedicated, homogeneous CPU cores—ideal for $N=4$ all-reduce benchmarks.

---

### 2.2 Provisioning via Azure CLI
```bash
# 1. Create Resource Group
az group create --name AdderRG --location eastus

# 2. Create Azure Linux VM (Ubuntu 24.04 LTS)
az vm create \
  --resource-group AdderRG \
  --name adder-node-0 \
  --image Canonical:ubuntu-24_04-lts:server:latest \
  --size Standard_D4s_v5 \
  --admin-username azureuser \
  --generate-ssh-keys
```

---

### 2.3 Network Security Group (NSG) Port Rules
In Azure, the virtual network firewall blocks unapproved inbound traffic by default. You must open the specific ports used by Adder:

| Port | Protocol | Purpose |
|---|---|---|
| **22** | TCP | SSH remote terminal & CLion remote toolchain |
| **18515** | TCP | Out-of-band QP parameter exchange handshake (D1, D2, D3) |
| **4791** | UDP | **RoCEv2 Data Plane** (Default RoCE UDP port) |
| **9999** | UDP | In-Network Aggregation payload (D4) |
| **19500** | TCP | Socket ring comparison baseline |

```bash
# Open Adder ports in Azure NSG
az vm open-port --resource-group AdderRG --name adder-node-0 --port 18515 --priority 1001
az vm open-port --resource-group AdderRG --name adder-node-0 --port 4791  --priority 1002
az vm open-port --resource-group AdderRG --name adder-node-0 --port 9999  --priority 1003
```

---

### 2.4 Enabling Soft-RoCE on Azure Linux
Once connected via SSH to the Azure VM:

```bash
# 1. Install RDMA libraries and tools
sudo apt update && sudo apt install -y \
    build-essential cmake ninja-build \
    gcc-14 g++-14 \
    libibverbs-dev librdmacm-dev libbpf-dev bpftool \
    rdma-core perftest iproute2

# 2. Load the Soft-RoCE kernel module
sudo modprobe rdma_rxe

# 3. Bind rxe0 to the primary network interface (eth0)
sudo rdma link add rxe0 type rxe netdev eth0

# 4. Verify the port is active
ibv_devinfo -d rxe0
```

---

### 2.5 Cost Management: Stop VMs When Not Benchmarking
To maximize your student credits:
* When not actively benchmarking, deallocate the VM to halt billing:
  ```bash
  az vm deallocate --resource-group AdderRG --name adder-node-0
  ```
* Start the VM again when ready:
  ```bash
  az vm start --resource-group AdderRG --name adder-node-0
  ```

---

## 3. Bare-Metal Reference Node (Debian 12) Guide

The standalone physical Debian 12 machine serves as the unvirtualized baseline host.

### 3.1 Managing the 4 GB RAM Ceiling during Compilation
Heavy C++23 template expansions with `-O3` can consume $\sim 800\text{ MB}$ per compiler thread. To avoid Out-Of-Memory (OOM) kills on 4 GB RAM:

1. **Activate a 2 GB Swap File**:
   ```bash
   sudo fallocate -l 2G /swapfile
   sudo chmod 600 /swapfile
   sudo mkswap /swapfile
   sudo swapon /swapfile
   ```
2. **Limit Build Parallelism to 2 Threads**:
   ```bash
   cmake --build build -j2
   ```

### 3.2 Compiling with Clang 18 on Debian 12
Debian 12 defaults to GCC 12.2 (which lacks C++23 `<expected>`). Install Clang 18 via the official LLVM script:

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 18

# Configure build with Clang 18
cmake -B build -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18 -DCMAKE_BUILD_TYPE=Release
```

---

## 4. Connecting CLion to Remote Environments

You can develop, build, and debug on remote Linux hosts directly from your local Windows CLion IDE:
1. Go to **Settings $\to$ Build, Execution, Deployment $\to$ Toolchains**.
2. Click **`+`** and select **SSH**.
3. Enter the Hostname / IP, Username, and SSH Key for your Azure VM or Debian homelab.
4. CLion will automatically index remote compiler headers, synchronize source files on edit, and execute binaries on the remote host with 1-click debugging.
