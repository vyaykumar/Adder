#pragma once

#include <vector>
#include <cstdint>
#include <cstddef>
#include <expected>
#include <string>
#include <allreduce/verbs_qp.h>
#include <shared/verbs.h>
#include <shared/free_list_pool.h>

namespace adder::allreduce {

struct TreeTopology {
    uint32_t rank{0};
    uint32_t world_size{1};
    int parent_rank{-1};
    int left_child_rank{-1};
    int right_child_rank{-1};

    static TreeTopology compute(uint32_t rank, uint32_t world_size) {
        TreeTopology topo;
        topo.rank = rank;
        topo.world_size = world_size;

        if (rank > 0) {
            topo.parent_rank = static_cast<int>((rank - 1) / 2);
        }

        uint32_t left = 2 * rank + 1;
        uint32_t right = 2 * rank + 2;

        if (left < world_size) topo.left_child_rank = static_cast<int>(left);
        if (right < world_size) topo.right_child_rank = static_cast<int>(right);

        return topo;
    }
};

class TreeAllReduce {
public:
    explicit TreeAllReduce(const TreeTopology& topo);
    ~TreeAllReduce() = default;

    // =========================================================================
    // Binary Tree Link & Pre-Registered Memory Binding
    // Goal: Bind parent, left-child, and right-child QPs (nullable for root/leaves)
    // along with the shared CompletionQueue and pre-registered MemoryRegions.
    // =========================================================================
    [[nodiscard]] std::expected<void, std::string> setup(
        VerbsQp* qp_parent,
        VerbsQp* qp_left_child,
        VerbsQp* qp_right_child,
        shared::FreeListMemoryPool* memory_pool = nullptr,
        verbs::CompletionQueue* shared_cq = nullptr,
        verbs::MemoryRegion* tensor_mr = nullptr,
        verbs::MemoryRegion* staging_mr = nullptr
    );

    // Executes 2-phase Binary Tree All-Reduce:
    // Phase 1: Reduce up to root
    // Phase 2: Broadcast down to leaves
    [[nodiscard]] std::expected<void, std::string> execute(
        void* data_buffer,
        size_t total_bytes,
        bool is_float = false
    );

private:
    TreeTopology topo_;
    VerbsQp* qp_parent_{nullptr};
    VerbsQp* qp_left_{nullptr};
    VerbsQp* qp_right_{nullptr};
    shared::FreeListMemoryPool* pool_{nullptr};
    verbs::CompletionQueue* cq_{nullptr};
    verbs::MemoryRegion* tensor_mr_{nullptr};
    verbs::MemoryRegion* staging_mr_{nullptr};
};

} // namespace adder::allreduce
