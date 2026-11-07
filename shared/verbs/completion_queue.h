#pragma once

#include <cerrno>
#include <expected>
#include <format>
#include <infiniband/verbs.h>
#include <shared/verbs/context.h>
#include <span>
#include <string>
#include <system_error>

namespace adder::verbs {

/// \brief RAII wrapper for an RDMA Completion Queue (`ibv_cq`).
///
/// \see docs/man/01_rdma_and_verbs_core.md Section 4 (Data Plane Architecture)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_create_cq / ibv_poll_cq)
class CompletionQueue {
public:
    [[nodiscard]] static std::expected<CompletionQueue, std::string> create(
        struct ibv_context* ctx,
        int cqe_depth = 256,
        void* cq_context = nullptr,
        struct ibv_comp_channel* channel = nullptr,
        int comp_vector = 0
    ) {
        if (!ctx) return std::unexpected("[CompletionQueue::create] Device context pointer is null");
        if (cqe_depth <= 0) return std::unexpected(
            std::format("[CompletionQueue::create] Invalid CQ depth ({}); must be positive", cqe_depth)
        );

        struct ibv_cq* cq = ibv_create_cq(ctx, cqe_depth, cq_context, channel, comp_vector);
        if (!cq) {
            int err = errno;
            auto device_name = (ctx->device) ? ibv_get_device_name(ctx->device) : "unknown";
            return std::unexpected(
                std::format("[CompletionQueue::create] Failed to create CQ (depth = {}) on device '{}': {} ({})",
                    cqe_depth,
                    device_name,
                    std::generic_category().message(err),
                    err
                )
            );
        }
        return CompletionQueue(cq, cqe_depth);
    }

    [[nodiscard]] static std::expected<CompletionQueue, std::string> create(
        const Context& ctx,
        int cqe_depth = 256
    ) {
        return create(ctx.get(), cqe_depth);
    }

    ~CompletionQueue() {
        if (cq_) ibv_destroy_cq(cq_);
        cq_ = nullptr;
    }

    CompletionQueue(const CompletionQueue&) = delete;
    CompletionQueue& operator=(const CompletionQueue&) = delete;

    CompletionQueue(CompletionQueue&& other) noexcept
        : cq_(other.cq_), depth_(other.depth_) {
        other.cq_ = nullptr;
        other.depth_ = 0;
    }

    CompletionQueue& operator=(CompletionQueue&& other) noexcept {
        if (this != &other) {
            if (cq_) ibv_destroy_cq(cq_);
            cq_ = other.cq_;
            depth_ = other.depth_;

            other.cq_ = nullptr;
            other.depth_ = 0;
        }
        return *this;
    }

    // =========================================================================
    // Hot-Path Polling Interface (Zero Virtual Dispatch, Direct Inline Verbs)
    // =========================================================================

    [[nodiscard]] inline int poll(int num_entries, struct ibv_wc* wc_array) noexcept {
        return ibv_poll_cq(cq_, num_entries, wc_array);
    }

    [[nodiscard]] inline bool poll_one(struct ibv_wc& wc) noexcept {
        return ibv_poll_cq(cq_, 1, &wc) == 1;
    }

    [[nodiscard]] inline int poll_batch(std::span<struct ibv_wc> batch) noexcept {
        return ibv_poll_cq(cq_, static_cast<int>(batch.size()), batch.data());
    }

    [[nodiscard]] int depth() const noexcept { return depth_; }

    [[nodiscard]] struct ibv_cq* get() const noexcept { return cq_; }
    [[nodiscard]] explicit operator bool() const noexcept { return cq_ != nullptr; }

private:
    explicit CompletionQueue(struct ibv_cq* cq, int depth) : cq_(cq), depth_(depth) {}
    struct ibv_cq* cq_{nullptr};
    int depth_{0};
};

} // namespace adder::verbs
