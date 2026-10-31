# Man 04: Collective Communication & Reduction Topologies

## 1. The Collective Communication Problem in Distributed AI

In distributed Deep Learning training (Data Parallelism via PyTorch DDP, DeepSpeed, or Megatron-LM), each GPU node processes an independent mini-batch of data. At the end of every backward pass:
* Every node possesses a local gradient tensor $\mathbf{g}_i$.
* All nodes must compute the element-wise sum of all gradients across the cluster:
  $$\mathbf{G} = \sum_{i=0}^{N-1} \mathbf{g}_i$$
* The fully reduced gradient tensor $\mathbf{G}$ must be broadcast back to all nodes before the optimizer step can occur. This collective operation is **All-Reduce**.

---

## 2. Ring All-Reduce: Bandwidth-Optimal Collective

Ring All-Reduce organizes $N$ nodes into a unidirectional logical ring, where node $i$ transmits data only to node $(i+1) \pmod N$ and receives data only from node $(i - 1 + N) \pmod N$.

```
                 Node 0
              ┌─────────┐
         ┌───►│ Chunk k │────┐
         │    └─────────┘    │
         │                   ▼
    ┌─────────┐         ┌─────────┐
    │ Chunk k │         │ Chunk k │
    │ Node 3  │         │ Node 1  │
    └─────────┘         └─────────┘
         ▲                   │
         │    ┌─────────┐    │
         └────│ Chunk k │◄───┘
              │ Node 2  │
              └─────────┘
```

The tensor of size $S$ bytes is partitioned into $N$ equal chunks of size $\frac{S}{N}$. The algorithm proceeds in two distinct phases of $N-1$ steps each:

---

### 2.1 Mathematical Proof of Communication Volume

1. **Scatter-Reduce Phase**:
   * Each node sends one chunk of size $\frac{S}{N}$ to its downstream neighbor in each step.
   * Upon receiving a chunk, the node accumulates it into its local corresponding chunk.
   * Running for $N-1$ steps, each node transmits:
     $$\text{Volume}_{\text{scatter}} = (N - 1) \times \frac{S}{N} = \frac{N - 1}{N} S\text{ bytes}$$
   * At the conclusion of this phase, each node holds the fully reduced sum for exactly **one** chunk.

2. **All-Gather Phase**:
   * Each node transmits its fully reduced chunk around the ring for another $N-1$ steps.
   * Incoming chunks are copied directly into the local tensor without accumulation.
   * Running for $N-1$ steps, each node transmits:
     $$\text{Volume}_{\text{gather}} = (N - 1) \times \frac{S}{N} = \frac{N - 1}{N} S\text{ bytes}$$

3. **Total Data Transmitted per Node**:
   $$\text{Total Volume} = \text{Volume}_{\text{scatter}} + \text{Volume}_{\text{gather}} = 2 \left(\frac{N - 1}{N}\right) S\text{ bytes}$$

> [!TIP]
> Notice that as the cluster size grows ($N \to \infty$), the total data sent per node approaches an upper bound of $2S$. The network bandwidth required per node is **completely independent of the number of nodes in the cluster**!

---

### 2.2 Step-by-Step Execution Trace for $N=4$ Nodes

Let tensor chunks be $C_0, C_1, C_2, C_3$. In the table below, notation $0$ indicates chunk $C_0$ from Rank 0, and $(0+3)$ indicates accumulated chunks.

#### Phase 1: Scatter-Reduce ($N-1 = 3$ Steps)

| Step | Rank 0 Sends $\to$ Rank 1 | Rank 1 Sends $\to$ Rank 2 | Rank 2 Sends $\to$ Rank 3 | Rank 3 Sends $\to$ Rank 0 |
|---|---|---|---|---|
| **Step 0** | Sends $C_0$ | Sends $C_1$ | Sends $C_2$ | Sends $C_3$ |
| *Result* | R1 has $(C_1 + C_0)$ | R2 has $(C_2 + C_1)$ | R3 has $(C_3 + C_2)$ | R0 has $(C_0 + C_3)$ |
| **Step 1** | Sends $(C_0 + C_3)$ | Sends $(C_1 + C_0)$ | Sends $(C_2 + C_1)$ | Sends $(C_3 + C_2)$ |
| *Result* | R1 has $(C_1 + C_0 + C_3)$ | R2 has $(C_2 + C_1 + C_0)$ | R3 has $(C_3 + C_2 + C_1)$ | R0 has $(C_0 + C_3 + C_2)$ |
| **Step 2** | Sends $(C_0+C_3+C_2)$ | Sends $(C_1+C_0+C_3)$ | Sends $(C_2+C_1+C_0)$ | Sends $(C_3+C_2+C_1)$ |
| *Final* | **R1 holds full $C_0$** | **R2 holds full $C_1$** | **R3 holds full $C_2$** | **R0 holds full $C_3$** |

#### Phase 2: All-Gather ($N-1 = 3$ Steps)
Each node now circulates its fully reduced chunk. After 3 more steps, all 4 nodes hold the complete reduced tensor $(C_0 + C_1 + C_2 + C_3)$.

---

## 3. Binary Tree All-Reduce: Latency-Optimal Collective

In a Binary Tree topology:
1. **Reduce-Up Phase**: Leaves send their complete tensor $S$ to their parent, which vector-adds incoming data. This propagates up the tree to the root (Rank 0) in $\log_2(N)$ steps.
2. **Broadcast-Down Phase**: The root transmits the fully reduced tensor $S$ back down the tree to its children in $\log_2(N)$ steps.

### Communication Metrics for Tree:
* **Critical Path Steps**: $2 \log_2(N)$ steps.
* **Total Volume Moved per Node**: $2 \log_2(N) \cdot S$ bytes.

---

## 4. The Crossover Equation: Ring vs. Tree

Under the standard Hockney communication model ($T = \alpha + S\beta$, where $\alpha$ is network latency/hop and $\beta = 1/\text{bandwidth}$):

$$T_{\text{ring}}(S) = 2(N - 1)\alpha + 2\left(\frac{N - 1}{N}\right)S\beta$$
$$T_{\text{tree}}(S) = 2\log_2(N)\alpha + 2\log_2(N)S\beta$$

```
Latency (T)
     ▲
     │                  Tree (Dominates for small messages due to fewer hops: 2 log2 N)
     │                 /
     │                /      Ring (Dominates for large messages due to link saturation: 2 S)
     │               /     /
     │              /    /
     │             /   /
     │            /  /
     │           / /
     │          X  <── Crossover Message Size S*
     │        /   \
     │      /       \
     └─────┴──────────┴────────────────────────► Message Size (S)
```

### The Crossover Threshold $S^*$:
$$S^* = \frac{2(N - 1 - \log_2 N)\alpha}{2\left(\log_2 N - \frac{N - 1}{N}\right)\beta}$$

* **For Small Buffers ($S < S^*$, e.g. $< 64\text{ KB}$)**: Binary Tree achieves lower total completion time because latency hops ($2\log_2 N$) dominate.
* **For Large Buffers ($S > S^*$, e.g. $\ge 256\text{ KB}$)**: Ring All-Reduce significantly outperforms Tree because network bandwidth dominates, and the Ring moves far less data per link ($2\frac{N-1}{N}S \ll 2\log_2 N \cdot S$).

---

## 5. Vectorized In-Place Accumulation (SIMD)

During the Scatter-Reduce phase, received chunks must be added to local memory buffers. Performing scalar addition loops creates a severe CPU memory bottleneck.

Adder leverages **AVX2 / AVX-512 SIMD vectorization**:

```cpp
#include <immintrin.h>

void vector_add_int32_avx2(int32_t* dst, const int32_t* src, size_t count) noexcept {
    size_t i = 0;
    // Process 8 x int32 (256 bits) per instruction
    for (; i + 8 <= count; i += 8) {
        __m256i v_dst = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(dst + i));
        __m256i v_src = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
        __m256i v_res = _mm256_add_epi32(v_dst, v_src);
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), v_res);
    }
    // Clean up tail elements
    for (; i < count; ++i) {
        dst[i] += src[i];
    }
}
```

---

## 6. Floating-Point Non-Associativity Trap

When validating all-reduce correctness:
* **Integer Buffers (`int32_t`)**: Addition is mathematically associative. The result must match a single-core reference sum **bit-for-bit**:
  $$\sum_{r=1}^N r = \frac{N(N + 1)}{2}$$
* **Floating-Point Buffers (`float`)**: **IEEE-754 floating-point addition is NOT associative!**
  $$(a + b) + c \neq a + (b + c)$$
  * In Ring All-Reduce, addition accumulates sequentially: $(((a + b) + c) + d)$.
  * In Binary Tree, addition accumulates hierarchically: $((a + b) + (c + d))$.
  * Because floating-point rounding errors depend on summation order, Ring and Tree will produce slightly different float values.
  * **Rule**: Verify floating-point collective correctness using an allowable relative error tolerance:
    $$\frac{|\text{result} - \text{reference}|}{\text{reference}} < 10^{-6}$$
