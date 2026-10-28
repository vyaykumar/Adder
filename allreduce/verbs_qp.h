#pragma once

#include <infiniband/verbs.h>
#include <string>
#include <string_view>
#include <expected>
#include <memory>
#include <cstdint>
#include <vector>

namespace adder::allreduce {

struct QpConnectionParams {
    uint32_t qpn{0};
    uint16_t lid{0};
    uint32_t psn{0};
    union ibv_gid gid{};
};

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

    // Initializes the device, PD, CQ, and QP
    std::expected<void, std::string> init(
        std::string_view dev_name,
        uint8_t ib_port,
        int cq_depth = 256,
        int max_send_wr = 128,
        int max_recv_wr = 128
    );

    // Transitions QP from RESET -> INIT
    std::expected<void, std::string> transition_to_init(int pkey_index = 0);

    // Transitions QP from INIT -> RTR (Ready to Receive)
    std::expected<void, std::string> transition_to_rtr(
        const QpConnectionParams& remote_params,
        enum ibv_mtu path_mtu = IBV_MTU_1024,
        uint8_t min_rnr_timer = 12 // Prevents RNR retry thrashing
    );

    // Transitions QP from RTR -> RTS (Ready to Send)
    std::expected<void, std::string> transition_to_rts(
        uint32_t local_send_psn,
        uint8_t timeout = 14,
        uint8_t retry_cnt = 7,
        uint8_t rnr_retry = 7
    );

    // Track resources associated with this PD so they are safely destroyed before ibv_dealloc_pd
    void track_mr(struct ibv_mr* mr) { if (mr) tracked_mrs_.push_back(mr); }
    void track_ah(struct ibv_ah* ah) { if (ah) tracked_ahs_.push_back(ah); }

    // Getters for local connection negotiation
    QpConnectionParams get_local_params() const;

    struct ibv_context* context() const noexcept { return ctx_; }
    struct ibv_pd* pd() const noexcept { return pd_; }
    struct ibv_cq* cq() const noexcept { return cq_; }
    struct ibv_qp* qp() const noexcept { return qp_; }
    uint8_t ib_port() const noexcept { return ib_port_; }

private:
    struct ibv_context* ctx_{nullptr};
    struct ibv_pd* pd_{nullptr};
    struct ibv_cq* cq_{nullptr};
    struct ibv_qp* qp_{nullptr};
    std::vector<struct ibv_mr*> tracked_mrs_;
    std::vector<struct ibv_ah*> tracked_ahs_;
    uint8_t ib_port_{1};
    uint16_t local_lid_{0};
    union ibv_gid local_gid_{};
    uint32_t local_psn_{0};
};

} // namespace adder::allreduce
