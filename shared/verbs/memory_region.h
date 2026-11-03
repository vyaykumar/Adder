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
        // TODO: [Implement Memory Registration]
        // 1. Verify pd != nullptr, addr != nullptr, length > 0.
        // 2. Call `struct ibv_mr* mr = ibv_reg_mr(pd, addr, length, access_flags);`
        // 3. If mr is nullptr:
        //    - If errno == ENOMEM: Note that RLIMIT_MEMLOCK was likely exceeded (run `ulimit -l unlimited`).
        //    - Return unexpected with formatted error message.
        // 4. Return MemoryRegion(mr).
        (void)pd;
        (void)addr;
        (void)length;
        (void)access_flags;
        return std::unexpected("TODO: Implement MemoryRegion::register_buffer");
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
        // TODO: [Implement Memory Deregistration]
        // 1. If mr_ != nullptr, call `ibv_dereg_mr(mr_)`.
        // 2. Set mr_ = nullptr.
        // Pitfall: Never deregister an MR while outstanding Work Requests
        // referencing its lkey/rkey are still in flight in a QP!
    }

    // Move-only semantics
    MemoryRegion(const MemoryRegion&) = delete;
    MemoryRegion& operator=(const MemoryRegion&) = delete;

    MemoryRegion(MemoryRegion&& other) noexcept : mr_(other.mr_) {
        other.mr_ = nullptr;
    }

    MemoryRegion& operator=(MemoryRegion&& other) noexcept {
        if (this != &other) {
            // TODO: [Implement Move Assignment]
            // 1. Deregister existing mr_ via ibv_dereg_mr().
            // 2. Transfer mr_ from other.
            // 3. Reset other.mr_ = nullptr.
        }
        return *this;
    }

    /// \brief Local Key: used by local HCA to validate DMA read/write access.
    [[nodiscard]] uint32_t lkey() const noexcept {
        // TODO: Return mr_->lkey if mr_ != nullptr, else 0
        return 0;
    }

    /// \brief Remote Key: exchanged with remote peer for one-sided RDMA_WRITE/READ.
    [[nodiscard]] uint32_t rkey() const noexcept {
        // TODO: Return mr_->rkey if mr_ != nullptr, else 0
        return 0;
    }

    /// \brief Base address of the registered buffer.
    [[nodiscard]] void* addr() const noexcept {
        // TODO: Return mr_->addr if mr_ != nullptr, else nullptr
        return nullptr;
    }

    /// \brief Length of registered buffer in bytes.
    [[nodiscard]] size_t length() const noexcept {
        // TODO: Return mr_->length if mr_ != nullptr, else 0
        return 0;
    }

    /// \brief Returns a typed std::span viewing the registered memory.
    template <typename T>
    [[nodiscard]] std::span<T> as_span() const noexcept {
        // TODO: [Implement Typed View]
        // If !mr_ || !mr_->addr, return empty span {}.
        // Otherwise return std::span<T>(static_cast<T*>(mr_->addr), mr_->length / sizeof(T)).
        return {};
    }

    /// \brief Raw pointer accessor.
    [[nodiscard]] struct ibv_mr* get() const noexcept { return mr_; }
    [[nodiscard]] explicit operator bool() const noexcept { return mr_ != nullptr; }

private:
    explicit MemoryRegion(struct ibv_mr* mr) : mr_(mr) {}
    struct ibv_mr* mr_{nullptr};
};

} // namespace adder::verbs
