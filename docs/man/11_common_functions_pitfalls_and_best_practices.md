# Man 11: Common Functions, Pitfalls, & Best Practices Reference

This document is an exhaustive operational reference for the most frequently used functions in `libibverbs`, POSIX systems programming, and low-latency C++23. For every function, it details the signature, purpose, common pitfalls to avoid, and battle-tested best practices.

---

## Quick Navigation

1. [Device Discovery & Context: `ibv_get_device_list`, `ibv_open_device`](#1-device-discovery--context)
2. [Port & Device Capabilities: `ibv_query_port`, `ibv_query_device`](#2-port--device-capabilities)
3. [Protection Domains: `ibv_alloc_pd`, `ibv_dealloc_pd`](#3-protection-domains)
4. [Memory Registration: `ibv_reg_mr`, `ibv_dereg_mr`](#4-memory-registration)
5. [Completion Queues: `ibv_create_cq`, `ibv_poll_cq`, `ibv_destroy_cq`](#5-completion-queues)
6. [Queue Pairs: `ibv_create_qp`, `ibv_modify_qp`, `ibv_destroy_qp`](#6-queue-pairs)
7. [Data Path Work Requests: `ibv_post_send`, `ibv_post_recv`](#7-data-path-work-requests)
8. [Address Handles: `ibv_create_ah`, `ibv_destroy_ah`](#8-address-handles-ud-transport)
9. [POSIX & Network Systems Primitives: `pthread_setaffinity_np`, `clock_gettime`, Socket IO](#9-posix--network-systems-primitives)

---

## 1. Device Discovery & Context

### `ibv_get_device_list()` & `ibv_free_device_list()`
```c
struct ibv_device **ibv_get_device_list(int *num_devices);
void ibv_free_device_list(struct ibv_device **list);
```
* **Purpose**: Enumerates all InfiniBand, RoCE, and Soft-RoCE adapters present on the host.
* **Pitfalls to Avoid**:
  * **Memory Leak**: Forgetting to call `ibv_free_device_list()`. Note that freeing the list does *not* close opened devices; it only frees the array of device pointers.
  * **Assuming index 0 is valid**: `num_devices` can be 0 if the kernel module (`rdma_rxe` or `mlx5_ib`) is not loaded.
  * **Multi-NIC Confusion**: On cloud or server setups with multiple adapters (e.g. `rxe0`, `mlx5_0`, `mlx5_1`), blindly opening `list[0]` may pick the wrong physical interface.
* **Best Practices**:
  * Iterate the list and match device name via `ibv_get_device_name(list[i])`.
  * Call `ibv_free_device_list()` immediately after `ibv_open_device()`.

---

### `ibv_open_device()` & `ibv_close_device()`
```c
struct ibv_context *ibv_open_device(struct ibv_device *device);
int ibv_close_device(struct ibv_context *context);
```
* **Purpose**: Opens an adapter and creates an operating context for verbs operations.
* **Pitfalls to Avoid**:
  * **Calling `ibv_close_device()` while child resources exist**: If any PD, CQ, or QP created under this context is still active, `ibv_close_device()` fails with `EBUSY` or causes resource leaks in the kernel driver.
* **Best Practices**:
  * Wrap `ibv_context*` in an RAII class or clean up in strict reverse creation order.

---

## 2. Port & Device Capabilities

### `ibv_query_port()`
```c
int ibv_query_port(struct ibv_context *context, uint8_t port_num, struct ibv_port_attr *port_attr);
```
* **Purpose**: Retrieves active port attributes, including port link state, active MTU, GID table length, and subnet LID.
* **Pitfalls to Avoid**:
  * **The `active_mtu` Enum Trap**: `port_attr.active_mtu` is an **enum (`enum ibv_mtu`)**, NOT a byte count!
    * `IBV_MTU_1024` has integer value `3`.
    * `IBV_MTU_2048` has integer value `4`.
    * `IBV_MTU_4096` has integer value `5`.
    * If you use `port_attr.active_mtu` directly as a buffer size, you will allocate a 3-byte buffer!
  * **Ignoring `port_attr.state`**: Transmitting on a port that is `IBV_PORT_DOWN` or `IBV_PORT_INIT` instead of `IBV_PORT_ACTIVE` fails silently.
* **Best Practices**:
  * Always use a conversion function:
    ```cpp
    size_t mtu_to_bytes(enum ibv_mtu mtu) {
        switch (mtu) {
            case IBV_MTU_256:  return 256;
            case IBV_MTU_512:  return 512;
            case IBV_MTU_1024: return 1024;
            case IBV_MTU_2048: return 2048;
            case IBV_MTU_4096: return 4096;
            default:           return 1024;
        }
    }
    ```
  * Assert `port_attr.state == IBV_PORT_ACTIVE` before proceeding.

---

## 3. Protection Domains

### `ibv_alloc_pd()` & `ibv_dealloc_pd()`
```c
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context);
int ibv_dealloc_pd(struct ibv_pd *pd);
```
* **Purpose**: Creates/destroys an isolation domain for memory regions and queue pairs.
* **Pitfalls to Avoid**:
  * **The `EBUSY` Destruction Bug**: `ibv_dealloc_pd()` will return `-1` with `errno = EBUSY` if **any** `ibv_mr` or `ibv_ah` registered under it has not been deregistered first.
* **Best Practices**:
  * Explicitly maintain tracking vectors of allocated MRs and AHs (as implemented in `VerbsQp::track_mr()` and `VerbsQp::destroy()`), ensuring every MR is deregistered before calling `ibv_dealloc_pd()`.

---

## 4. Memory Registration

### `ibv_reg_mr()` & `ibv_dereg_mr()`
```c
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access);
int ibv_dereg_mr(struct ibv_mr *mr);
```
* **Purpose**: Pins physical RAM pages and registers address translation tables with the HCA.
* **Pitfalls to Avoid**:
  * **Calling on the Hot Path**: Calling `ibv_reg_mr()` during a timed benchmark introduces $10\text{--}100\ \mu\text{s}$ kernel page-table overhead.
  * **The `ENOMEM` / `ulimit -l` Trap**: Fails with `ENOMEM` if `ulimit -l` is default (64 KB).
  * **Missing Permissions**: Omitting `IBV_ACCESS_LOCAL_WRITE` on a receive buffer results in `IBV_WC_LOC_PROT_ERR`. Omitting `IBV_ACCESS_REMOTE_WRITE` on a target buffer for `RDMA_WRITE` results in `IBV_WC_REM_ACCESS_ERR`.
  * **Freeing Buffer before Deregistration**: Calling `free(buf)` or `munmap(buf)` before `ibv_dereg_mr(mr)` leaves dangling physical page mappings in the kernel and crashes on shutdown.
* **Best Practices**:
  * Register memory once during startup using `FreeListMemoryPool`.
  * Use page-aligned allocations (`posix_memalign`) or 2 MB hugepages (`mmap` with `MAP_HUGETLB`).

---

## 5. Completion Queues

### `ibv_create_cq()` & `ibv_destroy_cq()`
```c
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe, void *cq_context,
                             struct ibv_comp_channel *channel, int comp_vector);
int ibv_destroy_cq(struct ibv_cq *cq);
```
* **Purpose**: Allocates a Completion Queue for CQEs.
* **Pitfalls to Avoid**:
  * **Undersizing `cqe`**: If `cqe` capacity is smaller than total in-flight sends and receives across all attached QPs, the CQ overflows. On hardware HCAs, this triggers a fatal CQ error state.
* **Best Practices**:
  * Size `cqe >= (max_send_wr + max_recv_wr) * num_attached_qps`.
  * Pass `channel = nullptr` for pure spin-polling hot paths to eliminate completion event infrastructure overhead.

---

### `ibv_poll_cq()`
```c
int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
```
* **Purpose**: Retrieves completed work requests from the CQ register.
* **Pitfalls to Avoid**:
  * **Single-Entry Polling**: Polling with `num_entries = 1` wastes CPU cycles. Batch polling (`num_entries = 8` or `16`) amortizes the polling call cost over multiple completions.
  * **Ignoring `wc.status`**: Assuming `ibv_poll_cq() > 0` implies success. You **must** check `wc[i].status == IBV_WC_SUCCESS`.
  * **Unbounded Spin Without CPU Relaxation**: Spinning in an empty `while(n == 0)` loop without `_mm_pause()` causes CPU instruction pipeline starvation and thermal throttling.
* **Best Practices**:
  * Stack-allocate the completion array:
    ```cpp
    struct ibv_wc wc[16];
    int n = ibv_poll_cq(cq, 16, wc);
    ```
  * In the spin loop, call `_mm_pause()` (or `ADDER_CPU_PAUSE()`) on zero completions:
    ```cpp
    while (completed < required) {
        int ret = ibv_poll_cq(cq, max_wc - completed, wc + completed);
        if (ret > 0) completed += ret;
        else if (ret < 0) return ret;
        else ADDER_CPU_PAUSE();
    }
    ```

---

## 6. Queue Pairs

### `ibv_create_qp()` & `ibv_destroy_qp()`
```c
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr);
int ibv_destroy_qp(struct ibv_qp *qp);
```
* **Purpose**: Allocates a Queue Pair (Send Queue + Receive Queue).
* **Pitfalls to Avoid**:
  * **Garbage in Uninitialized Attributes**: Failing to zero out `struct ibv_qp_init_attr`. Uninitialized fields lead to random `EINVAL` returns.
  * **Ignoring Negotiated Capabilities**: The HCA may grant lower capabilities than requested (e.g. `cap.max_inline_data`).
* **Best Practices**:
  * Always value-initialize the struct: `struct ibv_qp_init_attr init_attr{};`.
  * Inspect `init_attr.cap.max_inline_data` after creation to know your actual inline threshold.

---

### `ibv_modify_qp()`
```c
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask);
```
* **Purpose**: Moves the QP through its state transitions (`RESET` $\to$ `INIT` $\to$ `RTR` $\to$ `RTS`).
* **Pitfalls to Avoid**:
  * **Bitmask Mismatch**: Setting an attribute in `attr` but forgetting to set the corresponding flag in `attr_mask` causes the driver to ignore that field. Setting a flag in `attr_mask` without providing the attribute causes `EINVAL`.
  * **RoCEv2 GID Index Trap**: Setting `ah_attr.grh.sgid_index = 0` (IPv6 link-local) instead of `1` (IPv4) on RoCEv2 networks causes connection timeouts during the RTR transition.
  * **Skipping States**: Transitioning directly from `RESET` to `RTR` or `RTS` fails immediately with `EINVAL`.
* **Best Practices**:
  * Encapsulate transitions into distinct, type-safe member functions (`transition_to_init()`, `transition_to_rtr()`, `transition_to_rts()`) as designed in `allreduce/verbs_qp.cc`.

---

## 7. Data Path Work Requests

### `ibv_post_send()`
```c
int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);
```
* **Purpose**: Submits a linked list of work requests to the Send Queue.
* **Pitfalls to Avoid**:
  * **Unterminated Linked List**: Forgetting `wr.next = nullptr;`. If `wr.next` contains garbage, the HCA tries to execute random memory as work requests and segfaults.
  * **Buffer Reuse with Non-Inline Sends**: If `IBV_SEND_INLINE` is **not** set, the buffer is read asynchronously via PCIe DMA. Overwriting the local buffer *before* polling the CQE corrupts the in-flight data on the wire.
  * **Misinterpreting Return Codes**:
    * If `ibv_post_send()` returns non-zero, the request was **rejected immediately** and **no CQE will ever be generated**.
    * If `ibv_post_send()` returns 0, the request was accepted; failure will be reported later via `ibv_poll_cq()` with a non-zero `wc.status`.
* **Best Practices**:
  * Set `send_flags = IBV_SEND_SIGNALED` on requests you intend to poll.
  * For small buffers ($\le 128\text{ B}$), add `IBV_SEND_INLINE` to eliminate DMA overhead and allow immediate buffer reuse.

---

### `ibv_post_recv()`
```c
int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr);
```
* **Purpose**: Posts pre-allocated receive descriptors to the Receive Queue.
* **Pitfalls to Avoid**:
  * **Posting After Send Arrives (RNR Death Spiral)**: If an incoming send arrives before `ibv_post_recv()` has been called, the connection drops packets or fails with `IBV_WC_RNR_RETRY_EXC_ERR`.
  * **UD 40-Byte GRH Truncation**: For Unreliable Datagrams (UD), the receive buffer **must be sized to `message_size + 40`**, and the payload is read starting at offset `buf + 40`.
* **Best Practices**:
  * Pre-post a deep queue of receive buffers (e.g. 16 to 64 buffers) before initiating the first send.
  * Use credit loops (`shared/ring_buffer.h`) so senders only transmit when receivers have confirmed credits.

---

## 8. Address Handles (UD Transport)

### `ibv_create_ah()` & `ibv_destroy_ah()`
```c
struct ibv_ah *ibv_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr);
int ibv_destroy_ah(struct ibv_ah *ah);
```
* **Purpose**: Creates an Address Handle encapsulating the destination routing information for Unreliable Datagram sends.
* **Pitfalls to Avoid**:
  * **Destroying PD before AH**: Like Memory Regions, destroying the Protection Domain while an Address Handle is active triggers `EBUSY`.
  * **Creating AH on the Hot Path**: Calling `ibv_create_ah()` on every individual packet send adds massive latency.
* **Best Practices**:
  * Create the `ibv_ah` once during connection initialization and reuse it across all sends to that destination.
  * Track active AHs and destroy them prior to `ibv_dealloc_pd()`.

---

## 9. POSIX & Network Systems Primitives

### `pthread_setaffinity_np()`
```c
int pthread_setaffinity_np(pthread_t thread, size_t cpusetsize, const cpu_set_t *cpuset);
```
* **Purpose**: Pins a thread to specific logical CPU cores.
* **Pitfalls to Avoid**:
  * **Late `_GNU_SOURCE`**: If `#define _GNU_SOURCE` is not defined as line 1 of the file before any `#include`, `pthread_setaffinity_np` prototype is suppressed under strict ISO C/C++ modes.
  * **Ignoring SMT Siblings / Hybrid Cores**: Pinning two competing high-throughput polling threads to sibling hyperthreads (e.g. CPU 0 and CPU 1 on a P-core) cuts throughput in half.
* **Best Practices**:
  * Always use Adder's `CpuTopology::get_homogeneous_cores()` to pin exclusively to non-SMT homogeneous cores sharing an L2 cache cluster.

---

### Socket Streaming: `read_exact()` & `write_exact()`
* **Pitfall**: Raw `read(sock, buf, len)` and `write(sock, buf, len)` over TCP are **byte-stream operations**. A single `read()` can return partial bytes (e.g. 16 bytes of a 48-byte QP exchange struct), corrupting QP parameters and causing silent connection hangs.
* **Best Practice**:
  * Always wrap socket IO in exact streaming loops:
    ```cpp
    inline bool read_exact(int fd, void* buf, size_t len) {
        char* p = static_cast<char*>(buf);
        while (len > 0) {
            ssize_t n = read(fd, p, len);
            if (n <= 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += n;
            len -= static_cast<size_t>(n);
        }
        return true;
    }
    ```

---

### `clock_gettime(CLOCK_MONOTONIC_RAW)`
```c
int clock_gettime(clockid_t clk_id, struct timespec *tp);
```
* **Purpose**: Reads hardware nanosecond timestamps for microsecond-scale interval timing.
* **Pitfalls to Avoid**:
  * **Using `CLOCK_REALTIME`**: Skews or jumps backward when NTP adjusts the system clock.
  * **Calling in Inner Hot Loop without Reason**: Although fast ($\sim 15\text{ ns}$ via vDSO), avoid calling it multiple times within single-digit-nanosecond vector math loops.
* **Best Practices**:
  * Use `CLOCK_MONOTONIC_RAW` as wrapped in `adder::shared::NanosecondClock::now_ns()` for unskewed, monotonic latency calculations.
