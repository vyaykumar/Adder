#pragma once

#include <cerrno>
#include <cstdint>
#include <expected>
#include <format>
#include <infiniband/verbs.h>
#include <shared/verbs/completion_queue.h>
#include <shared/verbs/protection_domain.h>
#include <string>
#include <system_error>

namespace adder::verbs {

/// \brief Configuration parameters for RDMA Queue Pair creation.
struct QpConfig {
    enum ibv_qp_type qp_type{IBV_QPT_RC}; // IBV_QPT_RC or IBV_QPT_UD
    uint32_t max_send_wr{128};
    uint32_t max_recv_wr{128};
    uint32_t max_send_sge{1};
    uint32_t max_recv_sge{1};
    uint32_t max_inline_data{0}; // Set to e.g. 64 or 256 for IBV_SEND_INLINE
    int sq_sig_all{0};           // 1 to generate CQE for every Send WR; 0 for selective signaling
};

/// \brief RAII wrapper for an RDMA Queue Pair (`ibv_qp`).
///
/// \see docs/man/02_queue_pairs_and_transports.md (RC vs UD, QP Lifecycle)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_create_qp / ibv_post_send)
class QueuePair {
public:
    [[nodiscard]] static std::expected<QueuePair, std::string> create(
        struct ibv_pd* pd,
        struct ibv_cq* send_cq,
        struct ibv_cq* recv_cq,
        const QpConfig& cfg = {}
    ) {
        if (!pd)
            return std::unexpected("[QueuePair::create] ProtectionDomain pointer is null");
        if (!send_cq)
            return std::unexpected("[QueuePair::create] Send CompletionQueue pointer is null");
        if (!recv_cq)
            return std::unexpected("[QueuePair::create] Receive CompletionQueue pointer is null");

        struct ibv_qp_init_attr init_attr{
                    .qp_context = nullptr,
                    .send_cq = send_cq,
                    .recv_cq = recv_cq,
                    .srq = nullptr,
                    .cap = {
                        .max_send_wr = cfg.max_send_wr,
                        .max_recv_wr = cfg.max_recv_wr,
                        .max_send_sge = cfg.max_send_sge,
                        .max_recv_sge = cfg.max_recv_sge,
                        .max_inline_data = cfg.max_inline_data,
                    },
                    .qp_type = cfg.qp_type,
                    .sq_sig_all = cfg.sq_sig_all,
                };

        struct ibv_qp* qp = ibv_create_qp(pd, &init_attr);
        if (!qp) {
            int err = errno;
            return std::unexpected(
                std::format("[QueuePair::create] Failed to create QueuePair (type = {}): {} ({})",
                    static_cast<int>(cfg.qp_type),
                    std::generic_category().message(err),
                    err
                )
            );
        }

        return QueuePair(qp, init_attr.cap.max_inline_data);
    }

    [[nodiscard]] static std::expected<QueuePair, std::string> create(
        const ProtectionDomain& pd,
        const CompletionQueue& send_cq,
        const CompletionQueue& recv_cq,
        const QpConfig& cfg = {}
    ) {
        return create(pd.get(), send_cq.get(), recv_cq.get(), cfg);
    }

    ~QueuePair() {
        if (qp_) ibv_destroy_qp(qp_);
        qp_ = nullptr;
    }

    QueuePair(const QueuePair&) = delete;
    QueuePair& operator=(const QueuePair&) = delete;

    QueuePair(QueuePair&& other) noexcept
        : qp_(other.qp_), actual_inline_data_(other.actual_inline_data_) {
        other.qp_ = nullptr;
        other.actual_inline_data_ = 0;
    }

    QueuePair& operator=(QueuePair&& other) noexcept {
        if (this != &other) {
            if (qp_) ibv_destroy_qp(qp_);
            qp_ = other.qp_;
            actual_inline_data_ = other.actual_inline_data_;
            other.qp_ = nullptr;
            other.actual_inline_data_ = 0;
        }
        return *this;
    }

    // =========================================================================
    // Hot-Path Work Request Posting (Zero-Overhead Direct Verbs Call)
    // =========================================================================

    [[nodiscard]] inline int post_send(struct ibv_send_wr* wr, struct ibv_send_wr** bad_wr) noexcept {
        return ibv_post_send(qp_, wr, bad_wr);
    }

    [[nodiscard]] inline int post_recv(struct ibv_recv_wr* wr, struct ibv_recv_wr** bad_wr) noexcept {
        return ibv_post_recv(qp_, wr, bad_wr);
    }

    [[nodiscard]] uint32_t qp_num() const noexcept {
        return qp_ ? qp_->qp_num : 0;
    }

    [[nodiscard]] uint32_t actual_inline_data() const noexcept { return actual_inline_data_; }

    [[nodiscard]] struct ibv_qp* get() const noexcept { return qp_; }
    [[nodiscard]] explicit operator bool() const noexcept { return qp_ != nullptr; }

    // =========================================================================
    // D2 Extension: High-Level RC State Machine Progression Helper
    // Goal: Allow a QueuePair to transition RESET -> INIT -> RTR -> RTS in one
    // monadic pipeline using RemoteEndpoint (see state_transitions.h).
    // =========================================================================
    // TODO: Implement connect_rc(const RemoteEndpoint& remote, uint32_t local_psn, ...)
    // by chaining qp_state::transition_rc_to_init -> transition_rc_to_rtr -> transition_rc_to_rts.

private:
    explicit QueuePair(struct ibv_qp* qp, uint32_t inline_bytes)
        : qp_(qp), actual_inline_data_(inline_bytes) {}
    struct ibv_qp* qp_{nullptr};
    uint32_t actual_inline_data_{0};
};

} // namespace adder::verbs
