#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <new>
#include <concepts>
#include <span>

namespace adder::shared {

#if defined(__cpp_lib_hardware_interference_size)
    using std::hardware_destructive_interference_size;
#else
    constexpr size_t hardware_destructive_interference_size = 64;
#endif

// Lock-Free Single-Producer Single-Consumer (SPSC) Circular Ring Buffer
// Used for credit loops and chunk completion tracking across ring stages.
template <typename T, size_t Capacity>
requires ((Capacity & (Capacity - 1)) == 0 && Capacity > 0) // Must be power of 2
class SpscRingBuffer {
public:
    static constexpr size_t kCapacity = Capacity;
    static constexpr size_t kMask = Capacity - 1;

    SpscRingBuffer() = default;
    ~SpscRingBuffer() = default;

    // Non-copyable, non-movable
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    // Enqueue single item (Producer only)
    inline bool push(const T& item) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_cached_;

        if ((head - tail) >= Capacity) {
            // Re-read consumer's actual tail with acquire semantics
            tail_cached_ = tail_.load(std::memory_order_acquire);
            if ((head - tail_cached_) >= Capacity) {
                return false; // Full
            }
        }

        buffer_[head & kMask] = item;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Dequeue single item (Consumer only)
    inline std::optional<T> pop() noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_cached_;

        if (tail == head) {
            // Re-read producer's actual head with acquire semantics
            head_cached_ = head_.load(std::memory_order_acquire);
            if (tail == head_cached_) {
                return std::nullopt; // Empty
            }
        }

        T item = buffer_[tail & kMask];
        tail_.store(tail + 1, std::memory_order_release);
        return item;
    }

    // Peek available elements without removing (Consumer only)
    inline size_t available() const noexcept {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_relaxed);
        return (head >= tail) ? (head - tail) : 0;
    }

    inline bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_relaxed);
    }

    inline bool full() const noexcept {
        return available() >= Capacity;
    }

private:
    alignas(hardware_destructive_interference_size) std::atomic<size_t> head_{0};
    size_t head_cached_{0};

    alignas(hardware_destructive_interference_size) std::atomic<size_t> tail_{0};
    size_t tail_cached_{0};

    alignas(hardware_destructive_interference_size) T buffer_[Capacity];
};

} // namespace adder::shared
