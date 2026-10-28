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

struct ChunkDescriptor {
    uint8_t* ptr{nullptr};
    size_t size{0};
    uint32_t lkey{0};
    uint32_t rkey{0};
    uint64_t remote_addr{0};
    uint32_t chunk_id{0};
};

class FreeListMemoryPool {
public:
    FreeListMemoryPool() = default;
    ~FreeListMemoryPool() {
        destroy();
    }

    // Non-copyable, movable
    FreeListMemoryPool(const FreeListMemoryPool&) = delete;
    FreeListMemoryPool& operator=(const FreeListMemoryPool&) = delete;

    FreeListMemoryPool(FreeListMemoryPool&& other) noexcept {
        swap(other);
    }
    FreeListMemoryPool& operator=(FreeListMemoryPool&& other) noexcept {
        if (this != &other) {
            destroy();
            swap(other);
        }
        return *this;
    }

    // Allocates and registers contiguous memory buffer
    std::expected<void, std::string> init(
        struct ibv_pd* pd,
        size_t chunk_size,
        size_t num_chunks,
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ,
        bool use_hugepages = false
    ) {
        chunk_size_ = chunk_size;
        num_chunks_ = num_chunks;
        total_size_ = chunk_size * num_chunks;
        use_hugepages_ = use_hugepages;

        // Allocate memory (hugepages or page-aligned standard memory)
        if (use_hugepages) {
            base_ptr_ = static_cast<uint8_t*>(mmap(
                nullptr,
                total_size_,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                -1,
                0
            ));
            if (base_ptr_ == MAP_FAILED) {
                // Fallback to standard aligned memory if hugepages not configured on system
                base_ptr_ = nullptr;
            }
        }

        if (base_ptr_ == nullptr) {
            int ret = posix_memalign(reinterpret_cast<void**>(&base_ptr_), sysconf(_SC_PAGESIZE), total_size_);
            if (ret != 0 || base_ptr_ == nullptr) {
                return std::unexpected("posix_memalign failed with rc=" + std::to_string(ret));
            }
            std::memset(base_ptr_, 0, total_size_);
        }

        // Register memory region with RDMA Protection Domain
        mr_ = ibv_reg_mr(pd, base_ptr_, total_size_, access_flags);
        if (!mr_) {
            if (use_hugepages_ && base_ptr_ != MAP_FAILED) {
                munmap(base_ptr_, total_size_);
            } else {
                free(base_ptr_);
            }
            base_ptr_ = nullptr;
            return std::unexpected("ibv_reg_mr failed: check ulimit -l (memlock) and permissions");
        }

        // Populate free-list index stack
        free_indices_.reserve(num_chunks);
        for (size_t i = 0; i < num_chunks; ++i) {
            free_indices_.push_back(static_cast<uint32_t>(i));
        }

        return {};
    }

    // O(1) chunk allocation from pre-registered pool (Hot path compliant: zero malloc)
    inline std::optional<ChunkDescriptor> acquire_chunk() noexcept {
        if (free_indices_.empty()) {
            return std::nullopt;
        }
        uint32_t idx = free_indices_.back();
        free_indices_.pop_back();

        return ChunkDescriptor{
            .ptr = base_ptr_ + (idx * chunk_size_),
            .size = chunk_size_,
            .lkey = mr_->lkey,
            .rkey = mr_->rkey,
            .remote_addr = reinterpret_cast<uint64_t>(base_ptr_ + (idx * chunk_size_)),
            .chunk_id = idx
        };
    }

    // O(1) chunk release back to pool
    inline void release_chunk(uint32_t chunk_id) noexcept {
        if (chunk_id < num_chunks_) {
            free_indices_.push_back(chunk_id);
        }
    }

    uint8_t* raw_base_ptr() const noexcept { return base_ptr_; }
    size_t total_size() const noexcept { return total_size_; }
    size_t chunk_size() const noexcept { return chunk_size_; }
    size_t available_chunks() const noexcept { return free_indices_.size(); }
    struct ibv_mr* mr() const noexcept { return mr_; }

    void destroy() noexcept {
        if (mr_) {
            ibv_dereg_mr(mr_);
            mr_ = nullptr;
        }
        if (base_ptr_) {
            if (use_hugepages_) {
                munmap(base_ptr_, total_size_);
            } else {
                free(base_ptr_);
            }
            base_ptr_ = nullptr;
        }
        free_indices_.clear();
    }

private:
    void swap(FreeListMemoryPool& other) noexcept {
        std::swap(base_ptr_, other.base_ptr_);
        std::swap(total_size_, other.total_size_);
        std::swap(chunk_size_, other.chunk_size_);
        std::swap(num_chunks_, other.num_chunks_);
        std::swap(mr_, other.mr_);
        std::swap(use_hugepages_, other.use_hugepages_);
        std::swap(free_indices_, other.free_indices_);
    }

    uint8_t* base_ptr_{nullptr};
    size_t total_size_{0};
    size_t chunk_size_{0};
    size_t num_chunks_{0};
    struct ibv_mr* mr_{nullptr};
    bool use_hugepages_{false};
    std::vector<uint32_t> free_indices_;
};

} // namespace adder::shared
