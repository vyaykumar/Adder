#pragma once

#include <cerrno>
#include <cstdint>
#include <expected>
#include <format>
#include <infiniband/verbs.h>
#include <shared/verbs/protection_domain.h>
#include <span>
#include <string>
#include <system_error>

namespace adder::verbs {

/// \brief RAII wrapper for a pinned RDMA Memory Region (`ibv_mr`).
///
/// \see docs/man/03_memory_registration_and_pools.md (Memory Registration, lkey/rkey)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_reg_mr / ibv_dereg_mr)
class MemoryRegion {
public:
    /// \brief Registers an existing user buffer with the NIC under the given Protection Domain.
    [[nodiscard]] static std::expected<MemoryRegion, std::string> register_buffer(
        struct ibv_pd* pd,
        void* addr,
        size_t length,
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
    ) {
        if (!pd or !addr or length == 0)
            return std::unexpected("[MemoryRegion::register_buffer] Null PD, null buffer address, or zero length provided");

        struct ibv_mr *mr = ibv_reg_mr(pd, addr, length, access_flags);
        if (mr == nullptr) {
            int err = errno;
            std::string hint = (err == ENOMEM) ? " (exceeded locked memory limit; check 'ulimit-l')" : "";
            return std::unexpected(
                std::format( "[MemoryRegion::register_buffer] Failed to register buffer ({} bytes at {}): {} ({}){}",
                    length,
                    addr,
                    std::generic_category().message(errno),
                    errno,
                    hint
                )
            );
        }
        return MemoryRegion(mr);
    }

    [[nodiscard]] static std::expected<MemoryRegion, std::string> register_buffer(
        const ProtectionDomain& pd,
        void* addr,
        size_t length,
        int access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ
    ) {
        return register_buffer(pd.get(), addr, length, access_flags);
    }

    ~MemoryRegion() {
        if (mr_ != nullptr) ibv_dereg_mr(mr_);
        mr_ = nullptr;
    }

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

    [[nodiscard]] uint32_t lkey() const noexcept {
        return mr_ != nullptr ? mr_->lkey : 0;
    }

    [[nodiscard]] uint32_t rkey() const noexcept {
        return mr_ != nullptr ? mr_->rkey : 0;
    }

    [[nodiscard]] void* addr() const noexcept {
        return mr_ != nullptr ? mr_->addr : nullptr;
    }

    [[nodiscard]] size_t length() const noexcept {
        return mr_ != nullptr ? mr_->length : 0;
    }

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
