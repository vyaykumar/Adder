#include <allreduce/tree.h>
#include <allreduce/ring.h>
#include <shared/busy_poll.h>
#include <iostream>

namespace adder::allreduce {

TreeAllReduce::TreeAllReduce(const TreeTopology& topo)
    : topo_(topo) {}

std::expected<void, std::string> TreeAllReduce::setup(
    VerbsQp* qp_parent,
    VerbsQp* qp_left_child,
    VerbsQp* qp_right_child,
    shared::FreeListMemoryPool* memory_pool,
    verbs::CompletionQueue* shared_cq,
    verbs::MemoryRegion* tensor_mr,
    verbs::MemoryRegion* staging_mr
) {
    qp_parent_ = qp_parent;
    qp_left_ = qp_left_child;
    qp_right_ = qp_right_child;
    pool_ = memory_pool;
    cq_ = shared_cq;
    tensor_mr_ = tensor_mr;
    staging_mr_ = staging_mr;

    // =========================================================================
    // Step 1: Pre-Post Tree Link Receives
    // Goal: Pre-post initial receive buffers on active child links (for Phase 1)
    // and parent link (for Phase 2) so no sender triggers an RNR NAK.
    // =========================================================================
    std::cout << "// TODO: TreeAllReduce::setup - Pre-post receives for tree links\n";
    return {};
}

std::expected<void, std::string> TreeAllReduce::execute(
    void* data_buffer,
    size_t total_bytes,
    [[maybe_unused]] bool is_float
) {
    if (!data_buffer || total_bytes == 0) {
        return std::unexpected("Invalid buffer or size for Tree All-Reduce");
    }

    // =========================================================================
    // Phase 1: Reduce Up the Tree (Leaves -> Root Rank 0)
    // Goal: Aggregate full buffers bottom-up in log2(N) hops.
    //   1. If left child exists (qp_left_ != nullptr):
    //      PrePost Receive into staging_mr_ -> Poll CQ -> RingAllReduce::vector_add_* into data_buffer.
    //   2. If right child exists (qp_right_ != nullptr):
    //      PrePost Receive into staging_mr_ -> Poll CQ -> RingAllReduce::vector_add_* into data_buffer.
    //   3. If parent exists (topo_.parent_rank >= 0 && qp_parent_ != nullptr):
    //      Post Send of accumulated data_buffer up to parent -> Poll CQ for send completion.
    // =========================================================================
    //
    // Q: Why does Binary Tree beat Ring at tiny payload sizes (e.g. <= 4 KB)
    //    but lose at large payload sizes (>= 1 MB)?
    // A: Tree takes only 2 * log2(N) steps (lower latency overhead), whereas Ring
    //    takes 2 * (N - 1) steps. However, Tree sends the ENTIRE buffer S on every
    //    link (2 * log2(N) * S bytes), while Ring pipelines S/N chunks (only 2*(N-1)/N * S bytes).
    //
    // TODO: Step 1:
    // - If this node has left child: receive data, vector add into local data_buffer.
    // - If this node has right child: receive data, vector add into local data_buffer.
    // - If this node is not root (rank != 0): send accumulated data_buffer to parent.
    std::cout << "// TODO: TreeAllReduce::execute - Phase 1: Reduce up to root (rank 0)\n";

    // =========================================================================
    // Phase 2: Broadcast Down the Tree (Root Rank 0 -> Leaves)
    // Goal: Distribute the fully reduced tensor from Rank 0 down to all leaves.
    //   1. If parent exists (topo_.parent_rank >= 0 && qp_parent_ != nullptr):
    //      PrePost Receive directly into tensor_mr_ (data_buffer) -> Poll CQ.
    //   2. If left child exists (qp_left_ != nullptr):
    //      Post Send of data_buffer to left child -> Poll CQ.
    //   3. If right child exists (qp_right_ != nullptr):
    //      Post Send of data_buffer to right child -> Poll CQ.
    // =========================================================================
    // TODO: Step 2:
    // - If this node is not root: receive fully reduced data from parent into data_buffer.
    // - If this node has left child: send fully reduced data down to left child.
    // - If this node has right child: send fully reduced data down to right child.
    std::cout << "// TODO: TreeAllReduce::execute - Phase 2: Broadcast down to leaves\n";

    return {};
}

} // namespace adder::allreduce
