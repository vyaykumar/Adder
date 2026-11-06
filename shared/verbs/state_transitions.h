#pragma once

#include <infiniband/verbs.h>
#include <expected>
#include <string>
#include <cstdint>
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
/// Encapsulates the mandatory bitmask flags and address vector configurations required
/// by `ibv_modify_qp()`, preventing common `EINVAL` (code 22) errors from missing flags.
///
/// \see docs/man/02_queue_pairs_and_transports.md Section 2 (State Machine Transitions)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_modify_qp)
namespace qp_state {

// =============================================================================
// Reliable Connection (RC) Transitions
// =============================================================================

/// \brief Transitions an RC Queue Pair from RESET -> INIT.
/// \param qp Target Queue Pair.
/// \param port_num 1-based local port number.
/// \param pkey_index Partition key index (usually 0).
/// \param access_flags Allowed remote access operations (LOCAL_WRITE, REMOTE_WRITE, REMOTE_READ).
/// \return Success or error description.
[[nodiscard]] inline std::expected<void, std::string> transition_rc_to_init(
    struct ibv_qp* qp,
    uint8_t port_num = 1,
    int pkey_index = 0,
    int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
) {
    // TODO: [Implement RC RESET -> INIT Transition]
    // 1. Verify qp != nullptr.
    // 2. Initialize `struct ibv_qp_attr attr{}`.
    // 3. Set:
    //      attr.qp_state = IBV_QPS_INIT;
    //      attr.pkey_index = static_cast<uint16_t>(pkey_index);
    //      attr.port_num = port_num;
    //      attr.qp_access_flags = access_flags;
    // 4. Construct bitmask:
    //      int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;
    // 5. Call `int ret = ibv_modify_qp(qp, &attr, mask);`
    // 6. If ret != 0, return unexpected with strerror(ret).
    // 7. Return {};
    if (!qp) return std::unexpected("Null parameter");

    struct ibv_qp_attr attr{
        .qp_state = IBV_QPS_INIT,
        .pkey_index = static_cast<uint16_t>(pkey_index),
        .port_num = port_num,
        .qp_access_flags = access_flags
    };
    int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0)
        return std::unexpected(
            std::format("Failed to INIT QueuePair: {} ({})", streror(errno), errno)
        );
    return {};
}

/// \brief Transitions an RC Queue Pair from INIT -> RTR (Ready to Receive).
/// \param qp Target Queue Pair.
/// \param remote Connection details of the remote peer.
/// \param port_num 1-based local port number.
/// \param gid_index Local GID index (1 for RoCEv2 IPv4).
/// \param path_mtu Negotiated Path MTU (e.g. IBV_MTU_1024).
/// \param min_rnr_timer Minimum RNR NAK timer (12 = ~0.64 ms).
/// \return Success or error description.
[[nodiscard]] inline std::expected<void, std::string> transition_rc_to_rtr(
    struct ibv_qp* qp,
    const RemoteEndpoint& remote,
    uint8_t port_num = 1,
    int gid_index = 1,
    enum ibv_mtu path_mtu = IBV_MTU_1024,
    uint8_t min_rnr_timer = 12
) {
    // TODO: [Implement RC INIT -> RTR Transition]
    // 1. Verify qp != nullptr.
    // 2. Initialize `struct ibv_qp_attr attr{}`.
    // 3. Set primary connection attributes:
    //      attr.qp_state = IBV_QPS_RTR;
    //      attr.path_mtu = path_mtu;
    //      attr.dest_qp_num = remote.qpn;
    //      attr.rq_psn = remote.psn;
    //      attr.max_dest_rd_atomic = 1;
    //      attr.min_rnr_timer = min_rnr_timer;
    // 4. Populate Address Vector (attr.ah_attr):
    //      attr.ah_attr.is_global = 1;
    //      attr.ah_attr.port_num = port_num;
    //      attr.ah_attr.dlid = remote.lid;
    //      attr.ah_attr.grh.dgid = remote.gid;
    //      attr.ah_attr.grh.sgid_index = static_cast<uint8_t>(gid_index);
    //      attr.ah_attr.grh.hop_limit = 64;
    // 5. Construct bitmask:
    //      int mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
    //                 IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    // 6. Call `int ret = ibv_modify_qp(qp, &attr, mask);`
    // 7. If ret != 0, return unexpected with strerror(ret).
    // 8. Return {};
    if (!qp) return std::unexpected("Null parameter");

    struct ibv_qp_attr attr{
        .qp_state = IBV_QPS_RTR,
        .path_mtu = path_mtu,
        .dest_qp_num = remote.qpn,
        .rq_psn = remote.psn,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer = min_rnr_timer,
        .ah_attr.is_global = 1,
        .ah_attr.port_num = port_num,
        .ah_attr.dlid = remote.lid,
        .ah_attr.grh.dgid = remote.gid,
        .ah_attr.grh.sgid_index = static_cast<uint8_t>(gid_index),
        .ah_attr.grh.hop_limit = 64,
    };
    int mask = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
               IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC  | IBV_QP_MIN_RNR_TIMER;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0)
        return std::unexpected(
            std::format("Failed to RTR QueuePair: {} ({})", streror(errno), errno)
        );
    return {};
}

/// \brief Transitions an RC Queue Pair from RTR -> RTS (Ready to Send).
/// \param qp Target Queue Pair.
/// \param local_send_psn Starting PSN seed for local send queue.
/// \param timeout Local ACK timeout exponent (14 = ~4.096 ms * 4 = 16.38 ms).
/// \param retry_cnt Hardware retry limit for unacknowledged packets before error.
/// \param rnr_retry Hardware retry limit for Receiver Not Ready before error.
/// \return Success or error description.
[[nodiscard]] inline std::expected<void, std::string> transition_rc_to_rts(
    struct ibv_qp* qp,
    uint32_t local_send_psn,
    uint8_t timeout = 14,
    uint8_t retry_cnt = 7,
    uint8_t rnr_retry = 7
) {
    // TODO: [Implement RC RTR -> RTS Transition]
    // 1. Verify qp != nullptr.
    // 2. Initialize `struct ibv_qp_attr attr{}`.
    // 3. Set:
    //      attr.qp_state = IBV_QPS_RTS;
    //      attr.timeout = timeout;
    //      attr.retry_cnt = retry_cnt;
    //      attr.rnr_retry = rnr_retry;
    //      attr.sq_psn = local_send_psn;
    //      attr.max_rd_atomic = 1;
    // 4. Construct bitmask:
    //      int mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
    //                 IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;
    // 5. Call `int ret = ibv_modify_qp(qp, &attr, mask);`
    // 6. If ret != 0, return unexpected with strerror(ret).
    // 7. Return {};
    if (!qp) return std::unexpected("Null parameter");

    struct ibv_qp_attr attr{
        .qp_state = IBV_QPS_RTS,
        .timeout = timeout,
        .retry_cnt = retry_cnt,
        .rnr_retry = rnr_retry,
        .sq_psn = local_send_psn,
        .max_rd_atomic = 1
    };
    int mask = IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
               IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC;

    int ret = ibv_modify_qp(qp, &attr, mask);
    if (ret != 0)
        return std::unexpected(
            std::format("Failed to RTS QueuePair: {} ({})", streror(errno), errno)
        );
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
    // TODO: [Implement UD RESET -> INIT Transition]
    // 1. Initialize `struct ibv_qp_attr attr{}` with IBV_QPS_INIT, pkey_index, port_num, qkey.
    // 2. Bitmask: IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY.
    // 3. Call `ibv_modify_qp(qp, &attr, mask)`.
    struct ibv_qp_attr attr{
        .qp_state = IBV_QPS_INIT,
        .pkey_index = static_cast<uint16_t>(pkey_index),
        .port_num = port_num,
        .qkey = qkey
    };
    int mask = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;
    ibv_modify_qp(qp, &attr, mask);
    // No error handling here?
}

/// \brief Transitions a UD Queue Pair from INIT -> RTR.
[[nodiscard]] inline std::expected<void, std::string> transition_ud_to_rtr(struct ibv_qp* qp) {
    // TODO: [Implement UD INIT -> RTR Transition]
    // 1. Set attr.qp_state = IBV_QPS_RTR; mask = IBV_QP_STATE.
    // 2. Call `ibv_modify_qp(qp, &attr, mask)`.
    struct ibv_qp_attr attr {
        .qp_state = IBV_QPS_RTR,
    }
    int mask = IBV_QP_STATE;
    ibv_modify_qp(qp, &attr, mask);
}

/// \brief Transitions a UD Queue Pair from RTR -> RTS.
[[nodiscard]] inline std::expected<void, std::string> transition_ud_to_rts(
    struct ibv_qp* qp,
    uint32_t local_send_psn
) {
    // TODO: [Implement UD RTR -> RTS Transition]
    // 1. Set attr.qp_state = IBV_QPS_RTS; attr.sq_psn = local_send_psn;
    // 2. Bitmask = IBV_QP_STATE | IBV_QP_SQ_PSN.
    // 3. Call `ibv_modify_qp(qp, &attr, mask)`.
    struct ibv_qp_attr attr {
        .qp_state = IBV_QPS_RTS,
        .sq_psn = local_send_psn,
    }
    int mask = IBV_QP_STATE | IBV_QP_SQ_PSN;
    ibv_modify_qp(qp, &attr, mask);
}

// =============================================================================
// Error and Teardown Transitions
// =============================================================================

/// \brief Transitions any QP to the ERROR state to flush all outstanding WRs.
[[nodiscard]] inline std::expected<void, std::string> transition_to_error(struct ibv_qp* qp) {
    // TODO: Set attr.qp_state = IBV_QPS_ERR; mask = IBV_QP_STATE; call ibv_modify_qp.
    struct ibv_qp_attr attr {
        .qp_state = IBV_QPS_ERR,
    }
    int mask = IBV_QP_STATE;
    ibv_modify_qp(qp, &attr, mask);
}

/// \brief Resets a QP back to the RESET state.
[[nodiscard]] inline std::expected<void, std::string> transition_to_reset(struct ibv_qp* qp) {
    // TODO: Set attr.qp_state = IBV_QPS_RESET; mask = IBV_QP_STATE; call ibv_modify_qp.
    struct ibv_qp_attr attr {
        .qp_state = IBV_QPS_RESET,
    }
    int mask = IBV_QP_STATE;
    ibv_modify_qp(qp, &attr, mask);
}

} // namespace qp_state
} // namespace adder::verbs
