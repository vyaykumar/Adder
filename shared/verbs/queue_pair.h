#pragma once

#include <infiniband/verbs.h>
#include <shared/verbs/protection_domain.h>
#include <shared/verbs/completion_queue.h>
#include <expected>
#include <string>
#include <cstdint>
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
/// Encapsulates `ibv_create_qp()` and guarantees cleanup via `ibv_destroy_qp()`.
/// Also exposes zero-overhead `inline` methods for posting Send and Recv Work Requests.
///
/// \see docs/man/02_queue_pairs_and_transports.md (RC vs UD, QP Lifecycle)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_create_qp / ibv_post_send)
class QueuePair {
public:
    /// \brief Creates a Queue Pair within the given Protection Domain and associated CQs.
    /// \param pd Raw Protection Domain pointer.
    /// \param send_cq CQ for send work completions.
    /// \param recv_cq CQ for receive work completions.
    /// \param cfg Queue Pair configuration attributes.
    /// \return Managed QueuePair or error string.
    [[nodiscard]] static std::expected<QueuePair, std::string> create(
        struct ibv_pd* pd,
        struct ibv_cq* send_cq,
        struct ibv_cq* recv_cq,
        const QpConfig& cfg = {}
    ) {
        // TODO: [Implement QP Creation]
        // 1. Verify pd != nullptr, send_cq != nullptr, recv_cq != nullptr.
        // 2. Populate `struct ibv_qp_init_attr init_attr{}`:
        //      init_attr.send_cq = send_cq;
        //      init_attr.recv_cq = recv_cq;
        //      init_attr.cap.max_send_wr = cfg.max_send_wr;
        //      init_attr.cap.max_recv_wr = cfg.max_recv_wr;
        //      init_attr.cap.max_send_sge = cfg.max_send_sge;
        //      init_attr.cap.max_recv_sge = cfg.max_recv_sge;
        //      init_attr.cap.max_inline_data = cfg.max_inline_data;
        //      init_attr.qp_type = cfg.qp_type;
        //      init_attr.sq_sig_all = cfg.sq_sig_all;
        // 3. Call `struct ibv_qp* qp = ibv_create_qp(pd, &init_attr);`
        // 4. If qp is nullptr, return unexpected with strerror(errno).
        // 5. Return QueuePair(qp, init_attr.cap.max_inline_data).
        (void)pd;
        (void)send_cq;
        (void)recv_cq;
        (void)cfg;
        return std::unexpected("TODO: Implement QueuePair::create");
    }

    /// \brief Overload accepting managed PD and CQ wrappers.
    [[nodiscard]] static std::expected<QueuePair, std::string> create(
        const ProtectionDomain& pd,
        const CompletionQueue& send_cq,
        const CompletionQueue& recv_cq,
        const QpConfig& cfg = {}
    ) {
        return create(pd.get(), send_cq.get(), recv_cq.get(), cfg);
    }

    /// \brief Destructor. Destroys the Queue Pair.
    ~QueuePair() {
        // TODO: [Implement QP Destruction]
        // Pitfall: ibv_destroy_qp() must be called BEFORE destroying the associated
        // CQs and PD, otherwise the kernel verbs subsystem returns EBUSY.
        // If qp_ != nullptr, call ibv_destroy_qp(qp_) and set qp_ = nullptr.
    }

    // Move-only semantics
    QueuePair(const QueuePair&) = delete;
    QueuePair& operator=(const QueuePair&) = delete;

    QueuePair(QueuePair&& other) noexcept 
        : qp_(other.qp_), actual_inline_data_(other.actual_inline_data_) {
        other.qp_ = nullptr;
        other.actual_inline_data_ = 0;
    }

    QueuePair& operator=(QueuePair&& other) noexcept {
        if (this != &other) {
            // TODO: [Implement Move Assignment]
            // 1. Destroy existing qp_ via ibv_destroy_qp() if non-null.
            // 2. Transfer qp_ and actual_inline_data_ from other.
            // 3. Reset other.qp_ = nullptr and other.actual_inline_data_ = 0.
        }
        return *this;
    }

    // =========================================================================
    // Hot-Path Work Request Posting (Zero-Overhead Direct Verbs Call)
    // =========================================================================

    /// \brief Post a linked list of Send Work Requests to the Send Queue.
    /// \param wr Pointer to head of `ibv_send_wr` chain.
    /// \param bad_wr Output pointer set to the failing WR if posting fails.
    /// \return 0 on success, or positive errno on failure.
    [[nodiscard]] inline int post_send(struct ibv_send_wr* wr, struct ibv_send_wr** bad_wr) noexcept {
        // TODO: Call `ibv_post_send(qp_, wr, bad_wr)` directly.
        (void)wr;
        (void)bad_wr;
        return 0;
    }

    /// \brief Post a linked list of Receive Work Requests to the Receive Queue.
    /// \param wr Pointer to head of `ibv_recv_wr` chain.
    /// \param bad_wr Output pointer set to the failing WR if posting fails.
    /// \return 0 on success, or positive errno on failure.
    [[nodiscard]] inline int post_recv(struct ibv_recv_wr* wr, struct ibv_recv_wr** bad_wr) noexcept {
        // TODO: Call `ibv_post_recv(qp_, wr, bad_wr)` directly.
        (void)wr;
        (void)bad_wr;
        return 0;
    }

    /// \brief Returns the 24-bit Queue Pair Number assigned by hardware.
    [[nodiscard]] uint32_t qp_num() const noexcept {
        // TODO: Return qp_->qp_num if qp_ != nullptr, else 0
        return 0;
    }

    /// \brief Actual inline data size granted by the HCA driver.
    [[nodiscard]] uint32_t actual_inline_data() const noexcept { return actual_inline_data_; }

    /// \brief Raw pointer accessor.
    [[nodiscard]] struct ibv_qp* get() const noexcept { return qp_; }
    [[nodiscard]] explicit operator bool() const noexcept { return qp_ != nullptr; }

private:
    explicit QueuePair(struct ibv_qp* qp, uint32_t inline_bytes) 
        : qp_(qp), actual_inline_data_(inline_bytes) {}
    struct ibv_qp* qp_{nullptr};
    uint32_t actual_inline_data_{0};
};

} // namespace adder::verbs
