#include "straggler/detector.h"
#include <algorithm>

namespace adder::straggler {

StragglerDetector::StragglerDetector(uint32_t world_size, double alpha, double sigma_threshold)
    : world_size_(world_size), alpha_(alpha), sigma_threshold_(sigma_threshold) {
    stats_.resize(world_size);
    for (uint32_t i = 0; i < world_size; ++i) {
        stats_[i].node_rank = i;
    }
}

void StragglerDetector::record_completion(uint32_t node_rank, uint64_t latency_ns) {
    if (node_rank >= world_size_) return;

    auto& s = stats_[node_rank];
    double lat = static_cast<double>(latency_ns);

    if (s.ema_mean == 0.0) {
        s.ema_mean = lat;
        s.ema_var = 0.0;
        return;
    }

    // TODO: Step 1: Update Exponential Moving Average (EMA) of mean and variance:
    // delta = lat - s.ema_mean;
    // s.ema_mean += alpha_ * delta;
    // s.ema_var = (1.0 - alpha_) * (s.ema_var + alpha_ * delta * delta);
    //
    // TODO: Step 2: Check if current latency exceeds threshold:
    // double stddev = std::sqrt(s.ema_var);
    // if (lat > s.ema_mean + sigma_threshold_ * stddev) {
    //     s.consecutive_slow_rounds++;
    // } else {
    //     s.consecutive_slow_rounds = 0;
    // }
}

bool StragglerDetector::check_and_update_stragglers() {
    bool straggler_detected = false;
    for (auto& s : stats_) {
        // TODO: Step 3: Mark as straggler if 3 consecutive slow rounds observed
        if (s.consecutive_slow_rounds >= 3) {
            s.is_straggler = true;
            straggler_detected = true;
        } else {
            s.is_straggler = false;
        }
    }
    return straggler_detected;
}

const NodeLatencyStats& StragglerDetector::get_stats(uint32_t node_rank) const {
    return stats_.at(node_rank);
}

bool StragglerDetector::is_straggler(uint32_t node_rank) const {
    if (node_rank >= world_size_) return false;
    return stats_[node_rank].is_straggler;
}

} // namespace adder::straggler
