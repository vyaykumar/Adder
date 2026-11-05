#pragma once

#include <infiniband/verbs.h>
#include <shared/verbs/protection_domain.h>
#include <expected>
#include <string>
#include <span>
#include <cstdint>
#include <system_error>

namespace adder::verbs {

/// \brief RAII wrapper for a pinned RDMA Memory Region (`ibv_mr`).
///
/// Encapsulates `ibv_reg_mr()` and ensures pinned memory is deregistered via
/// `ibv_dereg_mr()` upon destruction.
///
/// \see docs/man/03_memory_registration_and_pools.md (Memory Registration, lkey/rkey)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_reg_mr / ibv_dereg_mr)
class MemoryRegion {
public:
    /// \brief Registers an existing user buffer with the NIC under the given Protection Domain.
    /// \param pd Raw pointer to Protection Domain.
    /// \param addr Base virtual address of buffer to pin.
    /// \param length Length in bytes.
    /// \param access_flags Bitwise OR of IBV_ACCESS_* flags (e.g. LOCAL_WRITE, REMOTE_WRITE).
    /// \return Managed MemoryRegion or error string.
    [[nodiscard]] static std::expected<MemoryRegion, std::string> register_buffer(
        struct ibv_pd* pd,
        void* addr,
        size_t length,
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
    ) {
        //    - If errno == ENOMEM: Note that RLIMIT_MEMLOCK was likely exceeded (run `ulimit -l unlimited`).
        if (pd == nullptr or addr == nullptr or length <= 0)
            return std::unexpected("Invalid parameter values");

        struct ibv_mr *mr = ibv_reg_mr(pd, addr, length, access_flags);
        if (mr == nullptr)
            return std::unexpected(
                std::format("Failed to register buffer at address {}: {} ({})", addr, strerror(errno), errno)
            );
        return MemoryRegion(mr);
    }

    /// \brief Overload accepting managed ProtectionDomain.
    [[nodiscard]] static std::expected<MemoryRegion, std::string> register_buffer(
        const ProtectionDomain& pd,
        void* addr,
        size_t length,
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
    ) {
        return register_buffer(pd.get(), addr, length, access_flags);
    }

    /// \brief Destructor. Deregisters the memory region from the NIC.
    ~MemoryRegion() {
        if (mr_ != nullptr) ibv_dereg_mr(mr_);
        mr_ = nullptr;
    }

    // Move-only semantics
    MemoryRegion(const MemoryRegion&) = delete;
    MemoryRegion& operator=(const MemoryRegion&) = delete;

    MemoryRegion(MemoryRegion&& other) noexcept : mr_(other.mr_) {
        other.mr_ = nullptr;
    }

    MemoryRegion& operator=(MemoryRegion&& other) noexcept {
        if (this != &other) {
            if (mr_) ibv_dereg_mr(mr_);
            mr_ = other.mr_;
            other.mr_ = nullptr;
        }
        return *this;
    }

    /// \brief Local Key: used by local HCA to validate DMA read/write access.
    [[nodiscard]] uint32_t lkey() const noexcept {
        return mr_ != nullptr ? mr_->lkey : 0;
    }

    /// \brief Remote Key: exchanged with remote peer for one-sided RDMA_WRITE/READ.
    [[nodiscard]] uint32_t rkey() const noexcept {
        return mr_ != nullptr ? mr_->rkey : 0;
    }

    /// \brief Base address of the registered buffer.
    [[nodiscard]] void* addr() const noexcept {
        return mr_ != nullptr ? mr_->addr : nullptr;
    }

    /// \brief Length of registered buffer in bytes.
    [[nodiscard]] size_t length() const noexcept {
        return mr_ != nullptr ? mr_->length : 0;
    }

    /// \brief Returns a typed std::span viewing the registered memory.
    template <typename T>
    [[nodiscard]] std::span<T> as_span() const noexcept {
        if (!mr_ or !mr_->addr) return {};
        return std::span<T>(static_cast<T*>(mr_->addr), mr_->length/sizeof(T));
    }

    [[nodiscard]] struct ibv_mr* get() const noexcept { return mr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return mr_ != nullptr; }

private:
    explicit MemoryRegion(struct ibv_mr* mr) : mr_(mr) {}
    struct ibv_mr* mr_{nullptr};
};

} // namespace adder::verbs
