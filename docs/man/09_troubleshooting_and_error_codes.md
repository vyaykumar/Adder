# Man 09: Troubleshooting, Diagnostic Traps, & Error Codes

## 1. Work Completion Status Dictionary (`enum ibv_wc_status`)

When polling a Completion Queue via `ibv_poll_cq()`, any completion where `wc.status != IBV_WC_SUCCESS` indicates a failure on the data path. Below is the diagnostic guide for every verbs error:

---

### `IBV_WC_LOC_LEN_ERR` (Local Length Error)
* **What it means**: The local buffer provided in the Scatter/Gather Element (SGE) was smaller than the incoming packet or exceeded the maximum scatter/gather capability.
* **Primary Root Cause (The 40-Byte UD Bug)**:
  * On an **Unreliable Datagram (UD)** receive, the HCA automatically prepends a 40-byte Global Routing Header (GRH).
  * If the sender transmits 1024 bytes and the receiver only registers a 1024-byte buffer, the HCA tries to write $1024 + 40 = 1064$ bytes. The buffer overflows, and the HCA immediately throws `IBV_WC_LOC_LEN_ERR`.
* **Fix**:
  * Always allocate and post `message_size + 40` bytes for UD receives:
    ```cpp
    sge.length = message_size + 40;
    ```
  * Verify that `message_size <= mtu_to_bytes(port_attr.active_mtu)`.

---

### `IBV_WC_RNR_RETRY_EXC_ERR` (Receiver Not Ready Retry Exceeded)
* **What it means**: The sender transmitted an `IBV_WR_SEND` packet, but the receiving Queue Pair had **no matching receive buffer posted** in its Receive Queue. The receiver sent an RNR NAK; the sender retried `rnr_retry` times and timed out.
* **The "RNR Death Spiral"**:
  * In RDMA, two-sided sends **do not wait** for the receiver.
  * If Node A posts a send before Node B has called `ibv_post_recv()`, the connection enters hardware backoff timers (`min_rnr_timer`), destroying $P_{99.9}$ tail latency before fatal timeout.
* **Fix**:
  1. Always pre-post receive buffers **before** signaling the sender to transmit.
  2. Implement an explicit credit loop (`shared/ring_buffer.h`): Node A only sends chunk $k$ if it holds an available credit token proving Node B has already posted receive buffer $k$.
  3. Set `rnr_retry = 7` (infinite retry) in `IBV_QPS_RTS` during development to prevent QP teardown while debugging.

---

### `IBV_WC_REM_ACCESS_ERR` (Remote Access Error)
* **What it means**: A one-sided operation (`IBV_WR_RDMA_WRITE` or `RDMA_READ`) failed on the remote node.
* **Root Causes**:
  1. **Missing Access Flag**: The remote memory region was registered without `IBV_ACCESS_REMOTE_WRITE` or `IBV_ACCESS_REMOTE_READ`.
  2. **Wrong `rkey`**: The sender passed a stale or mismatched `rkey` in `wr.wr.rdma.rkey`.
  3. **Address Out of Bounds**: The requested `remote_addr + length` exceeds the remote registered memory boundary.

---

### `IBV_WC_RETRY_EXC_ERR` (Transport Retry Counter Exceeded)
* **What it means**: On an RC connection, the sender transmitted a packet but never received a hardware ACK/NAK from the peer within the timeout period. Retried `retry_cnt` times and aborted.
* **Root Causes**:
  1. **Remote QP Not in RTS**: The remote Queue Pair was not transitioned to `IBV_QPS_RTS` before sends arrived.
  2. **GID Index Mismatch**: The remote destination GID in `ah_attr.grh.dgid` was wrong or routed to an inactive link-local address.
  3. **Packet Sequence Number (PSN) Mismatch**: The sender's `sq_psn` does not match the receiver's `rq_psn` configured during the RTR transition.
  4. **Firewall / Network Drop**: UDP port `4791` (RoCEv2) is blocked by an Azure Network Security Group (NSG) or Linux `iptables`.

---

### `IBV_WC_WR_FLUSH_ERR` (Work Request Flushed Error)
* **What it means**: The work request was aborted and flushed out of the queue without being executed.
* **Root Cause**: A **previous** work request on the same Queue Pair failed. Once any work request encounters an error, the QP moves automatically to the `IBV_QPS_ERR` state and flushes all subsequent pending requests.

---

### `IBV_WC_LOC_PROT_ERR` (Local Protection Error)
* **What it means**: The local HCA could not access the local memory buffer.
* **Root Causes**:
  1. Buffer pointer does not match the `lkey` specified in the SGE.
  2. The buffer was registered without `IBV_ACCESS_LOCAL_WRITE` on a receive or read operation.
  3. Memory region was deregistered while requests were still in-flight.

---

## 2. Common System Call & Configuration Errors

### 1. `ENOMEM` (Cannot allocate memory) on `ibv_reg_mr()`
* **Symptom**: Calling `ibv_reg_mr()` fails with `errno = 12` (`ENOMEM`), even though the host has gigabytes of free RAM.
* **Root Cause**: Linux `RLIMIT_MEMLOCK` limit exceeded. `ibv_reg_mr()` pins physical pages in RAM via `ib_umem_get()`. The OS default is often 64 KB.
* **Fix**:
  ```bash
  ulimit -l unlimited
  ```
  Add to `/etc/security/limits.conf`:
  ```text
  * soft memlock unlimited
  * hard memlock unlimited
  ```

---

### 2. `EBUSY` (Device or resource busy) on `ibv_dealloc_pd()`
* **Symptom**: Calling `ibv_dealloc_pd()` fails on application shutdown.
* **Root Cause**: An active Memory Region (`ibv_mr`), Address Handle (`ibv_ah`), or Queue Pair (`ibv_qp`) associated with the Protection Domain was not destroyed first.
* **Fix**: Enforce strict RAII destruction order:
  $$\text{Queue Pairs} \longrightarrow \text{Completion Queues} \longrightarrow \text{MRs / AHs} \longrightarrow \text{Protection Domain} \longrightarrow \text{Device Context}$$

---

### 3. `EINVAL` (Invalid argument) on `ibv_modify_qp()`
* **Symptom**: `ibv_modify_qp()` returns `-1` with `errno = 22`.
* **Root Cause**: The bitmask (`attr_mask`) does not match the exact set of mandatory attributes required for that specific state transition.
* **Fix**: Review the exact transition mask table in [02_queue_pairs_and_transports.md](file:///d:/Code/MultiArch/Adder/docs/man/02_queue_pairs_and_transports.md).

---

## 3. Soft-RoCE Runtime Quirks

### 3.1 The `rxe_loopback` Bypass Trap
When running both client and server on the same Soft-RoCE device (`rxe0`) in the default network namespace:
* The kernel driver notices the destination IP matches the local host.
* It short-circuits the packet inside `rxe_loopback()` and **bypasses the Linux network queue entirely**.
* **Impact**: `tcpdump` observes 0 packets, and `tc qdisc netem delay` has zero effect on latency!
* **Solution**: Run client and server inside **two isolated network namespaces** connected via a virtual Ethernet (`veth`) pair.

### 3.2 GID Table Index Selection (IPv4 RoCEv2)
* Query GID attributes:
  ```bash
  cat /sys/class/infiniband/rxe0/ports/1/gids/1
  ```
* RoCEv2 over standard IPv4 requires GID Index 1 (`::ffff:192.168.x.x`). Using Index 0 (IPv6 link-local) results in silent connection timeouts.
