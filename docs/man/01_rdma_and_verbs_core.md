# Man 01: RDMA Architecture, Kernel-Bypass, & Verbs Core

## 1. The Fundamental Mental Model: Sockets vs. RDMA

### 1.1 The Classical Linux Socket Data Path
In traditional TCP/IP networking, sending and receiving data requires heavy operating system mediation:

```
[ Application Buffer (User Space) ]
               │
      (1) write() Syscall (Context Switch to Ring 0)
               ▼
   [ Kernel Socket Buffer: sk_buff ]  <── Memory Copy #1 (CPU Cache Pollution)
               │
      (2) TCP/IP Protocol Stack (Checksum, TCP State Machine, Window Tracking)
               ▼
   [ Network Device Driver Queue ]    <── Memory Copy #2 (Driver Ring Buffer)
               │
      (3) PCIe DMA Transfer to NIC
               ▼
        [ Physical Wire ]
```

**Bottlenecks of the Socket Stack**:
1. **Context Switches**: Crossing the user/kernel boundary on every system call costs hundreds of nanoseconds.
2. **Buffer Copies**: Data is copied from user memory to kernel `sk_buff` structures, polluting CPU L1/L2 caches.
3. **Interrupt Handling**: On receive, hardware interrupts and SoftIRQs (`ksoftirqd`) disrupt CPU execution.
4. **Serialization and Protocol Overhead**: TCP window updates, acknowledgments, and timer interrupts consume CPU cycles.

---

### 1.2 The RDMA Kernel-Bypass Data Path
Remote Direct Memory Access (RDMA) bypasses the operating system kernel entirely during data transfer:

```
[ Pre-Registered User Memory Region ] ◄──────────────┐
               │                                      │ True Zero-Copy
      (1) Post Work Request to User Queue Pair        │ Direct PCIe DMA
               │                                      │ (Bypasses Kernel)
      (2) Write to Doorbell Register (MMIO)           │
               ▼                                      │
    [ Hardware RDMA NIC (HCA) ] ──────────────────────┘
               │
      (3) Packet Serialization over RoCEv2 / InfiniBand
               ▼
        [ Physical Wire ]
```

**The 3 Tenets of RDMA Performance**:
1. **Kernel Bypass**: The application posts Work Requests (WRs) and polls Work Completions (WCs) directly against memory-mapped registers (doorbells) allocated by the hardware.
2. **Zero-Copy**: The network interface card (NIC) reads directly from and writes directly to user-space virtual addresses via PCIe Direct Memory Access (DMA).
3. **CPU Offload**: The NIC silicon handles reliable transport sequencing, packet retransmission, flow control, and CRC validation without CPU intervention.

---

### 1.3 What is Soft-RoCE (`rdma_rxe`)?
Soft-RoCE is a Linux kernel software driver that implements the RoCEv2 (RDMA over Converged Ethernet) protocol entirely in software:
* **In Hardware RDMA (e.g. Mellanox ConnectX)**: The Queue Pair queues, state machines, packet retransmissions, and DMA engines reside inside the NIC ASIC.
* **In Soft-RoCE (`rdma_rxe`)**: The Queue Pairs and state machines run in kernel worker threads. When an application posts a send, the `rxe` kernel driver formats UDP packets targeting destination port `4791` and hands them to the regular Linux network stack.
* **Why Soft-RoCE is valuable**: It exposes the **exact same `libibverbs` C API** and enforces the exact same state machine rules, memory keys, and queue semantics as a \$2,000 enterprise NIC, allowing complete local development on any standard PC or VM.

---

## 2. The Verbs Object Hierarchy

`libibverbs` models hardware resources in a strict tree hierarchy:

```
                  [ ibv_device ] (Physical / Virtual RDMA Adapter)
                        │
                  ibv_open_device()
                        ▼
                 [ ibv_context ] (Open Hardware Context)
                   ┌────┴──────────────────────────┐
          ibv_alloc_pd()                    ibv_create_cq()
                   ▼                               ▼
         [ ibv_pd (Protection Domain) ]    [ ibv_cq (Completion Queue) ]
          ┌────────┴──────────────┐                ▲
    ibv_reg_mr()             ibv_create_qp()       │
          ▼                       ▼                │
   [ ibv_mr (Memory) ]      [ ibv_qp (Queue Pair) ]┘
   - Local Key (lkey)       - Send Queue (SQ)
   - Remote Key (rkey)      - Receive Queue (RQ)
```

### 2.1 Protection Domain (`ibv_pd`)
A Protection Domain acts as a memory isolation container. Queue Pairs can only access Memory Regions (`ibv_mr`) that were registered within the **same** Protection Domain. This prevents rogue QPs from reading or writing unauthorized memory.

### 2.2 Memory Region (`ibv_mr`)
Before RDMA hardware can access a memory buffer:
1. Physical pages must be **pinned** (locked in RAM via `mlock()`) so the Linux kernel virtual memory manager cannot swap them out to disk or move them.
2. The virtual-to-physical address translation table is programmed into the NIC's Internal Translation Table (MTT).
3. Two memory keys are generated:
   * **`lkey` (Local Key)**: Used by the local HCA to authenticate local buffer access for Send/Recv.
   * **`rkey` (Remote Key)**: Shared out-of-band with remote nodes to authorize one-sided RDMA Read/Write operations into this region.

### 2.3 Completion Queue (`ibv_cq`)
A FIFO queue where the RDMA device writes **Work Completions (`ibv_wc`)** when Work Requests finish:
* Can service Send Queues, Receive Queues, or both across multiple Queue Pairs.
* Capacity must be sized to accommodate the maximum in-flight requests across all attached QPs.

### 2.4 Queue Pair (`ibv_qp`)
The communication endpoint. Every QP consists of:
* **Send Queue (SQ)**: Holds outbound work requests (Send, RDMA Write, RDMA Read, Atomic).
* **Receive Queue (RQ)**: Holds pre-posted receive buffers ready to capture incoming two-sided Send packets.

---

## 3. Completion Processing: Spin-Polling vs. Event Channels

### 3.1 Spin-Polling (`ibv_poll_cq`) — Sub-Microsecond Hot Path
In latency-critical loops, spin-polling queries the CQ register directly:

```cpp
struct ibv_wc wc[16];
int n = ibv_poll_cq(cq, 16, wc);
if (n > 0) {
    for (int i = 0; i < n; ++i) {
        if (wc[i].status != IBV_WC_SUCCESS) {
            // Handle error
        }
        // Process completion
    }
} else if (n < 0) {
    // Driver failure
}
```

* **Advantage**: Lowest possible latency (no context switch, zero interrupt overhead).
* **Cost**: 100% CPU utilization on the pinned polling core. Use `_mm_pause()` (in `shared/busy_poll.h`) to prevent CPU pipeline speculation thrashing.

### 3.2 Event-Driven Completion (`ibv_comp_channel`) — Low CPU Overhead
When CPU conservation is required:
1. Allocate an event channel: `ibv_create_comp_channel(ctx)`.
2. Attach it during CQ creation: `ibv_create_cq(ctx, depth, ctx, channel, 0)`.
3. Request notification: `ibv_req_notify_cq(cq, 0)`.
4. Wait for event: `ibv_get_cq_event(channel, &ev_cq, &ev_ctx)` (blocks in kernel).
5. Acknowledge event: `ibv_ack_cq_events(ev_cq, 1)`.
6. Poll completions using `ibv_poll_cq()`.

---

## 4. Core Verbs API Quick Reference

| Operation | Function Signature | Description |
|---|---|---|
| **Enumerate Devices** | `struct ibv_device** ibv_get_device_list(int *num_devices)` | Returns array of available RDMA devices. |
| **Open Context** | `struct ibv_context* ibv_open_device(struct ibv_device *device)` | Opens device and allocates user-space context. |
| **Query Port** | `int ibv_query_port(struct ibv_context *ctx, uint8_t port, struct ibv_port_attr *attr)` | Queries active MTU, port state (`PORT_ACTIVE`), GID count. |
| **Allocate PD** | `struct ibv_pd* ibv_alloc_pd(struct ibv_context *context)` | Creates a protection domain. |
| **Register Memory** | `struct ibv_mr* ibv_reg_mr(struct ibv_pd *pd, void *addr, size_t length, int access)` | Pins memory and returns `lkey`/`rkey`. |
| **Create CQ** | `struct ibv_cq* ibv_create_cq(struct ibv_context *ctx, int cqe, void *cq_context, struct ibv_comp_channel *channel, int comp_vector)` | Creates completion queue. |
| **Create QP** | `struct ibv_qp* ibv_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *qp_init_attr)` | Allocates Queue Pair. |
| **Modify QP** | `int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr, int attr_mask)` | Transitions QP state (`RESET` $\to$ `INIT` $\to$ `RTR` $\to$ `RTS`). |
| **Post Send** | `int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr)` | Submits chained list of send work requests. |
| **Post Recv** | `int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr)` | Posts buffers to receive queue. |
| **Poll CQ** | `int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc)` | Retrieves completed operations from CQ. |
| **Deregister MR** | `int ibv_dereg_mr(struct ibv_mr *mr)` | Unpins and frees memory region. |
| **Destroy QP** | `int ibv_destroy_qp(struct ibv_qp *qp)` | Destroys Queue Pair. |
| **Destroy CQ** | `int ibv_destroy_cq(struct ibv_cq *cq)` | Destroys Completion Queue. |
| **Deallocate PD** | `int ibv_dealloc_pd(struct ibv_pd *pd)` | Destroys Protection Domain (fails with `EBUSY` if MRs/AHs remain). |
| **Close Device** | `int ibv_close_device(struct ibv_context *context)` | Closes RDMA device context. |
