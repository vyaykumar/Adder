#include "allreduce/ring.h"
#include "shared/busy_poll.h"
#include <iostream>
#include <cstring>

namespace adder::allreduce {

RingAllReduce::RingAllReduce(const RingNodeConfig& config)
    : config_(config) {}

std::expected<void, std::string> RingAllReduce::setup_mesh(
    VerbsQp* qp_send_next,
    VerbsQp* qp_recv_prev,
    shared::FreeListMemoryPool* memory_pool
) {
    if (!qp_send_next || !qp_recv_prev || !memory_pool) {
        return std::unexpected("Null pointer passed to setup_mesh");
    }
    qp_next_ = qp_send_next;
    qp_prev_ = qp_recv_prev;
    pool_ = memory_pool;

    // TODO: Initialize credit loop: pre-post receive buffers on qp_prev_
    // to ensure receiver is ready before sender posts first work request.
    std::cout << "// TODO: RingAllReduce::setup_mesh - Pre-post receive credits to prevent RNR NAK\n";

    return {};
}

std::expected<void, std::string> RingAllReduce::execute(
    void* data_buffer,
    size_t total_bytes
) {
    if (!data_buffer || total_bytes == 0) {
        return std::unexpected("Invalid buffer or size for Ring All-Reduce");
    }

    const uint32_t N = config_.world_size;
    const uint32_t rank = config_.rank;

    if (N <= 1) {
        return {}; // Single-node trivial completion
    }

    const size_t chunk_size = total_bytes / N;

    // =========================================================================
    // Phase 1: Scatter-Reduce (N - 1 steps)
    // =========================================================================
    // TODO: Step 1: For step k in 0 .. N-2:
    //   send_chunk_idx = (rank - k + N) % N
    //   recv_chunk_idx = (rank - k - 1 + N) % N
    //   1. Check credit ring before sending to node (rank + 1) % N.
    //   2. Post send of chunk send_chunk_idx.
    //   3. Poll CQ for completion of incoming chunk recv_chunk_idx from (rank + N - 1) % N.
    //   4. Perform in-place vector addition of received chunk into data_buffer chunk recv_chunk_idx.
    //   5. Replenish credit back to sender.
    std::cout << "// TODO: RingAllReduce::execute - Phase 1: Scatter-Reduce (N-1 steps)\n";

    // =========================================================================
    // Phase 2: All-Gather (N - 1 steps)
    // =========================================================================
    // TODO: Step 2: For step k in 0 .. N-2:
    //   send_chunk_idx = (rank - k + 1 + N) % N
    //   recv_chunk_idx = (rank - k + N) % N
    //   1. Post send of fully reduced chunk to next node.
    //   2. Poll CQ for incoming chunk from previous node.
    //   3. Direct copy / DMA of incoming chunk into local data_buffer chunk recv_chunk_idx.
    std::cout << "// TODO: RingAllReduce::execute - Phase 2: All-Gather (N-1 steps)\n";

    return {};
}

void RingAllReduce::vector_add_int32(int32_t* dst, const int32_t* src, size_t count) noexcept {
    // Basic loop; can be upgraded to AVX2 / AVX-512 (_mm256_add_epi32 / _mm512_add_epi32)
    for (size_t i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

void RingAllReduce::vector_add_float(float* dst, const float* src, size_t count) noexcept {
    // Basic loop; can be upgraded to AVX2 / AVX-512 (_mm256_add_ps / _mm512_add_ps)
    for (size_t i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

} // namespace adder::allreduce
