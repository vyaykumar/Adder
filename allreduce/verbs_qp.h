#pragma once

#include <infiniband/verbs.h>
#include <shared/verbs.h>
#include <string>
#include <string_view>
#include <expected>
#include <memory>
#include <cstdint>
#include <vector>

namespace adder::allreduce {

// Endpoint connection parameters exchanged out-of-band between collective ranks.
// Mirrors adder::verbs::RemoteEndpoint so D1 and D2 share the exact same wire mental model.
using QpConnectionParams = adder::verbs::RemoteEndpoint;

// =============================================================================
// Peer Link Queue Pair Wrapper (Shared PD / Shared CQ Architecture)
// Goal: Represent a single point-to-point Queue Pair link in a Ring or Tree
// collective while sharing the rank's single ProtectionDomain and CompletionQueue.
// =============================================================================
//
// Q: Why does VerbsQp borrow a shared ProtectionDomain instead of allocating its own?
// A: In a Ring (2 QPs: next, prev) or Tree (up to 3 QPs: parent, left, right),
//    all QPs read/write the SAME registered tensor MemoryRegion (ibv_mr).
//    Because an ibv_mr is bound to a single ibv_pd, all peer QPs on a rank
//    MUST be created under that same ProtectionDomain to avoid IBV_WC_LOC_PROT_ERR.
//
// RAII wrapper for RDMA Verbs Protection Domain, CQ, and Queue Pair
class VerbsQp {
public:
    VerbsQp() = default;
    ~VerbsQp();

    // Non-copyable, movable
    VerbsQp(const VerbsQp&) = delete;
    VerbsQp& operator=(const VerbsQp&) = delete;
    VerbsQp(VerbsQp&& other) noexcept;
    VerbsQp& operator=(VerbsQp&& other) noexcept;

    // Explicit destruction method
    void destroy() noexcept;

    // Primary D2 Initializer: Creates a QueuePair attached to the rank's shared
    // Context, ProtectionDomain, and CompletionQueue (reusing shared/verbs RAII).
    [[nodiscard]] std::expected<void, std::string> init(
        const verbs::Context& ctx,
        const verbs::ProtectionDomain& pd,
        const verbs::CompletionQueue& cq,
        uint8_t ib_port = 1,
        int gid_index = 1,
        const verbs::QpConfig& cfg = {}
    );

    // Legacy standalone initializer (allocates isolated Context/PD/CQ; only for single-link tests)
    [[nodiscard]] std::expected<void, std::string> init(
        std::string_view dev_name,
        uint8_t ib_port,
        int cq_depth = 256,
        int max_send_wr = 128,
        int max_recv_wr = 128
    );

    // Transitions QP from RESET -> INIT (wraps verbs::qp_state::transition_rc_to_init)
    [[nodiscard]] std::expected<void, std::string> transition_to_init(int pkey_index = 0);

    // Transitions QP from INIT -> RTR (Ready to Receive; wraps verbs::qp_state::transition_rc_to_rtr)
    [[nodiscard]] std::expected<void, std::string> transition_to_rtr(
        const QpConnectionParams& remote_params,
        enum ibv_mtu path_mtu = IBV_MTU_1024,
        uint8_t min_rnr_timer = 12 // Prevents RNR retry thrashing
    );

    // Transitions QP from RTR -> RTS (Ready to Send; wraps verbs::qp_state::transition_rc_to_rts)
    [[nodiscard]] std::expected<void, std::string> transition_to_rts(
        uint32_t local_send_psn,
        uint8_t timeout = 14,
        uint8_t retry_cnt = 7,
        uint8_t rnr_retry = 7
    );

    // Monadic full pipeline: RESET -> INIT -> RTR -> RTS (mirrors D1 pingpong Step 7)
    [[nodiscard]] std::expected<void, std::string> connect_rc(
        const QpConnectionParams& remote_params,
        enum ibv_mtu path_mtu = IBV_MTU_1024
    );

    // =========================================================================
    // Hot-Path Data Plane Forwarding (Zero-Overhead Inline Verbs)
    // =========================================================================
    [[nodiscard]] inline int post_send(struct ibv_send_wr* wr, struct ibv_send_wr** bad_wr) noexcept {
        return qp_ ? ibv_post_send(qp_, wr, bad_wr) : -1;
    }

    [[nodiscard]] inline int post_recv(struct ibv_recv_wr* wr, struct ibv_recv_wr** bad_wr) noexcept {
        return qp_ ? ibv_post_recv(qp_, wr, bad_wr) : -1;
    }

    // Track resources associated with this PD so they are safely destroyed before ibv_dealloc_pd
    void track_mr(struct ibv_mr* mr) { if (mr) tracked_mrs_.push_back(mr); }
    void track_ah(struct ibv_ah* ah) { if (ah) tracked_ahs_.push_back(ah); }

    // Getters for local connection negotiation
    [[nodiscard]] QpConnectionParams get_local_params() const noexcept;

    [[nodiscard]] struct ibv_context* context() const noexcept { return ctx_; }
    [[nodiscard]] struct ibv_pd* pd() const noexcept { return pd_; }
    [[nodiscard]] struct ibv_cq* cq() const noexcept { return cq_; }
    [[nodiscard]] struct ibv_qp* qp() const noexcept { return qp_; }
    [[nodiscard]] uint32_t qp_num() const noexcept { return qp_ ? qp_->qp_num : 0; }
    [[nodiscard]] uint8_t ib_port() const noexcept { return ib_port_; }
    [[nodiscard]] int gid_index() const noexcept { return gid_index_; }

private:
    // Underlying RAII QueuePair from shared/verbs (when initialized via shared PD)
    verbs::QueuePair owned_qp_{};

    // Raw handles (non-owning when borrowing shared Context/PD/CQ; owning only in legacy standalone mode)
    struct ibv_context* ctx_{nullptr};
    struct ibv_pd* pd_{nullptr};
    struct ibv_cq* cq_{nullptr};
    struct ibv_qp* qp_{nullptr};
    bool owns_shared_resources_{false};

    std::vector<struct ibv_mr*> tracked_mrs_;
    std::vector<struct ibv_ah*> tracked_ahs_;
    uint8_t ib_port_{1};
    int gid_index_{1};
    uint16_t local_lid_{0};
    union ibv_gid local_gid_{};
    uint32_t local_psn_{0};
};

} // namespace adder::allreduce
