#pragma once

#include <cstdint>
#include <chrono>

namespace adder::straggler {

enum class InjectionType {
    None,
    SoftwareDelay, // Spin/sleep delay in user space
    CpuThrottling  // Simulates thermal or quota throttling
};

class StragglerInjector {
public:
    StragglerInjector(uint32_t target_rank, InjectionType type = InjectionType::None, uint64_t delay_ns = 0);

    // Applies delay if caller rank matches target rank
    void maybe_inject(uint32_t current_rank, uint64_t round_number) noexcept;

    void set_delay_ns(uint64_t delay_ns) noexcept { delay_ns_ = delay_ns; }
    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }

private:
    uint32_t target_rank_;
    InjectionType type_;
    uint64_t delay_ns_;
    bool enabled_{true};
};

} // namespace adder::straggler
