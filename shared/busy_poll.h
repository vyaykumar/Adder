#pragma once

#include <infiniband/verbs.h>
#include <cstdint>
#include <expected>
#include <string_view>
#include <chrono>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define ADDER_CPU_PAUSE() _mm_pause()
#elif defined(__aarch64__)
#define ADDER_CPU_PAUSE() asm volatile("yield" ::: "memory")
#else
#define ADDER_CPU_PAUSE() do {} while (0)
#endif

namespace adder::shared {

enum class PollMode {
    Spin,       // Pure busy-poll for sub-microsecond latency
    EventChannel // Interrupt-driven via ibv_comp_channel to reduce CPU burn
};

class BusyPoll {
public:
    // Pure spin-polling loop. Polls until at least min_completions arrive or timeout expires.
    // Guaranteed hot-path compliant: no heap allocation, no virtual calls, no exceptions.
    static inline int poll_cq_spin(
        struct ibv_cq* cq,
        int min_completions,
        struct ibv_wc* wc_array,
        int max_wc,
        uint64_t max_spin_iterations = 100'000'000ULL
    ) noexcept {
        int total_completed = 0;
        uint64_t spin_count = 0;

        while (total_completed < min_completions) {
            int ret = ibv_poll_cq(cq, max_wc - total_completed, wc_array + total_completed);
            if (ret > 0) {
                total_completed += ret;
            } else if (ret < 0) {
                return ret; // Error from verbs driver
            } else {
                ADDER_CPU_PAUSE();
                if (++spin_count >= max_spin_iterations) {
                    return -ETIMEDOUT;
                }
            }
        }
        return total_completed;
    }

    // Single-shot fast poll
    static inline int poll_cq_once(
        struct ibv_cq* cq,
        struct ibv_wc* wc_array,
        int max_wc
    ) noexcept {
        return ibv_poll_cq(cq, max_wc, wc_array);
    }

    // Blocks until CQ event arrives on the completion channel, then acknowledges and requests next notification
    static inline int wait_cq_event(
        struct ibv_comp_channel* channel,
        struct ibv_cq* cq
    ) noexcept {
        struct ibv_cq* ev_cq = nullptr;
        void* ev_ctx = nullptr;

        if (ibv_get_cq_event(channel, &ev_cq, &ev_ctx) != 0) {
            return -1;
        }

        ibv_ack_cq_events(ev_cq, 1);

        // Arm notification for next completion
        if (ibv_req_notify_cq(cq, 0) != 0) {
            return -1;
        }

        return 0;
    }
};

} // namespace adder::shared
