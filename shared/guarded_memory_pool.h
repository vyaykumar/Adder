#pragma once

#include <infiniband/verbs.h>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <expected>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

namespace adder::shared {

// Guarded memory pool: embeds PROT_NONE pages between chunks to immediately SIGSEGV on buffer overruns
class GuardedMemoryPool {
public:
    GuardedMemoryPool() = default;
    ~GuardedMemoryPool() {
        destroy();
    }

    std::expected<void, std::string> init(
        struct ibv_pd* pd,
        size_t chunk_payload_size,
        size_t num_chunks,
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE
    ) {
        page_size_ = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        chunk_payload_size_ = chunk_payload_size;
        num_chunks_ = num_chunks;

        // Round payload size up to nearest page size
        aligned_payload_size_ = ((chunk_payload_size + page_size_ - 1) / page_size_) * page_size_;
        slot_size_ = aligned_payload_size_ + page_size_; // Payload + Guard page

        total_allocation_ = slot_size_ * num_chunks_;

        raw_memory_ = static_cast<uint8_t*>(mmap(
            nullptr,
            total_allocation_,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1,
            0
        ));

        if (raw_memory_ == MAP_FAILED) {
            raw_memory_ = nullptr;
            return std::unexpected("mmap failed for guarded pool");
        }

        // Apply PROT_NONE to every guard page following each chunk
        for (size_t i = 0; i < num_chunks_; ++i) {
            uint8_t* guard_page = raw_memory_ + (i * slot_size_) + aligned_payload_size_;
            if (mprotect(guard_page, page_size_, PROT_NONE) != 0) {
                munmap(raw_memory_, total_allocation_);
                raw_memory_ = nullptr;
                return std::unexpected("mprotect PROT_NONE failed for guard page");
            }
        }

        // Register the full range (verbs handles registered ranges with guard pages if driver permits)
        mr_ = ibv_reg_mr(pd, raw_memory_, total_allocation_, access_flags);
        if (!mr_) {
            munmap(raw_memory_, total_allocation_);
            raw_memory_ = nullptr;
            return std::unexpected("ibv_reg_mr failed on guarded memory region");
        }

        return {};
    }

    uint8_t* get_chunk(size_t index) const noexcept {
        if (index >= num_chunks_ || !raw_memory_) return nullptr;
        return raw_memory_ + (index * slot_size_);
    }

    struct ibv_mr* mr() const noexcept { return mr_; }
    size_t chunk_size() const noexcept { return chunk_payload_size_; }

    void destroy() noexcept {
        if (mr_) {
            ibv_dereg_mr(mr_);
            mr_ = nullptr;
        }
        if (raw_memory_) {
            munmap(raw_memory_, total_allocation_);
            raw_memory_ = nullptr;
        }
    }

private:
    uint8_t* raw_memory_{nullptr};
    size_t page_size_{4096};
    size_t chunk_payload_size_{0};
    size_t aligned_payload_size_{0};
    size_t slot_size_{0};
    size_t num_chunks_{0};
    size_t total_allocation_{0};
    struct ibv_mr* mr_{nullptr};
};

} // namespace adder::shared
