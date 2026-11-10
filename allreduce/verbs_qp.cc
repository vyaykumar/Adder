#include <allreduce/verbs_qp.h>
#include <iostream>
#include <cstring>
#include <cerrno>

namespace adder::allreduce {

VerbsQp::~VerbsQp() {
    destroy();
}

void VerbsQp::destroy() noexcept {
    // Release RAII QueuePair first if created via shared/verbs::QueuePair::create
    owned_qp_ = verbs::QueuePair{};

    // 1. Destroy Queue Pair (only if raw-owned in legacy standalone mode)
    if (qp_ && owns_shared_resources_) {
        int rc = ibv_destroy_qp(qp_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_destroy_qp failed (rc=" << rc << ")\n";
        }
    }
    qp_ = nullptr;

    // 2. Destroy Completion Queue (only if owned in standalone mode)
    if (cq_ && owns_shared_resources_) {
        int rc = ibv_destroy_cq(cq_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_destroy_cq failed (rc=" << rc << ")\n";
        }
    }
    cq_ = nullptr;

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

    // 4. Deallocate Protection Domain (only if owned in standalone mode)
    if (pd_ && owns_shared_resources_) {
        int rc = ibv_dealloc_pd(pd_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_dealloc_pd failed (rc=" << rc << ", errno=" << errno << "): resource still registered to PD\n";
        }
    }
    pd_ = nullptr;

    // 5. Close device context (only if owned in standalone mode)
    if (ctx_ && owns_shared_resources_) {
        int rc = ibv_close_device(ctx_);
        if (rc != 0) {
            std::cerr << "[Warning] ibv_close_device failed (rc=" << rc << ")\n";
        }
    }
    ctx_ = nullptr;
    owns_shared_resources_ = false;
}

VerbsQp::VerbsQp(VerbsQp&& other) noexcept {
    owned_qp_ = std::move(other.owned_qp_);
    ctx_ = other.ctx_; other.ctx_ = nullptr;
    pd_ = other.pd_; other.pd_ = nullptr;
    cq_ = other.cq_; other.cq_ = nullptr;
    qp_ = other.qp_; other.qp_ = nullptr;
    owns_shared_resources_ = other.owns_shared_resources_;
    other.owns_shared_resources_ = false;
    tracked_mrs_ = std::move(other.tracked_mrs_);
    tracked_ahs_ = std::move(other.tracked_ahs_);
    ib_port_ = other.ib_port_;
    gid_index_ = other.gid_index_;
    local_lid_ = other.local_lid_;
    local_gid_ = other.local_gid_;
    local_psn_ = other.local_psn_;
}

VerbsQp& VerbsQp::operator=(VerbsQp&& other) noexcept {
    if (this != &other) {
        destroy();

        owned_qp_ = std::move(other.owned_qp_);
        ctx_ = other.ctx_; other.ctx_ = nullptr;
        pd_ = other.pd_; other.pd_ = nullptr;
        cq_ = other.cq_; other.cq_ = nullptr;
        qp_ = other.qp_; other.qp_ = nullptr;
        owns_shared_resources_ = other.owns_shared_resources_;
        other.owns_shared_resources_ = false;
        tracked_mrs_ = std::move(other.tracked_mrs_);
        tracked_ahs_ = std::move(other.tracked_ahs_);
        ib_port_ = other.ib_port_;
        gid_index_ = other.gid_index_;
        local_lid_ = other.local_lid_;
        local_gid_ = other.local_gid_;
        local_psn_ = other.local_psn_;
    }
    return *this;
}

std::expected<void, std::string> VerbsQp::init(
    [[maybe_unused]] const verbs::Context& ctx,
    [[maybe_unused]] const verbs::ProtectionDomain& pd,
    [[maybe_unused]] const verbs::CompletionQueue& cq,
    uint8_t ib_port,
    int gid_index,
    [[maybe_unused]] const verbs::QpConfig& cfg
) {
    ib_port_ = ib_port;
    gid_index_ = gid_index;
    local_psn_ = static_cast<uint32_t>(rand() & 0xffffff);
    owns_shared_resources_ = false;

    // =========================================================================
    // Step 1: Bind Shared Hardware Handles & Query Port/GID Metadata
    // Goal: Borrow the rank's shared Context, ProtectionDomain, and CQ, query
    // the local LID and RoCEv2 GID from ctx (just like Step 2 in pingpong.cc),
    // and instantiate owned_qp_ via verbs::QueuePair::create(pd, cq, cq, cfg).
    // =========================================================================
    //
    // Q: Why do we store ctx_, pd_, and cq_ with owns_shared_resources_ = false?
    // A: Because the caller (main.cc) owns the single shared Context, PD, and CQ
    //    across all ring/tree peer links. VerbsQp only owns its QueuePair (owned_qp_).
    //
    // TODO:
    //   1. Store non-owning pointers: ctx_ = ctx.get(); pd_ = pd.get(); cq_ = cq.get();
    //   2. Query port attributes via ctx.query_port(ib_port_) -> store port.lid in local_lid_.
    //   3. Query GID via ctx.query_gid(ib_port_, gid_index_) -> store in local_gid_.
    //   4. Create the QueuePair via verbs::QueuePair::create(pd, cq, cq, cfg) ->
    //      move into owned_qp_ and set qp_ = owned_qp_.get().
    std::cout << "// TODO: VerbsQp::init(shared Context/PD/CQ) - verbs::QueuePair::create\n";

    return {};
}

std::expected<void, std::string> VerbsQp::init(
    [[maybe_unused]] std::string_view dev_name,
    uint8_t ib_port,
    [[maybe_unused]] int cq_depth,
    [[maybe_unused]] int max_send_wr,
    [[maybe_unused]] int max_recv_wr
) {
    ib_port_ = ib_port;
    local_psn_ = static_cast<uint32_t>(rand() & 0xffffff);
    owns_shared_resources_ = true;

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

std::expected<void, std::string> VerbsQp::transition_to_init([[maybe_unused]] int pkey_index) {
    if (!qp_) return std::unexpected("QP is null");

    // =========================================================================
    // Step 5: Transition QP State from RESET -> INIT
    // Goal: Delegate directly to verbs::qp_state::transition_rc_to_init(qp_, ib_port_, pkey_index)
    // from <shared/verbs/state_transitions.h> (proven in D1 pingpong.cc!).
    // =========================================================================
    // TODO: Step 5: Transition QP state from RESET to INIT
    // struct ibv_qp_attr attr{};
    // attr.qp_state = IBV_QPS_INIT;
    // attr.port_num = ib_port_;
    // attr.pkey_index = pkey_index;
    // attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
    // Call ibv_modify_qp() with flags: IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS
    // (Or simply return verbs::qp_state::transition_rc_to_init(qp_, ib_port_, pkey_index);)
    std::cout << "// TODO: VerbsQp::transition_to_init - verbs::qp_state::transition_rc_to_init\n";

    return {};
}

std::expected<void, std::string> VerbsQp::transition_to_rtr(
    [[maybe_unused]] const QpConnectionParams& remote_params,
    [[maybe_unused]] enum ibv_mtu path_mtu,
    [[maybe_unused]] uint8_t min_rnr_timer
) {
    if (!qp_) return std::unexpected("QP is null");

    // =========================================================================
    // Step 6: Transition QP State from INIT -> RTR (Ready to Receive)
    // Goal: Delegate directly to verbs::qp_state::transition_rc_to_rtr(
    //           qp_, remote_params, ib_port_, gid_index_, path_mtu, min_rnr_timer)
    // from <shared/verbs/state_transitions.h>.
    // =========================================================================
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
    // (Or simply return verbs::qp_state::transition_rc_to_rtr(...);)
    std::cout << "// TODO: VerbsQp::transition_to_rtr - verbs::qp_state::transition_rc_to_rtr\n";

    return {};
}

std::expected<void, std::string> VerbsQp::transition_to_rts(
    [[maybe_unused]] uint32_t local_send_psn,
    [[maybe_unused]] uint8_t timeout,
    [[maybe_unused]] uint8_t retry_cnt,
    [[maybe_unused]] uint8_t rnr_retry
) {
    if (!qp_) return std::unexpected("QP is null");

    // =========================================================================
    // Step 7: Transition QP State from RTR -> RTS (Ready to Send)
    // Goal: Delegate directly to verbs::qp_state::transition_rc_to_rts(
    //           qp_, local_send_psn, timeout, retry_cnt, rnr_retry)
    // from <shared/verbs/state_transitions.h>.
    // =========================================================================
    // TODO: Step 7: Transition QP state from RTR to RTS (Ready to Send)
    // struct ibv_qp_attr attr{};
    // attr.qp_state = IBV_QPS_RTS;
    // attr.timeout = timeout;
    // attr.retry_cnt = retry_cnt;
    // attr.rnr_retry = rnr_retry;
    // attr.sq_psn = local_send_psn;
    // attr.max_rd_atomic = 1;
    // Call ibv_modify_qp() with required flags.
    // (Or simply return verbs::qp_state::transition_rc_to_rts(...);)
    std::cout << "// TODO: VerbsQp::transition_to_rts - verbs::qp_state::transition_rc_to_rts\n";

    return {};
}

std::expected<void, std::string> VerbsQp::connect_rc(
    [[maybe_unused]] const QpConnectionParams& remote_params,
    [[maybe_unused]] enum ibv_mtu path_mtu
) {
    if (!qp_) return std::unexpected("QP is null");

    // =========================================================================
    // Step 7 (Monadic Pipeline): RESET -> INIT -> RTR -> RTS
    // Goal: Chain transition_to_init() .and_then(transition_to_rtr) .and_then(transition_to_rts)
    // exactly like Step 7 in pingpong.cc!
    // =========================================================================
    // TODO: Chain transition_to_init() -> transition_to_rtr(remote_params, path_mtu)
    //       -> transition_to_rts(local_psn_) using std::expected::and_then.
    std::cout << "// TODO: VerbsQp::connect_rc - monadic INIT -> RTR -> RTS pipeline\n";

    return {};
}

QpConnectionParams VerbsQp::get_local_params() const noexcept {
    QpConnectionParams params{};
    if (qp_) params.qpn = qp_->qp_num;
    params.lid = local_lid_;
    params.gid = local_gid_;
    params.psn = local_psn_;
    return params;
}

} // namespace adder::allreduce
