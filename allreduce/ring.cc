#include <allreduce/ring.h>
#include <shared/busy_poll.h>
#include <iostream>
#include <cstring>

namespace adder::allreduce {

RingAllReduce::RingAllReduce(const RingNodeConfig& config)
    : config_(config) {}

std::expected<void, std::string> RingAllReduce::setup_mesh(
    VerbsQp* qp_send_next,
    VerbsQp* qp_recv_prev,
    shared::FreeListMemoryPool* memory_pool,
    verbs::CompletionQueue* shared_cq,
    verbs::MemoryRegion* tensor_mr,
    verbs::MemoryRegion* staging_mr
) {
    if (!qp_send_next || !qp_recv_prev) {
        return std::unexpected("Null QP pointer passed to setup_mesh");
    }
    qp_next_ = qp_send_next;
    qp_prev_ = qp_recv_prev;
    pool_ = memory_pool;
    cq_ = shared_cq;
    tensor_mr_ = tensor_mr;
    staging_mr_ = staging_mr;

    // =========================================================================
    // Step 1: Credit Ring & Receive Readiness Initialization
    // Goal: Seed the lock-free SPSC credit_ring_ (or pre-post receive buffers on
    // qp_prev_) so the upstream neighbor never sends into an empty receive queue
    // (preventing hardware RNR NAK retry storms).
    // =========================================================================
    //
    // Q: Why do we need credit flow control or pre-posted receives before sending?
    // A: In two-sided SEND/RECV (or RDMA_WRITE_WITH_IMM), if a packet arrives at
    //    qp_prev_ before an ibv_recv_wr is posted, the NIC responds with an RNR NAK
    //    and stalls the pipeline for min_rnr_timer (~0.64ms+).
    //
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
    [[maybe_unused]] const uint32_t rank = config_.rank;

    if (N <= 1) {
        return {}; // Single-node trivial completion
    }

    [[maybe_unused]] const size_t chunk_size = total_bytes / N;

    // =========================================================================
    // Phase 1: Scatter-Reduce (N - 1 steps)
    // Goal: Divide the tensor into N equal chunks. Across N - 1 steps, each rank
    // simultaneously sends chunk (rank - k) % N to downstream neighbor (rank + 1) % N
    // and receives chunk (rank - k - 1) % N from upstream neighbor (rank - 1 + N) % N
    // into staging_mr_, accumulating it in-place into tensor_mr_[recv_chunk_idx].
    // =========================================================================
    //
    // Q: In D1 pingpong.cc Step 8, Server waited for Ping then sent Pong. How does
    //    a Ring step differ?
    // A: Every rank is BOTH sender and receiver in every step!
    //    1. PrePost Receive on qp_prev_ pointing to staging_mr_ (wr_id tagged for Recv).
    //    2. Post Send on qp_next_ pointing to tensor_mr_ + send_chunk_idx * chunk_size.
    //    3. Spin-poll shared CQ (with ADDER_CPU_PAUSE() and spin timeout, just like D1)
    //       until BOTH the Send CQE and Recv CQE complete.
    //    4. Accumulate staging_mr_ into tensor_mr_[recv_chunk_idx] via vector_add_*.
    //
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
    // Goal: After Phase 1, each rank holds 1/N of the fully reduced final tensor
    // (at chunk index (rank + 1) % N). Across N - 1 steps, circulate these finished
    // chunks around the ring directly into tensor_mr_[recv_chunk_idx] (no SIMD needed!).
    // =========================================================================
    //
    // Q: Do we need the staging_mr_ scratchpad in Phase 2 (All-Gather)?
    // A: No! Because the incoming chunk is already fully reduced and replaces the
    //    local unreduced chunk, you can post_recv directly into
    //    tensor_mr_ + recv_chunk_idx * chunk_size (zero-copy DMA into final destination!).
    //
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
    // =========================================================================
    // SIMD Vector Reduction (int32_t)
    // Goal: Accumulate dst[i] += src[i] using C++23 [[assume]] alignment hints,
    // <experimental/simd>, or AVX2/AVX-512 intrinsics (_mm256_add_epi32).
    // =========================================================================
    // TODO: Add C++23 [[assume(reinterpret_cast<uintptr_t>(dst) % 32 == 0)]] or
    // explicit AVX2 / std::experimental::simd loop + scalar tail for count % 8.
    // Basic loop; can be upgraded to AVX2 / AVX-512 (_mm256_add_epi32 / _mm512_add_epi32)
    for (size_t i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

void RingAllReduce::vector_add_float(float* dst, const float* src, size_t count) noexcept {
    // =========================================================================
    // SIMD Vector Reduction (float)
    // Goal: Accumulate dst[i] += src[i] using C++23 [[assume]] alignment hints,
    // <experimental/simd>, or AVX2/AVX-512 intrinsics (_mm256_add_ps).
    // =========================================================================
    // TODO: Add C++23 [[assume(reinterpret_cast<uintptr_t>(dst) % 32 == 0)]] or
    // explicit AVX2 / std::experimental::simd loop + scalar tail for count % 8.
    // Basic loop; can be upgraded to AVX2 / AVX-512 (_mm256_add_ps / _mm512_add_ps)
    for (size_t i = 0; i < count; ++i) {
        dst[i] += src[i];
    }
}

} // namespace adder::allreduce
