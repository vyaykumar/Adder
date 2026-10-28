#include "straggler/injection.h"
#include "shared/latency_harness.h"
#include "shared/busy_poll.h"
#include <thread>

namespace adder::straggler {

StragglerInjector::StragglerInjector(uint32_t target_rank, InjectionType type, uint64_t delay_ns)
    : target_rank_(target_rank), type_(type), delay_ns_(delay_ns) {}

void StragglerInjector::maybe_inject(uint32_t current_rank, [[maybe_unused]] uint64_t round_number) noexcept {
    if (!enabled_ || current_rank != target_rank_ || type_ == InjectionType::None || delay_ns_ == 0) {
        return;
    }

    if (type_ == InjectionType::SoftwareDelay) {
        uint64_t start = shared::NanosecondClock::now_ns();
        while ((shared::NanosecondClock::now_ns() - start) < delay_ns_) {
            ADDER_CPU_PAUSE();
        }
    } else if (type_ == InjectionType::CpuThrottling) {
        // Sleep-based yield to simulate severe core throttling / time-slice loss
        std::this_thread::sleep_for(std::chrono::nanoseconds(delay_ns_));
    }
}

} // namespace adder::straggler
