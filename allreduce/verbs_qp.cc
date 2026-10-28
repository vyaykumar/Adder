#include "allreduce/verbs_qp.h"
#include <iostream>
#include <cstring>
#include <cerrno>

namespace adder::allreduce {

VerbsQp::~VerbsQp() {
    destroy();
}

void VerbsQp::destroy() noexcept {
    // 1. Destroy Queue Pair
    if (qp_) {
        int rc = ibv_destroy_qp(qp_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_destroy_qp failed (rc=" << rc << ")\n";
        }
        qp_ = nullptr;
    }

    // 2. Destroy Completion Queue
    if (cq_) {
        int rc = ibv_destroy_cq(cq_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_destroy_cq failed (rc=" << rc << ")\n";
        }
        cq_ = nullptr;
    }

    // 3. Deregister any Address Handles and Memory Regions associated with this PD
    for (auto* ah : tracked_ahs_) {
        if (ah) {
            int rc = ibv_destroy_ah(ah);
            if (rc != 0) {
                std::cerr << "[Warning] ibv_destroy_ah failed (rc=" << rc << ")\n";
            }
        }
    }
    tracked_ahs_.clear();

    for (auto* mr : tracked_mrs_) {
        if (mr) {
            int rc = ibv_dereg_mr(mr);
            if (rc != 0) {
                std::cerr << "[Warning] ibv_dereg_mr failed (rc=" << rc << ")\n";
            }
        }
    }
    tracked_mrs_.clear();

    // 4. Deallocate Protection Domain
    if (pd_) {
        int rc = ibv_dealloc_pd(pd_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_dealloc_pd failed (rc=" << rc << ", errno=" << errno << "): resource still registered to PD\n";
        }
        pd_ = nullptr;
    }

    // 5. Close device context
    if (ctx_) {
        int rc = ibv_close_device(ctx_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_close_device failed (rc=" << rc << ")\n";
        }
        ctx_ = nullptr;
    }
}

VerbsQp::VerbsQp(VerbsQp&& other) noexcept {
    ctx_ = other.ctx_; other.ctx_ = nullptr;
    pd_ = other.pd_; other.pd_ = nullptr;
    cq_ = other.cq_; other.cq_ = nullptr;
    qp_ = other.qp_; other.qp_ = nullptr;
    tracked_mrs_ = std::move(other.tracked_mrs_);
    tracked_ahs_ = std::move(other.tracked_ahs_);
    ib_port_ = other.ib_port_;
    local_lid_ = other.local_lid_;
    local_gid_ = other.local_gid_;
    local_psn_ = other.local_psn_;
}

VerbsQp& VerbsQp::operator=(VerbsQp&& other) noexcept {
    if (this != &other) {
        destroy();

        ctx_ = other.ctx_; other.ctx_ = nullptr;
        pd_ = other.pd_; other.pd_ = nullptr;
        cq_ = other.cq_; other.cq_ = nullptr;
        qp_ = other.qp_; other.qp_ = nullptr;
        tracked_mrs_ = std::move(other.tracked_mrs_);
        tracked_ahs_ = std::move(other.tracked_ahs_);
        ib_port_ = other.ib_port_;
        local_lid_ = other.local_lid_;
        local_gid_ = other.local_gid_;
        local_psn_ = other.local_psn_;
    }
    return *this;
}

std::expected<void, std::string> VerbsQp::init(
    std::string_view dev_name,
    uint8_t ib_port,
    int cq_depth,
    int max_send_wr,
    int max_recv_wr
) {
    ib_port_ = ib_port;
    local_psn_ = static_cast<uint32_t>(rand() & 0xffffff);

    // TODO: Step 1: Open RDMA device context
    // Enumerate with ibv_get_device_list(), match dev_name if provided, call ibv_open_device().
    std::cout << "// TODO: VerbsQp::init - ibv_open_device\n";

    // TODO: Step 2: Allocate Protection Domain
    // Call ibv_alloc_pd(ctx_).
    std::cout << "// TODO: VerbsQp::init - ibv_alloc_pd\n";

    // TODO: Step 3: Create Completion Queue
    // Call ibv_create_cq(ctx_, cq_depth, nullptr, nullptr, 0).
    std::cout << "// TODO: VerbsQp::init - ibv_create_cq\n";

    // TODO: Step 4: Create Queue Pair
    // Fill struct ibv_qp_init_attr with qp_type = IBV_QPT_RC, send_cq = cq_, recv_cq = cq_.
    // Set cap.max_send_wr = max_send_wr, cap.max_recv_wr = max_recv_wr, cap.max_send_sge = 1, cap.max_recv_sge = 1.
    // Call ibv_create_qp(pd_, &qp_init_attr).
    std::cout << "// TODO: VerbsQp::init - ibv_create_qp\n";

    return {};
}

std::expected<void, std::string> VerbsQp::transition_to_init(int pkey_index) {
    if (!qp_) return std::unexpected("QP is null");

    // TODO: Step 5: Transition QP state from RESET to INIT
    // struct ibv_qp_attr attr{};
    // attr.qp_state = IBV_QPS_INIT;
    // attr.port_num = ib_port_;
    // attr.pkey_index = pkey_index;
    // attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    // Call ibv_modify_qp() with flags: IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS
    std::cout << "// TODO: VerbsQp::transition_to_init - ibv_modify_qp(IBV_QPS_INIT)\n";

    return {};
}

std::expected<void, std::string> VerbsQp::transition_to_rtr(
    const QpConnectionParams& remote_params,
    enum ibv_mtu path_mtu,
    uint8_t min_rnr_timer
) {
    if (!qp_) return std::unexpected("QP is null");

    // TODO: Step 6: Transition QP state from INIT to RTR (Ready to Receive)
    // struct ibv_qp_attr attr{};
    // attr.qp_state = IBV_QPS_RTR;
    // attr.path_mtu = path_mtu;
    // attr.dest_qp_num = remote_params.qpn;
    // attr.rq_psn = remote_params.psn;
    // attr.max_dest_rd_atomic = 1;
    // attr.min_rnr_timer = min_rnr_timer;
    // Fill ah_attr (dlid, port_num, sl, src_path_bits, is_global, grh).
    // Call ibv_modify_qp() with required flags.
    std::cout << "// TODO: VerbsQp::transition_to_rtr - ibv_modify_qp(IBV_QPS_RTR)\n";

    return {};
}

std::expected<void, std::string> VerbsQp::transition_to_rts(
    uint32_t local_send_psn,
    uint8_t timeout,
    uint8_t retry_cnt,
    uint8_t rnr_retry
) {
    if (!qp_) return std::unexpected("QP is null");

    // TODO: Step 7: Transition QP state from RTR to RTS (Ready to Send)
    // struct ibv_qp_attr attr{};
    // attr.qp_state = IBV_QPS_RTS;
    // attr.timeout = timeout;
    // attr.retry_cnt = retry_cnt;
    // attr.rnr_retry = rnr_retry;
    // attr.sq_psn = local_send_psn;
    // attr.max_rd_atomic = 1;
    // Call ibv_modify_qp() with required flags.
    std::cout << "// TODO: VerbsQp::transition_to_rts - ibv_modify_qp(IBV_QPS_RTS)\n";

    return {};
}

QpConnectionParams VerbsQp::get_local_params() const {
    QpConnectionParams params;
    if (qp_) params.qpn = qp_->qp_num;
    params.lid = local_lid_;
    params.gid = local_gid_;
    params.psn = local_psn_;
    return params;
}

} // namespace adder::allreduce
