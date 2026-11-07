# Man 03: Memory Registration, Keys, & Pre-Registered Pools

## 1. Physical Mechanics of Memory Registration

### 1.1 Why Memory Registration is Necessary
In modern operating systems, applications operate in **virtual memory**:
* Virtual addresses are dynamically mapped to physical RAM pages by CPU Page Tables.
* The Linux kernel can swap pages out to disk, migrate pages between NUMA nodes, or perform copy-on-write page deduplication.

**The RDMA Hardware Constraint**:
A PCIe Network Interface Card (HCA) cannot handle CPU page faults. When performing Direct Memory Access (DMA), the HCA requires:
1. The **exact physical memory addresses** of the buffer.
2. A guarantee that the operating system **will never move or swap out** those physical pages during transfer.

```
[ Application Virtual Memory: 0x7fff_0000 ]
                     │
          ibv_reg_mr() System Call
                     ▼
[ Linux Kernel (ib_umem_get) ]
  ├── 1. Pins Physical RAM Pages (Increments page refcount, disables swap)
  └── 2. Resolves Virtual Address -> Physical Page Pointers (scatterlist)
                     ▼
[ Hardware RDMA HCA (MTT Cache) ]
  └── 3. Stores translation table in NIC Memory Translation Table (MTT)
  └── 4. Issues lkey and rkey
```

### 1.2 The High Cost of `ibv_reg_mr()`
Calling `ibv_reg_mr()` is an **extremely expensive operation** ($\sim 10\text{ to }100\ \mu\text{s}$):
* Traverses kernel page tables and locks virtual memory ranges.
* Modifies kernel internal memory accounting structures.
* Issues PCIe transactions to program the NIC's on-board Memory Translation Table (MTT).

> [!IMPORTANT]
> **Strict Hot-Path Rule**: Never call `ibv_reg_mr()` or `ibv_dereg_mr()` during a timed benchmark or collective reduction. All memory must be pre-allocated and registered once during application startup.

---

## 2. Memory Keys & Access Permissions

When `ibv_reg_mr()` succeeds, it returns `struct ibv_mr` containing two critical keys:

```cpp
struct ibv_mr {
    struct ibv_context *context;
    struct ibv_pd      *pd;
    void               *addr;
    size_t              length;
    uint32_t            handle;
    uint32_t            lkey;       // Local Key
    uint32_t            rkey;       // Remote Key
};
```

### 2.1 Local Key (`lkey`)
* Used locally when submitting Send or Receive Work Requests via Scatter/Gather Elements:
  ```cpp
  struct ibv_sge sge{};
  sge.addr   = (uint64_t)buffer_ptr;
  sge.length = payload_bytes;
  sge.lkey   = mr->lkey;
  ```
* Authenticates to the local HCA that the buffer belongs to the Queue Pair's Protection Domain.

### 2.2 Remote Key (`rkey`)
* An authorization token passed out-of-band (over TCP) to remote nodes.
* Remote nodes specify `rkey` and `remote_addr` during one-sided `IBV_WR_RDMA_WRITE` and `IBV_WR_RDMA_READ`.
* If a remote node attempts to write outside the registered bounds or with an incorrect `rkey`, the local HCA aborts the DMA and responds with `IBV_WC_REM_ACCESS_ERR`.

### 2.3 Access Flags
Combine flags using bitwise OR:
* `IBV_ACCESS_LOCAL_WRITE`: Mandatory for any buffer receiving data (Recv, Write destination, Read destination).
* `IBV_ACCESS_REMOTE_WRITE`: Authorizes remote peers to execute one-sided `RDMA_WRITE` into this memory.
* `IBV_ACCESS_REMOTE_READ`: Authorizes remote peers to execute one-sided `RDMA_READ`.

---

## 3. The `RLIMIT_MEMLOCK` & `ulimit -l` Trap

When `ibv_reg_mr()` calls `ib_umem_get()`, the kernel checks the process's **Locked Memory Limit (`RLIMIT_MEMLOCK`)**.

* **The Problem**: On stock Linux distributions (including Debian, Ubuntu, and WSL2), the default soft limit is often set to **64 KB**.
* Attempting to register a 1 MB buffer causes `ibv_reg_mr()` to fail immediately with **`ENOMEM` (Cannot allocate memory)**, misleading developers into believing physical RAM is exhausted!

### The Solution:
1. **At Runtime**:
   ```bash
   ulimit -l unlimited
   ```
2. **Permanent System Configuration** (`/etc/security/limits.conf`):
   ```text
   * soft memlock unlimited
   * hard memlock unlimited
   ```

---

## 4. Hugepages (`MAP_HUGETLB`) Optimization

Standard Linux memory uses **4 KB pages**. For a 16 MB message buffer:
$$\frac{16\text{ MB}}{4\text{ KB}} = 4{,}096\text{ page table entries}$$

* The CPU Translation Lookaside Buffer (TLB) and the NIC's on-chip Memory Translation Table (MTT) must cache thousands of entries, leading to frequent TLB misses.
* By allocating memory backed by **2 MB Hugepages** (`MAP_HUGETLB`), the entire 16 MB buffer requires only:
  $$\frac{16\text{ MB}}{2\text{ MB}} = 8\text{ page table entries}$$

### Hugepage Allocation in C++:
```cpp
void* ptr = mmap(
    nullptr,
    total_size,
    PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
    -1,
    0
);
if (ptr == MAP_FAILED) {
    // Fallback to posix_memalign page-aligned allocation
}
```

---

## 5. Pre-Registered Memory Pool Implementations

Adder provides two high-performance memory pool patterns in `shared/`:

### 5.1 `FreeListMemoryPool` (`shared/free_list_pool.h`)
Designed for high-throughput chunk recycling:
* Allocates a single contiguous block of memory (e.g. 64 chunks of 64 KB each) and registers it with `ibv_reg_mr()` once.
* Manages chunk indices using an internal vector stack (`free_indices_`).
* **`acquire_chunk()`**: Pops an available chunk index in $O(1)$ time with zero heap allocation.
* **`release_chunk()`**: Pushes the chunk index back onto the stack in $O(1)$ time.

```
Contiguous Registered Memory Region (Single MR, Single lkey/rkey):
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ Chunk 0  │ Chunk 1  │ Chunk 2  │ Chunk 3  │ ...      │
│ (64 KB)  │ (64 KB)  │ (64 KB)  │ (64 KB)  │          │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

---

### 5.2 `GuardedMemoryPool` (`shared/guarded_memory_pool.h`)
Designed for debugging buffer overruns during collective algorithm development:
* Interleaves a hardware-protected **`PROT_NONE` guard page** immediately following every single chunk payload.
* If a scatter-reduce vector addition or incoming network packet overruns its allocated chunk by even 1 byte, the CPU or DMA engine hits `PROT_NONE` and triggers an immediate **`SIGSEGV`**.

```
Guarded Memory Layout:
┌───────────────────────────┬────────────────┐┌───────────────────────────┬────────────────┐
│      Chunk 0 Payload      │ Guard Page #0  ││      Chunk 1 Payload      │ Guard Page #1  │
│      (PROT_READ/WRITE)    │  (PROT_NONE)   ││      (PROT_READ/WRITE)    │  (PROT_NONE)   │
└───────────────────────────┴────────────────┘└───────────────────────────┴────────────────┘
                                  ▲                                             ▲
                           Hardware Trap                                 Hardware Trap
```
