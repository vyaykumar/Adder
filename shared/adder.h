#pragma once

// Adder: Low-Latency RDMA Communication Engine Umbrella Header
// Includes all zero-overhead core utilities and hardware abstraction layers.

#include <shared/affinity.h>
#include <shared/busy_poll.h>
#include <shared/latency_harness.h>
#include <shared/ring_buffer.h>
#include <shared/free_list_pool.h>
#include <shared/guarded_memory_pool.h>
#include <shared/verbs.h>

namespace adder {
    using shared::CpuTopology;
    using shared::BusyPoll;
    using shared::LatencyHistogram;
    using shared::NanosecondClock;
    using shared::ProvenanceMetadata;
    using shared::SpscRingBuffer;
    using shared::FreeListMemoryPool;
    using shared::GuardedMemoryPool;
} // namespace adder
