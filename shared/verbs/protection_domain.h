#pragma once

#include <format>
#include <infiniband/verbs.h>
#include <shared/verbs/context.h>
#include <expected>
#include <string>
#include <system_error>

namespace adder::verbs {

/// \brief RAII wrapper for an RDMA Protection Domain (`ibv_pd`).
///
/// Encapsulates `ibv_alloc_pd()` and guarantees safe release via `ibv_dealloc_pd()`.
///
/// \see docs/man/01_rdma_and_verbs_core.md Section 3 (Object Hierarchy)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_alloc_pd / ibv_dealloc_pd)
class ProtectionDomain {
public:
    /// \brief Allocates a Protection Domain within the given verbs context.
    /// \param ctx Pointer to raw `ibv_context`.
    /// \return Managed ProtectionDomain or error string.
    [[nodiscard]] static std::expected<ProtectionDomain, std::string> alloc(struct ibv_context* ctx) {
        if (!ctx) return std::unexpected("Null context pointer");
        struct ibv_pd* pd = ibv_alloc_pd(ctx);
        if (!pd) {
            int err = errno;
            return std::unexpected(
                std::format("Failed to allocate protection domain: {} ({})", strerror(err), err)
            );
        }
        return ProtectionDomain(pd);
    }

    /// \brief Overload accepting managed Context.
    [[nodiscard]] static std::expected<ProtectionDomain, std::string> alloc(const Context& ctx) {
        return alloc(ctx.get());
    }

    ~ProtectionDomain() {
        if (!pd_) {
            ibv_dealloc_pd(pd_);
            pd_ = nullptr;
        }

    }

    ProtectionDomain(const ProtectionDomain&) = delete;
    ProtectionDomain& operator=(const ProtectionDomain&) = delete;

    ProtectionDomain(ProtectionDomain&& other) noexcept : pd_(other.pd_) {
        other.pd_ = nullptr;
    }

    ProtectionDomain& operator=(ProtectionDomain&& other) noexcept {
        if (this != &other) {
            if (pd_) ibv_dealloc_pd(pd_);
            pd_ = other.pd_;
            other.pd_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] struct ibv_pd* get() const noexcept { return pd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return pd_ != nullptr; }

private:
    explicit ProtectionDomain(struct ibv_pd* pd) : pd_(pd) {}
    struct ibv_pd* pd_{nullptr};
};

} // namespace adder::verbs
