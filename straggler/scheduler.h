#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <expected>
#include "straggler/detector.h"

namespace adder::straggler {

struct ChunkAllocationSchedule {
    uint32_t epoch{0};
    uint64_t start_round{0};
    std::vector<uint32_t> chunks_per_node; // Size = world_size
    uint32_t total_chunks{0};             // Invariant: sum(chunks_per_node) == total_chunks
};

class AdaptiveScheduler {
public:
    AdaptiveScheduler(uint32_t world_size, uint32_t total_chunks);

    // Computes a rebalanced schedule redistributing chunks from stragglers to fast nodes
    // Enforces the Conservation Invariant: total chunk count remains exactly constant
    ChunkAllocationSchedule rebalance(
        const StragglerDetector& detector,
        uint64_t current_round,
        uint32_t current_epoch
    );

    // Checks whether an active schedule should switch to a newly received epoch
    bool should_switch_epoch(uint64_t current_round, const ChunkAllocationSchedule& candidate);

    const ChunkAllocationSchedule& active_schedule() const { return current_schedule_; }
    void apply_schedule(const ChunkAllocationSchedule& new_sched) { current_schedule_ = new_sched; }

private:
    uint32_t world_size_;
    uint32_t total_chunks_;
    ChunkAllocationSchedule current_schedule_;
};

} // namespace adder::straggler
