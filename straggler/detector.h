#pragma once

#include <vector>
#include <cstdint>
#include <cmath>

namespace adder::straggler {

struct NodeLatencyStats {
    uint32_t node_rank{0};
    double ema_mean{0.0};
    double ema_var{0.0};
    uint32_t consecutive_slow_rounds{0};
    bool is_straggler{false};
};

class StragglerDetector {
public:
    explicit StragglerDetector(uint32_t world_size, double alpha = 0.2, double sigma_threshold = 2.0);

    // Records the observed round/chunk completion time in nanoseconds for a node
    void record_completion(uint32_t node_rank, uint64_t latency_ns);

    // Evaluates whether any node has exceeded 2*sigma for 3 consecutive rounds
    bool check_and_update_stragglers();

    const NodeLatencyStats& get_stats(uint32_t node_rank) const;
    bool is_straggler(uint32_t node_rank) const;

private:
    uint32_t world_size_;
    double alpha_;           // EMA smoothing factor
    double sigma_threshold_; // Default 2.0
    std::vector<NodeLatencyStats> stats_;
};

} // namespace adder::straggler
