#pragma once

#include <infiniband/verbs.h>
#include <shared/verbs/context.h>
#include <expected>
#include <string>
#include <span>
#include <system_error>

namespace adder::verbs {

/// \brief RAII wrapper for an RDMA Completion Queue (`ibv_cq`).
///
/// Encapsulates `ibv_create_cq()` and guarantees release via `ibv_destroy_cq()`.
/// Also exposes zero-overhead `inline` methods for hot-loop CQ polling.
///
/// \see docs/man/01_rdma_and_verbs_core.md Section 4 (Data Plane Architecture)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_create_cq / ibv_poll_cq)
class CompletionQueue {
public:
    /// \brief Creates a Completion Queue.
    /// \param ctx Raw verbs context.
    /// \param cqe_depth Minimum number of Work Completions the CQ can hold (e.g. 256 or 1024).
    /// \param cq_context Optional user context pointer.
    /// \param channel Optional completion event channel (nullptr for busy polling).
    /// \param comp_vector Completion vector index.
    /// \return Managed CompletionQueue or error string.
    [[nodiscard]] static std::expected<CompletionQueue, std::string> create(
        struct ibv_context* ctx,
        int cqe_depth = 256,
        void* cq_context = nullptr,
        struct ibv_comp_channel* channel = nullptr,
        int comp_vector = 0
    ) {
        // TODO: [Implement CQ Creation]
        // 1. Verify ctx != nullptr and cqe_depth > 0.
        // 2. Call `struct ibv_cq* cq = ibv_create_cq(ctx, cqe_depth, cq_context, channel, comp_vector);`
        // 3. If cq is nullptr, return unexpected with strerror(errno).
        // 4. Return CompletionQueue(cq, cqe_depth).
        (void)ctx;
        (void)cqe_depth;
        (void)cq_context;
        (void)channel;
        (void)comp_vector;
        return std::unexpected("TODO: Implement CompletionQueue::create");
    }

    /// \brief Overload accepting managed Context.
    [[nodiscard]] static std::expected<CompletionQueue, std::string> create(
        const Context& ctx,
        int cqe_depth = 256
    ) {
        return create(ctx.get(), cqe_depth);
    }

    /// \brief Destructor. Destroys the Completion Queue.
    ~CompletionQueue() {
        // TODO: [Implement CQ Destruction]
        // Pitfall: ibv_destroy_cq() will fail if any Queue Pair (QP) is still associated
        // with this CQ! All QPs must be destroyed before their corresponding CQs.
        // If cq_ != nullptr, call ibv_destroy_cq(cq_) and set cq_ = nullptr.
    }

    // Move-only semantics
    CompletionQueue(const CompletionQueue&) = delete;
    CompletionQueue& operator=(const CompletionQueue&) = delete;

    CompletionQueue(CompletionQueue&& other) noexcept 
        : cq_(other.cq_), depth_(other.depth_) {
        other.cq_ = nullptr;
        other.depth_ = 0;
    }

    CompletionQueue& operator=(CompletionQueue&& other) noexcept {
        if (this != &other) {
            // TODO: [Implement Move Assignment]
            // 1. Destroy existing cq_ via ibv_destroy_cq() if non-null.
            // 2. Transfer cq_ and depth_ from other.
            // 3. Reset other.cq_ = nullptr and other.depth_ = 0.
        }
        return *this;
    }

    // =========================================================================
    // Hot-Path Polling Interface (Zero Virtual Dispatch, Direct Inline Verbs)
    // =========================================================================

    /// \brief Poll the CQ for completions directly into an array.
    /// \param num_entries Maximum completions to retrieve.
    /// \param wc_array Pointer to output array of `ibv_wc`.
    /// \return Number of completions reaped (>= 0), or negative value on driver failure.
    [[nodiscard]] inline int poll(int num_entries, struct ibv_wc* wc_array) noexcept {
        // TODO: Call `ibv_poll_cq(cq_, num_entries, wc_array)` directly.
        (void)num_entries;
        (void)wc_array;
        return 0;
    }

    /// \brief Poll a single Work Completion from the CQ.
    /// \param wc Output reference to `ibv_wc`.
    /// \return True if a completion was reaped, false if CQ was empty.
    [[nodiscard]] inline bool poll_one(struct ibv_wc& wc) noexcept {
        // TODO: Call `ibv_poll_cq(cq_, 1, &wc)` and return true if result == 1.
        (void)wc;
        return false;
    }

    /// \brief Batch poll using modern C++ std::span.
    /// \param batch Span of pre-allocated `ibv_wc` elements.
    /// \return Number of completions reaped.
    [[nodiscard]] inline int poll_batch(std::span<struct ibv_wc> batch) noexcept {
        // TODO: Call `ibv_poll_cq(cq_, static_cast<int>(batch.size()), batch.data())`.
        (void)batch;
        return 0;
    }

    /// \brief Configured depth of the CQ.
    [[nodiscard]] int depth() const noexcept { return depth_; }

    /// \brief Raw pointer accessor.
    [[nodiscard]] struct ibv_cq* get() const noexcept { return cq_; }
    [[nodiscard]] explicit operator bool() const noexcept { return cq_ != nullptr; }

private:
    explicit CompletionQueue(struct ibv_cq* cq, int depth) : cq_(cq), depth_(depth) {}
    struct ibv_cq* cq_{nullptr};
    int depth_{0};
};

} // namespace adder::verbs
