#include "straggler/scheduler.h"
#include <numeric>
#include <iostream>

namespace adder::straggler {

AdaptiveScheduler::AdaptiveScheduler(uint32_t world_size, uint32_t total_chunks)
    : world_size_(world_size), total_chunks_(total_chunks) {
    current_schedule_.epoch = 0;
    current_schedule_.start_round = 0;
    current_schedule_.total_chunks = total_chunks;
    current_schedule_.chunks_per_node.resize(world_size, total_chunks / world_size);

    // Distribute remainder
    uint32_t remainder = total_chunks % world_size;
    for (uint32_t i = 0; i < remainder; ++i) {
        current_schedule_.chunks_per_node[i]++;
    }
}

ChunkAllocationSchedule AdaptiveScheduler::rebalance(
    const StragglerDetector& detector,
    uint64_t current_round,
    uint32_t current_epoch
) {
    ChunkAllocationSchedule next_sched;
    next_sched.epoch = current_epoch + 1;
    // Set start round R at least 2 rounds ahead
    next_sched.start_round = current_round + 2;
    next_sched.total_chunks = total_chunks_;
    next_sched.chunks_per_node = current_schedule_.chunks_per_node;

    // TODO: Step 1: Identify fast vs straggler nodes from detector
    // For each node marked as straggler, reduce its allocated chunks by 50% (or min 1 chunk).
    // Collect the removed chunks.
    //
    // TODO: Step 2: Distribute the removed chunks evenly across the fast (non-straggler) nodes.
    //
    // TODO: Step 3: Assert Conservation Invariant:
    // uint32_t sum = std::accumulate(next_sched.chunks_per_node.begin(), next_sched.chunks_per_node.end(), 0U);
    // assert(sum == total_chunks_);
    std::cout << "// TODO: AdaptiveScheduler::rebalance - Calculate new schedule preserving sum == total_chunks\n";

    return next_sched;
}

bool AdaptiveScheduler::should_switch_epoch(
    uint64_t current_round,
    const ChunkAllocationSchedule& candidate
) {
    // Switch occurs at round boundary R, never during a round
    if (candidate.epoch > current_schedule_.epoch && current_round >= candidate.start_round) {
        return true;
    }
    return false;
}

} // namespace adder::straggler
