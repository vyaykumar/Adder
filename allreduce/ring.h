#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <span>
#include <expected>
#include <string>
#include <allreduce/verbs_qp.h>
#include <shared/verbs.h>
#include <shared/free_list_pool.h>
#include <shared/ring_buffer.h>

namespace adder::allreduce {

struct RingNodeConfig {
    uint32_t rank{0};
    uint32_t world_size{1};
    size_t chunk_size{64 * 1024};
    size_t total_elements{0};
    bool is_float{false};
};

class RingAllReduce {
public:
    explicit RingAllReduce(const RingNodeConfig& config);
    ~RingAllReduce() = default;

    // =========================================================================
    // Mesh & Pre-Registered Memory Binding
    // Goal: Bind downstream (qp_send_next) and upstream (qp_recv_prev) QPs
    // along with the shared CompletionQueue and pre-registered MemoryRegions
    // (tensor_mr for local chunks, staging_mr for incoming chunk scratchpad).
    // =========================================================================
    //
    // Q: Why pass tensor_mr and staging_mr during setup_mesh instead of execute()?
    // A: Calling ibv_reg_mr on every execute() call pins pages in the kernel (~20-50 us).
    //    Pre-registering once at setup keeps execute() strictly zero-allocation and kernel-bypass!
    //
    // Initializes peer connection QPs (next node in ring: (rank + 1) % N, prev node: (rank + N - 1) % N)
    [[nodiscard]] std::expected<void, std::string> setup_mesh(
        VerbsQp* qp_send_next,
        VerbsQp* qp_recv_prev,
        shared::FreeListMemoryPool* memory_pool = nullptr,
        verbs::CompletionQueue* shared_cq = nullptr,
        verbs::MemoryRegion* tensor_mr = nullptr,
        verbs::MemoryRegion* staging_mr = nullptr
    );

    // Executes the two-phase Ring All-Reduce
    // Phase 1: Scatter-Reduce (N-1 steps)
    // Phase 2: All-Gather (N-1 steps)
    [[nodiscard]] std::expected<void, std::string> execute(
        void* data_buffer,
        size_t total_bytes
    );

    // In-place vector addition for chunks
    static void vector_add_int32(int32_t* dst, const int32_t* src, size_t count) noexcept;
    static void vector_add_float(float* dst, const float* src, size_t count) noexcept;

private:
    RingNodeConfig config_;
    VerbsQp* qp_next_{nullptr};
    VerbsQp* qp_prev_{nullptr};
    shared::FreeListMemoryPool* pool_{nullptr};
    verbs::CompletionQueue* cq_{nullptr};
    verbs::MemoryRegion* tensor_mr_{nullptr};
    verbs::MemoryRegion* staging_mr_{nullptr};

    // Credit loop ring buffer to prevent RNR NAK drops
    shared::SpscRingBuffer<uint32_t, 64> credit_ring_;
};

} // namespace adder::allreduce
