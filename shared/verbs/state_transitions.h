#pragma once

#include <cerrno>
#include <cstdint>
#include <expected>
#include <format>
#include <infiniband/verbs.h>
#include <string>
#include <system_error>

namespace adder::verbs {

/// \brief Endpoint parameters exchanged out-of-band across peers (via TCP or rank discovery).
struct RemoteEndpoint {
    uint32_t qpn{0};        ///< Remote Queue Pair Number (24 bits)
    uint16_t lid{0};        ///< Subnet Local Identifier (0 for RoCE)
    uint32_t psn{0};        ///< Packet Sequence Number starting seed
    union ibv_gid gid{};    ///< Global Identifier (IPv6 / RoCEv2 mapped IPv4)
    uint64_t vaddr{0};      ///< Remote buffer virtual address (for one-sided RDMA)
    uint32_t rkey{0};       ///< Remote memory key (for one-sided RDMA)
};

/// \brief Utilities for configuring QP state machine transitions (RESET -> INIT -> RTR -> RTS).
///
/// \see docs/man/02_queue_pairs_and_transports.md Section 2 (State Machine Transitions)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_modify_qp)
namespace qp_state {

// =============================================================================
// Reliable Connection (RC) Transitions
// =============================================================================

/// \brief Transitions an RC Queue Pair from RESET -> INIT.
[[nodiscard]] inline std::expected<void, std::string> transition_rc_to_init(
    struct ibv_qp* qp,
    uint8_t port_num = 1,
    int pkey_index = 0,
    unsigned access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
) {
    if (!qp) return std::unexpected("[StateTransistions::RC_to_INIT] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = static_cast<uint16_t>(pkey_index);
    attr.port_num = port_num;
    attr.qp_access_flags = access_flags;

    int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        int err = ret > 0 ? ret : errno;
        return std::unexpected(
            std::format("[StateTransistions::RC_to_INIT] Failed to transition QueuePair {} to INIT: {} ({})",
                qp->qp_num, std::generic_category().message(err), err
            )
        );
    }
    return {};
}

/// \brief Transitions an RC Queue Pair from INIT -> RTR (Ready to Receive).
[[nodiscard]] inline std::expected<void, std::string> transition_rc_to_rtr(
    struct ibv_qp* qp,
    const RemoteEndpoint& remote,
    uint8_t port_num = 1,
    int gid_index = 1,
    enum ibv_mtu path_mtu = IBV_MTU_1024,
    uint8_t min_rnr_timer = 12
) {
    if (!qp) return std::unexpected("[StateTransistions::RC_to_RTR] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = path_mtu;
    attr.dest_qp_num = remote.qpn;
    attr.rq_psn = remote.psn;
    attr.max_dest_rd_atomic = 1;
    attr.min_rnr_timer = min_rnr_timer;

    attr.ah_attr.is_global = 1;
    attr.ah_attr.port_num = port_num;
    attr.ah_attr.dlid = remote.lid;
    attr.ah_attr.grh.dgid = remote.gid;
    attr.ah_attr.grh.sgid_index = static_cast<uint8_t>(gid_index);
    attr.ah_attr.grh.hop_limit = 64;

    int mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
               IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        int err = (ret > 0) ? ret : errno;
        return std::unexpected(
            std::format("[StateTransistions::RC_to_RTR] Failed to transition QueuePair {} to RTR (remote QPN = {}): {} ({})",
                qp->qp_num, remote.qpn, std::generic_category().message(err), err
            )
        );
    }
    return {};
}

/// \brief Transitions an RC Queue Pair from RTR -> RTS (Ready to Send).
[[nodiscard]] inline std::expected<void, std::string> transition_rc_to_rts(
    struct ibv_qp* qp,
    uint32_t local_send_psn,
    uint8_t timeout = 14,
    uint8_t retry_cnt = 7,
    uint8_t rnr_retry = 7
) {
    if (!qp) return std::unexpected("[StateTransistions::RC_to_RTS] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = timeout;
    attr.retry_cnt = retry_cnt;
    attr.rnr_retry = rnr_retry;
    attr.sq_psn = local_send_psn;
    attr.max_rd_atomic = 1;

    int mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
               IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        int err = (ret > 0) ? ret : errno;
        return std::unexpected(
            std::format("[StateTransitions::RC_to_RTS] Failed to transition QueuePair {} to RTS: {} ({})",
                qp->qp_num, std::generic_category().message(err), err
            )
        );
    }
    return {};

}

// =============================================================================
// Unreliable Datagram (UD) Transitions
// =============================================================================

/// \brief Transitions a UD Queue Pair from RESET -> INIT.
[[nodiscard]] inline std::expected<void, std::string> transition_ud_to_init(
    struct ibv_qp* qp,
    uint8_t port_num = 1,
    uint32_t qkey = 0x11111111,
    int pkey_index = 0
) {
    if (!qp) return std::unexpected("[StateTransitions::UD_to_INIT] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = static_cast<uint16_t>(pkey_index);
    attr.port_num = port_num;
    attr.qkey = qkey;

    int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        int err = ret > 0 ? ret : errno;
        return std::unexpected(
            std::format("[StateTransitions::UD_to_INIT] Failed to transition UD QueuePair {} to INIT: {} ({})",
                qp->qp_num, std::generic_category().message(err), err
            )
        );
    }
    return {};
}

/// \brief Transitions a UD Queue Pair from INIT -> RTR.
[[nodiscard]] inline std::expected<void, std::string> transition_ud_to_rtr(struct ibv_qp* qp) {
    if (!qp) return std::unexpected("[StateTransitions::UD_to_RTR] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTR;

    int ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE);
    if (ret != 0) {
        int err = ret > 0 ? ret : errno;
        return std::unexpected(
            std::format("[StateTransitions::UD_to_RTR] Failed to transition UD QueuePair {} to RTR: {} ({})",
                qp->qp_num, std::generic_category().message(err), err
            )
        );
    }
    return {};
}

/// \brief Transitions a UD Queue Pair from RTR -> RTS.
[[nodiscard]] inline std::expected<void, std::string> transition_ud_to_rts(
    struct ibv_qp* qp,
    uint32_t local_send_psn
) {
    if (!qp) return std::unexpected("[StateTransitions::UD_to_RTS] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = local_send_psn;

    int mask = IBV_QP_STATE | IBV_QP_SQ_PSN;
    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0) {
        int err = ret > 0 ? ret : errno;
        return std::unexpected(
            std::format("[StateTransitions::UD_to_RTS] Failed to transition UD QueuePair {} to RTS: {} ({})",
                qp->qp_num, std::generic_category().message(err), err)
        );
    }
    return {};
}

// =============================================================================
// Error and Teardown Transitions
// =============================================================================

/// \brief Transitions any QP to the ERROR state to flush all outstanding WRs.
[[nodiscard]] inline std::expected<void, std::string> transition_to_error(struct ibv_qp* qp) {
    if (!qp) return std::unexpected("[StateTransitions::to_ERROR] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_ERR;

    int ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE);
    if (ret != 0) {
        int err = ret > 0 ? ret : errno;
        return std::unexpected(
            std::format("[StateTransitions::to_ERROR] Failed to transition QueuePair {} to ERROR: {} ({})",
                qp->qp_num, std::generic_category().message(err), err)
        );
    }
    return {};
}

/// \brief Resets a QP back to the RESET state.
[[nodiscard]] inline std::expected<void, std::string> transition_to_reset(struct ibv_qp* qp) {
    if (!qp) return std::unexpected("[StateTransitions::to_RESET] QueuePair pointer is null");

    struct ibv_qp_attr attr{};
    attr.qp_state = IBV_QPS_RESET;

    int ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE);
    if (ret != 0) {
        int err = (ret > 0) ? ret : errno;
        return std::unexpected(
            std::format("[StateTransitions::to_RESET] Failed to transition QP {} to RESET: {} ({})",
                        qp->qp_num, std::generic_category().message(err), err)
        );
    }
    return {};
}

} // namespace qp_state
} // namespace adder::verbs
