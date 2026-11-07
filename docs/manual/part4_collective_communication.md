# Part 4: Collective Communication

---

### 4.1 The $\alpha$–$\beta$ (Latency–Bandwidth) Cost Model & Crossover Prediction

#### Why it matters here
In Deliverable D2, you implement both a **Ring All-Reduce** and a **Binary Tree All-Reduce**. If you do not understand the analytical $\alpha$–$\beta$ cost model, you cannot explain why NCCL uses rings for large tensor gradients, why tree topologies win for small control messages, or how to predict the exact message-size crossover point before running a single benchmark.

#### Mental Model
Every network transmission incurs two costs:
1. **Startup Overhead ($\alpha$)**: The latency to inject a message into the fabric (software posting, PCIe doorbell, wire propagation, packet headers, interrupt or poll detection). This cost is paid once per message, regardless of whether you send 1 byte or 1 megabyte.
2. **Transfer Overhead ($\beta \cdot S$)**: The time dictated by the link bandwidth: $\beta = \frac{1}{\text{Bandwidth}}$. Moving $S$ bytes takes $\beta \cdot S$ seconds.

```
Time (T)
  ▲                                 Tree All-Reduce: T_tree = 2 log2(N)·α + 2 log2(N)·β·S
  │                               /
  │                             /
  │                           /
  │                         /   Ring All-Reduce: T_ring = 2(N-1)·α + 2((N-1)/N)·β·S
  │                       /   /
  │                     /   /
  │                   /   /
  │                 /   /
  │               /   /
  │  T_ring(0) ──►  X <── Crossover Point (S*)
  │              /  │
  │  T_tree(0) ─►   │
  └─────────────────┴───────────────────────► Message Size (S)
                  Small S                     Large S
               (Tree Wins!)                (Ring Wins!)
```

#### Core Details: Mathematical Derivations

Let $N$ be the number of participating nodes, $S$ be the total message size in bytes, $\alpha$ be point-to-point latency, and $\beta$ be inverse bandwidth ($\text{seconds/byte}$).

#### 1. Ring All-Reduce Analysis
In a ring, the total buffer of size $S$ is split into $N$ equal chunks of size $\frac{S}{N}$.
* **Phase 1: Scatter-Reduce (Reduce-Scatter)**:
  * Each node sends a chunk of size $\frac{S}{N}$ to its successor and receives a chunk from its predecessor.
  * To reduce all chunks across $N$ nodes requires $N - 1$ sequential steps.
  * Time: $T_{\text{scatter}} = (N - 1)\alpha + (N - 1)\beta \left(\frac{S}{N}\right) = (N - 1)\alpha + \left(\frac{N - 1}{N}\right)\beta S$.
* **Phase 2: All-Gather**:
  * Each node sends its fully reduced $\frac{S}{N}$ chunk around the ring so all nodes obtain the full result.
  * This also requires $N - 1$ sequential steps.
  * Time: $T_{\text{gather}} = (N - 1)\alpha + \left(\frac{N - 1}{N}\right)\beta S$.

Adding both phases gives the **Total Ring Time**:
$$T_{\text{ring}}(S, N) = 2(N - 1)\alpha + 2\left(\frac{N - 1}{N}\right)\beta S$$

> [!IMPORTANT]
> **Why Ring is Bandwidth-Optimal**:
> Look at the bandwidth term as cluster size scales:
> $$\lim_{N \to \infty} 2\left(\frac{N - 1}{N}\right)\beta S = 2\beta S$$
> The bandwidth time is completely independent of the number of nodes $N$! Each node transmits exactly $2\frac{N-1}{N}S$ bytes in total. Even if you scale from 4 nodes to 10,000 nodes, each node never transfers more than $2S$ bytes.

#### 2. Binary Tree All-Reduce Analysis
In a standard binary tree all-reduce, chunks are not partitioned. Instead, whole messages (or subtrees) are passed up and down the tree hierarchy of height $\lceil \log_2 N \rceil$.
* **Phase 1: Reduce to Root**: Data flows up the tree across $\lceil \log_2 N \rceil$ levels. In each step, a parent receives size $S$ and reduces it.
* **Phase 2: Broadcast to Leaves**: The root broadcasts the fully reduced buffer of size $S$ back down across $\lceil \log_2 N \rceil$ levels.

The critical path spans $2 \lceil \log_2 N \rceil$ serial steps, each transferring the full message size $S$:
$$T_{\text{tree}}(S, N) = 2 \lceil \log_2 N \rceil \alpha + 2 \lceil \log_2 N \rceil \beta S$$

#### 3. Analytical Derivation of the Crossover Size ($S^*$)
* **Small Messages ($S \to 0$)**: The $\alpha$ (latency) term dominates:
  $$T_{\text{tree}}(0) = 2 \lceil \log_2 N \rceil \alpha \quad \text{vs.} \quad T_{\text{ring}}(0) = 2(N - 1)\alpha$$
  For $N = 4$: Tree pays $2(2)\alpha = 4\alpha$, while Ring pays $2(3)\alpha = 6\alpha$. **Tree wins because it takes fewer network hops.**
* **Large Messages ($S \to \infty$)**: The $\beta$ (bandwidth) term dominates:
  $$\text{Tree Bandwidth Multiplier} = 2 \lceil \log_2 N \rceil \quad \text{vs.} \quad \text{Ring Bandwidth Multiplier} = 2\left(\frac{N - 1}{N}\right)$$
  For $N = 4$: Tree moves $4 \cdot S$ bytes on its critical path; Ring moves $2(3/4)S = 1.5 \cdot S$ bytes. **Ring transfers $2.66\times$ less data, so Ring wins.**

To find the crossover message size $S^*$ where Tree and Ring take identical time, set $T_{\text{ring}}(S^*) = T_{\text{tree}}(S^*)$:
$$2(N - 1)\alpha + 2\left(\frac{N - 1}{N}\right)\beta S^* = 2 \lceil \log_2 N \rceil \alpha + 2 \lceil \log_2 N \rceil \beta S^*$$

Solving for $S^*$:
$$2\left[(N - 1) - \lceil \log_2 N \rceil\right]\alpha = 2\left[\lceil \log_2 N \rceil - \frac{N - 1}{N}\right]\beta S^*$$

$$S^* = \frac{(N - 1) - \lceil \log_2 N \rceil}{\lceil \log_2 N \rceil - \frac{N - 1}{N}} \cdot \frac{\alpha}{\beta}$$

#### Worked Example: Predicting Crossover on Soft-RoCE ($N = 4$)
From your Part 1 and Part 3 baselines:
* Measured Soft-RoCE one-way ping-pong latency: $\alpha \approx 10\ \mu\text{s} = 10 \times 10^{-6}\ \text{s}$.
* Measured Soft-RoCE loopback bandwidth: $B \approx 20\ \text{Gbps} = 2.5 \times 10^9\ \text{bytes/s} \implies \beta = \frac{1}{B} = 0.4 \times 10^{-9}\ \text{s/byte}$.
* For $N = 4$:
  * Numerator: $(4 - 1) - \lceil \log_2 4 \rceil = 3 - 2 = 1$.
  * Denominator: $\lceil \log_2 4 \rceil - \frac{4 - 1}{4} = 2 - 0.75 = 1.25$.
  * Latency-to-bandwidth ratio: $\frac{\alpha}{\beta} = \frac{10 \times 10^{-6}}{0.4 \times 10^{-9}} = 25{,}000\ \text{bytes}$.

$$S^* = \frac{1}{1.25} \times 25{,}000\ \text{bytes} = 20{,}000\ \text{bytes} \approx 20\ \text{KB}$$

**Prediction**: For message sizes below $\approx 20\ \text{KB}$, Tree All-Reduce will be faster. Above $20\ \text{KB}$, Ring All-Reduce will dominate.

---

### 4.2 Step-by-Step Ring All-Reduce Trace ($N = 4$) & The Indexing Traps

#### Why it matters here
In `allreduce/ring.cc`, writing incorrect chunk index formulas results in silent data corruption: nodes overwrite unreduced chunks, add the wrong tensor slices, or crash with buffer overruns.

#### Mental Model: The Ouroboros Ring
Consider 4 nodes arranged in a circle: $0 \to 1 \to 2 \to 3 \to 0$. Each node holds an array of 4 chunks: $[C_0, C_1, C_2, C_3]$.
At each step:
* Every node sends one chunk to $(r + 1) \pmod 4$.
* Every node receives one chunk from $(r - 1 + 4) \pmod 4$.

```
           Node 0 [ C0  C1  C2  C3 ]
          ▲                        │
          │                        ▼
Node 3 [ C0 C1 C2 C3 ]     Node 1 [ C0 C1 C2 C3 ]
          ▲                        │
          │                        ▼
           Node 2 [ C0  C1  C2  C3 ]
```

---

#### Phase 1: Scatter-Reduce ($N - 1 = 3$ Steps)
* Goal: Each node $r$ accumulates the global sum for one chunk: Node $r$ ends up with the fully reduced Chunk $(r + 1) \pmod N$.
* At step $k \in \{0, 1, 2\}$:
  * Send chunk index: $c_{\text{send}} = (r - k + N) \pmod N$
  * Recv chunk index: $c_{\text{recv}} = (r - k - 1 + N) \pmod N$
  * Operation: Receive chunk $c_{\text{recv}}$ from predecessor and **add it in-place** into local buffer chunk $c_{\text{recv}}$.

#### Complete Trace Table for Phase 1 ($N = 4$):

| Step ($k$) | Node ($r$) | Sends Chunk to $(r+1)\%4$ | Receives Chunk from $(r-1+4)\%4$ | Action Performed by Node $r$ |
|---|---|---|---|---|
| **$k = 0$** | **Node 0** | Chunk 0 | Chunk 3 | Local $C_3 \mathrel{+}= \text{Recv } C_3$ (Node 0 now holds $C_3^{(3)} + C_3^{(0)}$) |
| | **Node 1** | Chunk 1 | Chunk 0 | Local $C_0 \mathrel{+}= \text{Recv } C_0$ (Node 1 now holds $C_0^{(0)} + C_0^{(1)}$) |
| | **Node 2** | Chunk 2 | Chunk 1 | Local $C_1 \mathrel{+}= \text{Recv } C_1$ (Node 2 now holds $C_1^{(1)} + C_1^{(2)}$) |
| | **Node 3** | Chunk 3 | Chunk 2 | Local $C_2 \mathrel{+}= \text{Recv } C_2$ (Node 3 now holds $C_2^{(2)} + C_2^{(3)}$) |
| **$k = 1$** | **Node 0** | Chunk 3 | Chunk 2 | Local $C_2 \mathrel{+}= \text{Recv } C_2$ (Node 0 now holds $C_2^{(2+3)} + C_2^{(0)}$) |
| | **Node 1** | Chunk 0 | Chunk 3 | Local $C_3 \mathrel{+}= \text{Recv } C_3$ (Node 1 now holds $C_3^{(3+0)} + C_3^{(1)}$) |
| | **Node 2** | Chunk 1 | Chunk 0 | Local $C_0 \mathrel{+}= \text{Recv } C_0$ (Node 2 now holds $C_0^{(0+1)} + C_0^{(2)}$) |
| | **Node 3** | Chunk 2 | Chunk 1 | Local $C_1 \mathrel{+}= \text{Recv } C_1$ (Node 3 now holds $C_1^{(1+2)} + C_1^{(3)}$) |
| **$k = 2$** | **Node 0** | Chunk 2 | Chunk 1 | Local $C_1 \mathrel{+}= \text{Recv } C_1$ $\implies$ **$C_1$ is fully reduced on Node 0!** |
| | **Node 1** | Chunk 3 | Chunk 2 | Local $C_2 \mathrel{+}= \text{Recv } C_2$ $\implies$ **$C_2$ is fully reduced on Node 1!** |
| | **Node 2** | Chunk 0 | Chunk 3 | Local $C_3 \mathrel{+}= \text{Recv } C_3$ $\implies$ **$C_3$ is fully reduced on Node 2!** |
| | **Node 3** | Chunk 1 | Chunk 0 | Local $C_0 \mathrel{+}= \text{Recv } C_0$ $\implies$ **$C_0$ is fully reduced on Node 3!** |

---

#### Phase 2: All-Gather ($N - 1 = 3$ Steps)
* Goal: Distribute the fully reduced chunks so every node has all 4 reduced values.
* At step $k \in \{0, 1, 2\}$:
  * Send chunk index: $c_{\text{send}} = (r + 1 - k + N) \pmod N$
  * Recv chunk index: $c_{\text{recv}} = (r - k + N) \pmod N$
  * Operation: Receive chunk $c_{\text{recv}}$ from predecessor and **overwrite** local buffer chunk $c_{\text{recv}}$ (no reduction; it is already fully reduced).

#### Complete Trace Table for Phase 2 ($N = 4$):

| Step ($k$) | Node ($r$) | Sends Chunk to $(r+1)\%4$ | Receives Chunk from $(r-1+4)\%4$ | Action Performed by Node $r$ |
|---|---|---|---|---|
| **$k = 0$** | **Node 0** | Chunk 1 (Fully reduced) | Chunk 0 (Fully reduced) | Local $C_0 = \text{Recv } C_0$ |
| | **Node 1** | Chunk 2 (Fully reduced) | Chunk 1 (Fully reduced) | Local $C_1 = \text{Recv } C_1$ |
| | **Node 2** | Chunk 3 (Fully reduced) | Chunk 2 (Fully reduced) | Local $C_2 = \text{Recv } C_2$ |
| | **Node 3** | Chunk 0 (Fully reduced) | Chunk 3 (Fully reduced) | Local $C_3 = \text{Recv } C_3$ |
| **$k = 1$** | **Node 0** | Chunk 0 | Chunk 3 | Local $C_3 = \text{Recv } C_3$ |
| | **Node 1** | Chunk 1 | Chunk 0 | Local $C_0 = \text{Recv } C_0$ |
| | **Node 2** | Chunk 2 | Chunk 1 | Local $C_1 = \text{Recv } C_1$ |
| | **Node 3** | Chunk 3 | Chunk 2 | Local $C_2 = \text{Recv } C_2$ |
| **$k = 2$** | **Node 0** | Chunk 3 | Chunk 2 | Local $C_2 = \text{Recv } C_2$ $\implies$ **Node 0 has all chunks!** |
| | **Node 1** | Chunk 0 | Chunk 3 | Local $C_3 = \text{Recv } C_3$ $\implies$ **Node 1 has all chunks!** |
| | **Node 2** | Chunk 1 | Chunk 0 | Local $C_0 = \text{Recv } C_0$ $\implies$ **Node 2 has all chunks!** |
| | **Node 3** | Chunk 2 | Chunk 1 | Local $C_1 = \text{Recv } C_1$ $\implies$ **Node 3 has all chunks!** |

---

#### ⚠️ Critical Indexing Traps & Fixes

#### Trap 1: The C++ Truncated Division Modulo Bug
In mathematics, $-1 \pmod 4 = 3$.
In C and C++, the `%` operator computes the **remainder of truncated division**, preserving the sign of the dividend:
```cpp
int x = -1 % 4; // Yields -1, NOT 3!
```
If you write `int prev_chunk = (r - k - 1) % N;`, when the expression evaluates to negative, you access index `-1` of your memory buffer, corrupting the heap.
* **The Fix**: Always bias by $N$ before taking the modulo:
```cpp
uint32_t c_recv = static_cast<uint32_t>((static_cast<int>(r) - static_cast<int>(k) - 1 + static_cast<int>(N * 2)) % static_cast<int>(N));
```

#### Trap 2: Message Sizes Not Divisible by $N$
If you have $100$ floats and $N = 4$, $100 / 4 = 25$ floats per chunk.
What if you have $102$ floats? $102 / 4 = 25$ (integer division). If every chunk is 25 floats, $4 \times 25 = 100$ floats. The last 2 floats are **never sent and never reduced**!
* **The Fix**: Partition using non-uniform chunk sizing:
  * Base quotient: $q = \lfloor S / N \rfloor$
  * Remainder: $rem = S \pmod N$
  * The first $rem$ chunks have size $q + 1$; the remaining $N - rem$ chunks have size $q$.

```cpp
// Minimal Illustration: Safe Non-Uniform Chunk Slicing (<25 lines)
#include <cstddef>
#include <algorithm>

struct ChunkSlice {
    size_t byte_offset{0};
    size_t byte_count{0};
};

inline ChunkSlice get_chunk_slice(size_t total_bytes, size_t rank, size_t world_size) noexcept {
    const size_t base_size = total_bytes / world_size;
    const size_t remainder = total_bytes % world_size;

    ChunkSlice slice;
    slice.byte_count = base_size + (rank < remainder ? 1 : 0);
    slice.byte_offset = rank * base_size + std::min(rank, remainder);
    return slice;
}
```

---

### 4.3 Pipelining, Credit Loops, and SIMD Vector Addition

#### Why it matters here
A naive ring implementation executes sequentially: Send Chunk $\to$ Poll CQ $\to$ CPU Vector Add $\to$ Send Next. During the CPU vector addition phase, the network interface sits idle. Pipelining and SIMD vectorization saturate both the CPU vector units and the RDMA link concurrently.

#### Mental Model: The Sliding Window Assembly Line
Without pipelining, an assembly line worker paints a car, waits for the car to be shipped to the dealership, and only then starts painting the next car.
With pipelining (double-buffering), worker $A$ paints car $k+1$ while car $k$ is in transit across the carrier.

```
Sequential (No Overlap):
Network: ──[ RX Chunk 0 ]──────────────────[ RX Chunk 1 ]──────────────
CPU:     ─────────────────[ Vector Add 0 ]───────────────[ Vector Add 1 ]

Pipelined (Double-Buffered Overlap):
Network: ──[ RX Chunk 0 ]──┬──[ RX Chunk 1 ]──┬──[ RX Chunk 2 ]────────
CPU:     ──────────────────┴──[ Vector Add 0 ]┴──[ Vector Add 1 ]──────
                              (Network and CPU busy simultaneously!)
```

#### Core Details: The Credit Loop Mechanism
Why do we need a credit loop using `shared/ring_buffer.h`?
* If Node 0 completes its vector addition quickly and blasts Chunk 1 to Node 1, but Node 1 is still busy vector-adding Chunk 0, Node 1 has not yet posted a receive buffer for Chunk 1.
* **Result**: Node 1's Queue Pair sends an RNR NAK back to Node 0. Node 0 enters exponential backoff, destroying pipeline throughput.
* **The Credit Invariant**:
  1. The receiver pre-posts $W$ receive buffers (e.g., $W = 2$ for double buffering) before the collective begins.
  2. The sender maintains a credit balance: `uint32_t credits = W;`.
  3. The sender only calls `ibv_post_send` when `credits > 0`.
  4. When the receiver consumes a chunk, it writes an immediate credit or returns a credit counter to the sender, incrementing the sender's balance.

#### SIMD Vectorized In-Place Addition
In the scatter-reduce phase, when a chunk arrives, the receiving CPU must add the incoming buffer into its local tensor:
$$D[i] = D[i] + S[i]$$
For single-precision floating point (`float32`), standard scalar addition processes 1 element per instruction ($4\ \text{bytes/cycle}$). At $100\ \text{Gbps}$ ($12.5\ \text{GB/s}$), scalar addition bottlenecks the collective.
* **AVX2**: Uses 256-bit YMM registers to process **8 floats per cycle** (`_mm256_add_ps`).
* **AVX-512**: Uses 512-bit ZMM registers to process **16 floats per cycle** (`_mm512_add_ps`).

```cpp
// Minimal Illustration: AVX2 Vectorized In-Place Accumulator (<25 lines)
#include <immintrin.h>
#include <cstddef>

void vector_add_float_avx2(float* dst, const float* src, size_t count) noexcept {
    size_t i = 0;
    // Process 8 floats (32 bytes) per iteration
    for (; i + 8 <= count; i += 8) {
        __m256 ymm_dst = _mm256_loadu_ps(dst + i);
        __m256 ymm_src = _mm256_loadu_ps(src + i);
        __m256 ymm_res = _mm256_add_ps(ymm_dst, ymm_src);
        _mm256_storeu_ps(dst + i, ymm_res);
    }
    // Clean up tail elements that don't fit in 256-bit vector
    for (; i < count; ++i) {
        dst[i] += src[i];
    }
}
```

---

### 4.4 Correctness Verification: Integer Bit-Exactness vs. Floating-Point Non-Associativity

#### Why it matters here
In Deliverable D2, the Definition of Done requires: *"Bit-exact against MPI on integer data. Float data within a stated tolerance."* If your integer tests fail by even 1 bit, you have a memory corruption or indexing bug. If your float test fails by $10^{-7}$, you might simply be observing IEEE-754 non-associativity!

#### 1. Integer Bit-Exactness
Two's-complement integer addition is strictly **associative and commutative**:
$$(A + B) + C = A + (B + C) \pmod{2^{32}}$$
* Regardless of whether you use a Ring topology, a Binary Tree, or single-threaded scalar addition, integer reduction **must match down to the exact bit representation**.
* **Canonical Verification Formula**:
  Initialize rank $r \in [0, N-1]$ with array elements $x_r[i] = r + 1$.
  The expected sum across $N$ ranks for element $i$ is:
  $$\text{Expected}[i] = \sum_{r=0}^{N-1} (r + 1) = \frac{N(N + 1)}{2}$$
  For $N = 4$, every element must equal $\frac{4 \times 5}{2} = 10$. If any element is $9$ or $11$, your scatter-reduce failed.

#### 2. Floating-Point Non-Associativity (IEEE-754)
Floating-point addition is **NOT associative**:
$$(A + B) + C \neq A + (B + C)$$
* **Why**: When numbers of vastly different magnitudes are added, the smaller number's mantissa is shifted to the right, truncating the least significant bits (loss of precision).
* **Ring Topology Accumulation Order**:
  Accumulates sequentially around the circle:
  $$\text{Result}_{\text{ring}} = (((x_0 + x_1) + x_2) + x_3)$$
* **Binary Tree Accumulation Order**:
  Accumulates pairwise up the tree branches:
  $$\text{Result}_{\text{tree}} = ((x_0 + x_1) + (x_2 + x_3))$$
* **Impact**: $\text{Result}_{\text{ring}}$ and $\text{Result}_{\text{tree}}$ **will produce different float bit patterns**!
* **The Rule**: Always verify floating-point results using an **epsilon relative error tolerance**:
  $$\frac{|x_{\text{observed}} - x_{\text{expected}}|}{|x_{\text{expected}}|} < \epsilon \quad (\text{where } \epsilon = 10^{-6}\text{ for float32})$$

---

### 4.5 The Fair MPI Comparison: Forcing Transports & Avoiding Dishonest Benchmarks

#### Why it matters here
In Deliverable D2, you benchmark your custom Ring All-Reduce against `MPI_Allreduce`. If you run OpenMPI out of the box on a single laptop without tuning MCA parameters, your comparison will be scientifically dishonest.

#### The Localhost MPI Trap
When OpenMPI runs multiple ranks on a single host (`localhost`), it detects shared memory support and routes traffic through the **Vader BTL (Byte Transfer Layer)** or **CMA (Cross-Memory Attach)**:
* Data is copied directly across process virtual address spaces in RAM at memory bus speeds (**$30\ \text{GB/s} - 60\ \text{GB/s}$**).
* It does **not** touch network sockets, UDP packets, or the Soft-RoCE driver.
* If you compare your Soft-RoCE ring (which traverses kernel UDP sockets at $15\ \mu\text{s}$) against shared-memory MPI, MPI will appear $10\times$ faster, invalidating your analysis.

#### How to Force OpenMPI onto the Network Transports

#### 1. Force OpenMPI onto TCP Sockets:
```bash
mpirun --mca pml ob1 --mca btl self,tcp -np 4 ./build/allreduce_bench --transport mpi_tcp
```
* `--mca pml ob1`: Uses the standard point-to-point management layer.
* `--mca btl self,tcp`: Completely disables `vader` and `sm`, forcing OpenMPI to transmit packets over the loopback TCP interface.

#### 2. Force OpenMPI onto Soft-RoCE (`rxe0` via UCX):
Modern OpenMPI uses Unified Communication X (UCX) for RDMA:
```bash
mpirun --mca pml ucx \
       -x UCX_TLS=rc_verbs \
       -x UCX_NET_DEVICES=rxe0:1 \
       -np 4 ./build/allreduce_bench --transport mpi_roce
```
* `UCX_TLS=rc_verbs`: Strictly restricts UCX to RDMA Reliable Connection verbs, disabling shared-memory (`sm`, `posix`).
* `UCX_NET_DEVICES=rxe0:1`: Binds UCX directly to port 1 of your `rxe0` Soft-RoCE adapter.

#### Checklist for a Rigorous, Honest Comparison:
1. **Identical Buffer Sizes**: Ensure both tools evaluate the same number of *bytes*, not *elements* (e.g., $1\ \text{MB} = 262{,}144\ \text{floats}$).
2. **Identical Core Pinning**: Pin OpenMPI ranks to the exact same cores using `--bind-to core` or `numactl`.
3. **Warmup Parity**: Discard the first 100 iterations on both harnesses.
4. **Transport Provenance**: In `results/mpi_comparison.csv`, always record the exact `mpi_transport` used (`sm`, `tcp`, or `rc_verbs`).

---

### 4.6 CPU Oversubscription: Why $N = 8$ is Only a Stress Test

#### Why it matters here
The specification requires evaluating $N = 2$, $N = 4$, and $N = 8$ nodes, but explicitly designates $N = 8$ as an **oversubscription stress test**. You must explain why $N = 8$ degrades on your laptop.

#### Core Topology Breakdown on Intel Core i3-1220P
* Total Execution Units: **2 P-cores (4 threads) + 8 E-cores (8 threads) = 10 physical cores / 12 logical threads**.
* For $N = 4$: All 4 ranks run on physical Cores 4, 5, 6, and 7 (E-Core Cluster 0). Each process owns 100% of an isolated physical CPU core.
* For $N = 8$:
  * You allocate 8 ranks across Cores 4 through 11.
  * **The Fatal Contention**: Soft-RoCE (`rdma_rxe`) is a software emulator. When packets are transmitted, the kernel spawns worker threads (`rxe_worker` and softirq handlers `ksoftirqd`) to process network packet serialization and UDP checksums.
  * If all 8 benchmark processes are running 100% busy-poll spin loops (`ibv_poll_cq`) across all 8 E-cores, **there are zero spare CPU cycles for the kernel `rxe_worker` threads**!
  * The Linux scheduler is forced to preempt your user-space polling threads, triggering severe context switches, TLB shootdowns, and CPU cache thrashing.

```
N = 4 (Clean Baseline):
Core 4: [ Rank 0 Busy-Poll (100%) ]
Core 5: [ Rank 1 Busy-Poll (100%) ]
Core 6: [ Rank 2 Busy-Poll (100%) ]
Core 7: [ Rank 3 Busy-Poll (100%) ]
Cores 0-3 (P-cores): [ OS Tasks + rxe_worker kernel threads run freely ] ──► Zero Contention!

N = 8 (Oversubscription Stress Test):
Cores 4-11: [ 8 Ranks Busy-Polling ] ◄── FIGHTING FOR CYCLES ──► [ rxe_worker Threads ]
Result: Severe thread preemption, lockups, and massive tail latency spikes!
```

**Reporting Rule**: Label all $N = 8$ benchmark results as **"Oversubscribed / Correctness Stress Test Only"** in your README and plots.

---

### Self-Check Questions (Part 4)

1. In Ring All-Reduce, why is the total volume of data transmitted by each node bounded by $2S$, regardless of whether the cluster contains 4 nodes or 4,000 nodes?
2. Under what condition will Binary Tree All-Reduce outperform Ring All-Reduce?
3. In C++, why is calculating the previous node's chunk index as `(r - k - 1) % N` a severe bug?
4. Why does running an integer all-reduce produce identical bit patterns across Ring and Tree topologies, while a floating-point all-reduce produces slightly different values?

<details>
<summary><b>Click to View Answers</b></summary>

1. **Answer**: In a ring of $N$ nodes, the total message size $S$ is divided into $N$ equal chunks of size $\frac{S}{N}$. In the scatter-reduce phase, each node sends $N - 1$ chunks, transmitting $(N - 1) \frac{S}{N}$ bytes. In the all-gather phase, each node again sends $N - 1$ chunks, transmitting $(N - 1) \frac{S}{N}$ bytes. Total data sent per node is $2 \left(\frac{N - 1}{N}\right) S$. As $N \to \infty$, $2\frac{N-1}{N} \to 2$, meaning the data volume per node approaches $2S$ and never exceeds it.
2. **Answer**: Binary Tree All-Reduce outperforms Ring All-Reduce for small message sizes ($S < S^*$), where communication time is dominated by network startup latency ($\alpha$) rather than bandwidth ($\beta$). The tree topology takes only $2 \lceil \log_2 N \rceil$ serial hops on its critical path, whereas the ring takes $2(N - 1)$ serial hops.
3. **Answer**: In C and C++, the `%` operator computes the remainder of truncated division, which preserves the negative sign. If $(r - k - 1)$ evaluates to a negative number, the result will be negative (e.g., $-1 \% 4 == -1$), causing an out-of-bounds array access and corrupting memory. The expression must be safely biased: `(r - k - 1 + 2 * N) % N`.
4. **Answer**: Two's-complement integer addition is strictly associative: $(A + B) + C = A + (B + C) \pmod{2^{32}}$, so grouping does not alter the exact bits. Floating-point addition (IEEE-754) is **non-associative**: shifting mantissas during rounding causes loss of least-significant bits depending on the order of summation. Ring accumulates sequentially $((x_0 + x_1) + x_2)$, whereas Tree accumulates pairwise $((x_0 + x_1) + (x_2 + x_3))$.
</details>

---

### 4.7 If You Remember Nothing Else (Part 4)
1. Ring All-Reduce is bandwidth-optimal because each node transfers $2\frac{N-1}{N}S$ bytes; Tree All-Reduce wins for small messages because it takes only $2\log_2 N$ latency hops.
2. Always bias C++ modulo expressions with $+ 2N$ when computing indices with subtractions (`(r - k - 1 + 2*N) % N`), or you will index memory with negative offsets.
3. Handle non-divisible message sizes by giving the first $rem$ chunks $q+1$ bytes and the remainder $q$ bytes; never truncate $S/N$.
4. Floating-point addition is non-associative: Ring and Tree will produce different float bits; verify float collectives with a relative error epsilon ($< 10^{-6}$).
5. When comparing against OpenMPI on a single host, force the transport onto TCP (`--mca btl self,tcp`) or Soft-RoCE via UCX (`-x UCX_TLS=rc_verbs`), or MPI's shared-memory engine will give an unfair comparison.
