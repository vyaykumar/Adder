#include "allreduce/tree.h"
#include "allreduce/ring.h"
#include <iostream>

namespace adder::allreduce {

TreeAllReduce::TreeAllReduce(const TreeTopology& topo)
    : topo_(topo) {}

std::expected<void, std::string> TreeAllReduce::setup(
    VerbsQp* qp_parent,
    VerbsQp* qp_left_child,
    VerbsQp* qp_right_child,
    shared::FreeListMemoryPool* memory_pool
) {
    qp_parent_ = qp_parent;
    qp_left_ = qp_left_child;
    qp_right_ = qp_right_child;
    pool_ = memory_pool;

    std::cout << "// TODO: TreeAllReduce::setup - Pre-post receives for tree links\n";
    return {};
}

std::expected<void, std::string> TreeAllReduce::execute(
    void* data_buffer,
    size_t total_bytes,
    bool is_float
) {
    if (!data_buffer || total_bytes == 0) {
        return std::unexpected("Invalid buffer or size for Tree All-Reduce");
    }

    // =========================================================================
    // Phase 1: Reduce Up the Tree
    // =========================================================================
    // TODO: Step 1:
    // - If this node has left child: receive data, vector add into local data_buffer.
    // - If this node has right child: receive data, vector add into local data_buffer.
    // - If this node is not root (rank != 0): send accumulated data_buffer to parent.
    std::cout << "// TODO: TreeAllReduce::execute - Phase 1: Reduce up to root (rank 0)\n";

    // =========================================================================
    // Phase 2: Broadcast Down the Tree
    // =========================================================================
    // TODO: Step 2:
    // - If this node is not root: receive fully reduced data from parent into data_buffer.
    // - If this node has left child: send fully reduced data down to left child.
    // - If this node has right child: send fully reduced data down to right child.
    std::cout << "// TODO: TreeAllReduce::execute - Phase 2: Broadcast down to leaves\n";

    return {};
}

} // namespace adder::allreduce
