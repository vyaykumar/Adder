# Man 06: In-Network Aggregation via eBPF/XDP & C23

## 1. The SwitchML Paradigm: Computing Inside the Network

In classical distributed collectives (such as Ring All-Reduce), every compute node acts as both a sender, a receiver, and a reduction math engine. Over an $N$-node ring, each node transmits:

$$\text{Data Transmitted} = 2 \left(\frac{N - 1}{N}\right) S \approx 2S\text{ bytes}$$

**The In-Network Aggregation Breakthrough (SwitchML)**:
If the programmable network switch (or a smart middlebox/edge host) performs the vector addition directly on packets passing through it:
* Each node transmits its gradients **once** to the switch ($S$ bytes).
* The switch sums the gradients in-flight.
* The switch broadcasts the aggregated result back to all nodes ($S$ bytes).
* **Traffic Reduction**: Egress data movement per node drops from $\sim 2S$ to exactly $S$ (**50% reduction in network traffic!**).

```
Ring All-Reduce (Host-Mediated):
Node 0 ──(2S)──► Node 1 ──(2S)──► Node 2 ──(2S)──► Node 3

In-Network Aggregation (SwitchML):
Node 0 ──(S)──► ┌───────────────────────────┐ ◄──(S)── Node 2
                │ Network Switch / XDP Core │
Node 1 ──(S)──► │ [ Vector Accumulator Map ]│ ◄──(S)── Node 3
                └─────────────┬─────────────┘
                              │
                    Broadcast (S Bytes)
                              ▼
                        All Nodes
```

---

## 2. Architecture of eBPF and XDP (eXpress Data Path)

Deliverable D4 emulates a programmable network switch using Linux **eBPF/XDP**:

```
[ Physical Wire / veth ]
           │
           ▼
   [ Network Driver Ingress ]
           │
           ▼  <── XDP Hook (Executes xdp_agg.bpf.c HERE)
   ┌────────────────────────────────────────────────────────┐
   │ 1. Parse UDP Header (Port 9999)                        │
   │ 2. Read Chunk ID & Fixed-Point Gradient Payload        │
   │ 3. Atomic In-Place Addition in BPF Array Map           │
   │ 4. If all N nodes arrived: XDP_TX / Forward Reduced    │
   └────────────────────────────────────────────────────────┘
           │
           ├── XDP_DROP (Drop packet)
           ├── XDP_TX (Bounce back out same interface)
           └── XDP_PASS (Hand packet to standard Linux sk_buff stack)
```

* **XDP Execution**: Runs directly inside the network driver before the Linux kernel allocates an `sk_buff` or invokes the TCP/IP stack, achieving single-digit microsecond packet processing.

---

## 3. The eBPF Kernel Verifier Constraints

The Linux kernel verifier analyzes eBPF bytecode statically to guarantee memory safety and system stability. It enforces severe constraints:

1. **No Floating-Point Support**: The eBPF virtual machine possesses no floating-point registers or math instructions (`float` or `double` will be rejected at compile time).
2. **512-Byte Stack Limit**: Allocating large arrays on the stack triggers verifier rejection. All buffers must reside in **BPF Maps**.
3. **Mandatory Packet Bounds Verification**: Every memory access into the packet buffer must be mathematically proven to lie between `data` and `data_end`:
   ```c
   void *data_end = (void *)(long)ctx->data_end;
   void *data = (void *)(long)ctx->data;

   struct ethhdr *eth = data;
   if ((void *)(eth + 1) > data_end) {
       return XDP_PASS; // Bounds check required before dereference
   }
   ```
4. **Bounded Loops**: Unbounded `while` loops are rejected. All iteration over gradient elements must be explicitly unrolled via `#pragma unroll`.

---

## 4. 32-Bit Fixed-Point Gradient Quantization

To perform gradient reduction inside eBPF without floating-point arithmetic, floating-point numbers are converted into **32-bit signed fixed-point integers**:

### 4.1 Scaling and Quantization Formula
Let $F$ be the number of fractional bits (typically $F = 16$). For a float value $x$:

$$\text{FixedPoint}(x) = \left\lfloor x \cdot 2^F \right\rfloor$$
$$\text{Float}(y) = \frac{y}{2^F}$$

### 4.2 Dynamic Scale Selection to Prevent Overflow
For $N$ nodes accumulating into a 32-bit signed integer:
$$N \times \max(|x|) \cdot 2^F < 2^{31} - 1$$

---

## 5. C23 Overflow Detection (`<stdckdint.h>`)

In modern **ISO C23**, integer overflow checking is standardized through compiler built-in primitives in `<stdckdint.h>`.

In the userspace fallback aggregator (`innet_agg/agg_userspace.c`), gradient accumulation tracks numerical overflows using `ckd_add`:

```c
#define _GNU_SOURCE
#include <stdckdint.h>
#include <stdint.h>

void accumulate_gradients(int32_t* accum, const int32_t* payload, size_t count, uint64_t* overflow_counter) {
    for (size_t i = 0; i < count; ++i) {
        int32_t result = 0;
        // ckd_add returns true if signed overflow occurred
        if (ckd_add(&result, accum[i], payload[i])) {
            (*overflow_counter)++;
        }
        accum[i] = result;
    }
}
```

---

## 6. Packet Replication in All-Gather (TC Hook / TCX)

Once the switch has received all $N$ chunks and finished vector reduction, it must broadcast the result back to all participating worker nodes:
* Standard XDP (`XDP_TX`) can only reflect a packet back to the sender.
* To replicate a packet to multiple destination MAC/IP addresses, Adder attaches a Traffic Control (TC) hook using `bpf_clone_redirect` or modern Linux 6.6+ **TCX** (`SEC("tcx/ingress")` / `SEC("tcx/egress")`).

---

## 7. C23 Portable Userspace Fallback (`innet_agg/agg_userspace.c`)

If the host environment lacks eBPF support or the kernel verifier rejects attachments:
* `agg_userspace.c` runs on a dedicated pinned CPU core.
* Listens on UDP port `9999` using standard socket APIs.
* Uses the exact same packet header format (`struct agg_hdr`) and fixed-point math as the eBPF program.
* Enables complete baseline benchmarking and quantisation error analysis on any Linux or cloud instance.
