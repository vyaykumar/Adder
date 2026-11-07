# Man 02: Queue Pairs, State Machines, & Transports

## 1. Transport Types: RC vs. UD

`libibverbs` supports four primary transport types:

| Feature | Reliable Connection (RC) | Unreliable Datagram (UD) | Unreliable Connected (UC) | Extended Reliable (XRC) |
|---|---|---|---|---|
| **Connection Model** | Point-to-Point (1-to-1) | Multipoint (1-to-Many) | Point-to-Point (1-to-1) | Many-to-Many Shared |
| **Hardware Reliability** | Yes (ACKs, hardware retransmission) | No (drops on congestion) | No | Yes |
| **Packet Ordering** | Guaranteed in-order | Out-of-order possible | Guaranteed in-order | Guaranteed in-order |
| **Max Message Size** | Up to $2\text{ GB}$ (hardware fragments) | **Clamped to 1 Active MTU** | Up to $2\text{ GB}$ | Up to $2\text{ GB}$ |
| **Supported Operations** | Send/Recv, Write, Read, Atomics | **Send/Recv only** | Send/Recv, Write | All RC operations |
| **Memory Keys** | `lkey` and `rkey` | `lkey` only | `lkey` and `rkey` | `lkey` and `rkey` |
| **Header Overhead** | Minimal | **+40 bytes GRH on receive** | Minimal | Minimal |

---

## 2. The QP State Machine

A Queue Pair starts in `RESET` and must be transitioned sequentially through `INIT`, `RTR` (Ready to Receive), and `RTS` (Ready to Send) before any network traffic can be transmitted.

```
       ┌───────────┐
       │   RESET   │ (Initial State after ibv_create_qp)
       └─────┬─────┘
             │ ibv_modify_qp(IBV_QPS_INIT)
             ▼
       ┌───────────┐
       │   INIT    │ (Port assigned, access flags set, Q_Key set if UD)
       └─────┬─────┘
             │ ibv_modify_qp(IBV_QPS_RTR)
             ▼
       ┌───────────┐
       │    RTR    │ (Remote QP info bound, path MTU set, receiver active)
       └─────┬─────┘
             │ ibv_modify_qp(IBV_QPS_RTS)
             ▼
       ┌───────────┐
       │    RTS    │ (Ready to Send: timeouts, retry counts, SQ PSN set)
       └───────────┘
```

---

### 2.1 State Transitions for Reliable Connection (RC)

#### Step 1: RESET $\to$ INIT
```cpp
struct ibv_qp_attr attr{};
attr.qp_state        = IBV_QPS_INIT;
attr.port_num        = ib_port;
attr.pkey_index      = 0;
attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                       IBV_ACCESS_REMOTE_WRITE |
                       IBV_ACCESS_REMOTE_READ;

int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
ibv_modify_qp(qp, &attr, mask);
```

#### Step 2: INIT $\to$ RTR (Ready to Receive)
*Requires remote QP parameters (QPN, LID/GID, PSN) received over the TCP side-channel.*
```cpp
struct ibv_qp_attr attr{};
attr.qp_state           = IBV_QPS_RTR;
attr.path_mtu           = IBV_MTU_1024;       // Must match port active MTU
attr.dest_qp_num        = remote_info.qpn;    // Peer QP Number
attr.rq_psn             = remote_info.psn;    // Peer initial Packet Sequence Number
attr.max_dest_rd_atomic = 1;                  // Max in-flight responder resources
attr.min_rnr_timer      = 12;                 // ~0.65ms RNR retry timer

// Address Handle for RoCEv2 (Global Routing Header)
attr.ah_attr.is_global      = 1;
attr.ah_attr.port_num       = ib_port;
attr.ah_attr.sl             = 0;              // Service Level
attr.ah_attr.src_path_bits  = 0;
attr.ah_attr.dlid           = remote_info.lid;
attr.ah_attr.grh.hop_limit  = 64;
attr.ah_attr.grh.dgid       = remote_info.gid;
attr.ah_attr.grh.sgid_index = 1;              // RoCEv2 / IPv4 mapping index

int mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
           IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
ibv_modify_qp(qp, &attr, mask);
```

#### Step 3: RTR $\to$ RTS (Ready to Send)
```cpp
struct ibv_qp_attr attr{};
attr.qp_state       = IBV_QPS_RTS;
attr.timeout        = 14;                     // 4.096us * 2^14 = ~67ms
attr.retry_cnt      = 7;                      // Retries before error
attr.rnr_retry      = 7;                      // RNR retries (7 = infinite retry)
attr.sq_psn         = local_send_psn;         // Local initial Packet Sequence Number
attr.max_rd_atomic  = 1;                      // Max in-flight requester resources

int mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
           IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
ibv_modify_qp(qp, &attr, mask);
```

---

### 2.2 State Transitions for Unreliable Datagram (UD)

UD transitions differ fundamentally from RC because UD is connectionless:
* **In INIT**: You must set `attr.qkey = 0x11111111`.
* **In RTR**: **No destination attributes are provided!** You only transition the state: `attr.qp_state = IBV_QPS_RTR` with mask `IBV_QP_STATE`.
* **In RTS**: You only provide `attr.sq_psn = local_send_psn` with mask `IBV_QP_STATE | IBV_QP_SQ_PSN`. (No timeouts or retry counts exist for unreliable datagrams).

---

## 3. Communication Paradigms: Two-Sided vs. One-Sided

### 3.1 Two-Sided (`IBV_WR_SEND` / `RECV`)
* **Behavior**: The sender posts an `IBV_WR_SEND`. The receiver **must** have already posted an `ibv_post_recv()` buffer of sufficient size.
* **CPU Involvement**: Both sides generate a Completion Queue Entry (CQE). The receiving CPU must actively poll its CQ to learn that data has arrived.
* **Danger**: If the send arrives before the receive buffer is posted, the HCA issues an **RNR NAK (Receiver Not Ready)**.

### 3.2 One-Sided (`IBV_WR_RDMA_WRITE` & `RDMA_READ`)
* **Behavior**: The sender specifies the remote virtual memory address (`wr.wr.rdma.remote_addr`) and remote memory key (`wr.wr.rdma.rkey`).
* **True Zero-CPU on Remote Side**: The remote NIC writes incoming data directly into host RAM via PCIe DMA without waking up or notifying the remote CPU.
* **The Notification Problem**: Because standard `RDMA_WRITE` generates **no CQE on the remote receiver**, how does the remote side know data transfer is finished?
  * *Option A*: Remote polls memory directly (cache-unfriendly).
  * *Option B*: **`IBV_WR_RDMA_WRITE_WITH_IMM`** (Best Practice): Writes data via zero-copy DMA *and* consumes a pre-posted receive descriptor on the receiver, placing a 32-bit immediate value into `wc.imm_data` to trigger a remote CQ event!

---

## 4. `IBV_SEND_INLINE` Optimization

When sending small payloads ($\le 128\text{ B}$ or $\le 256\text{ B}$), standard RDMA requires two PCIe transactions:
1. Driver rings the doorbell over PCIe MMIO.
2. The NIC initiates a PCIe DMA Read transaction to fetch the payload from host RAM.

```
Standard Send:  [CPU] ──(1) Doorbell Ring──> [NIC] ──(2) PCIe DMA Read──> [RAM]
Inline Send:    [CPU] ──(1) Doorbell + Data embedded in MMIO Register──> [NIC]
```

**Using Inline Sends**:
* Pass flag `send_wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE`.
* Slashes small-message latency by eliminating PCIe DMA round-trips.
* The buffer does **not** need to remain valid after `ibv_post_send()` returns.
* Limited by `cap.max_inline_data` queried during `ibv_create_qp()`.

---

## 5. Unreliable Datagram (UD) Traps & Mechanics

### 5.1 The 40-Byte Global Routing Header (GRH) Trap
On Unreliable Datagram receives, the hardware automatically writes a **40-byte Global Routing Header (GRH)** into the first 40 bytes of the receive buffer:

```
Receive Buffer Allocated (MTU + 40 Bytes):
┌────────────────────────┬───────────────────────────────────────────┐
│  40 Bytes GRH Header   │        Actual Application Payload         │
│  (Written by HCA)      │        (Starts at buffer_base + 40)       │
└────────────────────────┴───────────────────────────────────────────┘
▲                        ▲
│ buf                    │ buf + 40
```

> [!CAUTION]
> If you allocate an $N$-byte buffer for an $N$-byte UD message:
> - The HCA attempts to write $40 + N$ bytes.
> - The buffer overflows, and the work completion fails with **`IBV_WC_LOC_LEN_ERR` (Local Length Error)**!
>
> **The Golden UD Rules**:
> 1. Buffer Allocation: `malloc(msg_size + 40)`
> 2. Receive SGE Length: `sge.length = msg_size + 40`
> 3. Receive Payload Access: Read from `buf + 40`
> 4. Send Payload: Post exactly `msg_size` (no GRH offset on the send side; the HCA adds the GRH on the wire).

### 5.2 Active MTU Clamping
Unlike RC (which fragments messages up to 2GB in hardware), a UD message **cannot exceed one active MTU**. Attempting to send 2048 bytes on an active MTU of 1024 bytes fails immediately. Always clamp UD message sizes to `mtu_to_bytes(port_attr.active_mtu)`.

### 5.3 GID Index Selection (Soft-RoCE)
On Soft-RoCE (`rxe0`), GID table indices are partitioned:
* **Index 0**: IPv6 link-local address (e.g. `fe80::...`).
* **Index 1**: **IPv4-Mapped IPv6 address** (`::ffff:192.168.x.x`).
* **Trap**: Selecting GID index 0 on an IPv4 network causes silent packet drops or connection timeouts during the RTR transition. Always verify and default to `gid_index = 1`.
