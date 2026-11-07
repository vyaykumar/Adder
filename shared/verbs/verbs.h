#pragma once

// Adder: C++23 RAII Verbs Control Plane & Zero-Overhead Data Path Module
// Provides safe, idiomatic modern C++ wrappers for libibverbs while preserving
// bare-metal nanosecond performance on the hot data path.

#include <shared/verbs/device_list.h>
#include <shared/verbs/context.h>
#include <shared/verbs/protection_domain.h>
#include <shared/verbs/memory_region.h>
#include <shared/verbs/completion_queue.h>
#include <shared/verbs/queue_pair.h>
#include <shared/verbs/state_transitions.h>

namespace adder::verbs {
    // Top-level convenience aliases
    using qp_state::transition_rc_to_init;
    using qp_state::transition_rc_to_rtr;
    using qp_state::transition_rc_to_rts;
    using qp_state::transition_ud_to_init;
    using qp_state::transition_ud_to_rtr;
    using qp_state::transition_ud_to_rts;
    using qp_state::transition_to_error;
    using qp_state::transition_to_reset;
} // namespace adder::verbs
