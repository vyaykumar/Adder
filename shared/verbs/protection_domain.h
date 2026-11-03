#pragma once

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
        // TODO: [Implement PD Allocation]
        // 1. Verify ctx != nullptr.
        // 2. Call `struct ibv_pd* pd = ibv_alloc_pd(ctx);`
        // 3. If pd is nullptr, return unexpected with strerror(errno).
        // 4. Return ProtectionDomain(pd).
        (void)ctx;
        return std::unexpected("TODO: Implement ProtectionDomain::alloc");
    }

    /// \brief Overload accepting managed Context.
    [[nodiscard]] static std::expected<ProtectionDomain, std::string> alloc(const Context& ctx) {
        // TODO: Call alloc(ctx.get())
        return alloc(ctx.get());
    }

    /// \brief Destructor. Deallocates the Protection Domain.
    ~ProtectionDomain() {
        // TODO: [Implement PD Deallocation]
        // Pitfall: ibv_dealloc_pd() will fail with EBUSY (code 16) if any
        // Memory Regions (ibv_mr), Queue Pairs (ibv_qp), or Address Handles (ibv_ah)
        // created within this PD have not been destroyed first.
        // If pd_ != nullptr, call ibv_dealloc_pd(pd_) and set pd_ = nullptr.
    }

    // Move-only semantics (prevents duplicate deallocations)
    ProtectionDomain(const ProtectionDomain&) = delete;
    ProtectionDomain& operator=(const ProtectionDomain&) = delete;

    ProtectionDomain(ProtectionDomain&& other) noexcept : pd_(other.pd_) {
        other.pd_ = nullptr;
    }

    ProtectionDomain& operator=(ProtectionDomain&& other) noexcept {
        if (this != &other) {
            // TODO: [Implement Move Assignment]
            // 1. Deallocate existing pd_ via ibv_dealloc_pd() if non-null.
            // 2. Transfer pd_ from other.
            // 3. Reset other.pd_ = nullptr.
        }
        return *this;
    }

    /// \brief Raw pointer accessor.
    [[nodiscard]] struct ibv_pd* get() const noexcept { return pd_; }
    [[nodiscard]] explicit operator bool() const noexcept { return pd_ != nullptr; }

private:
    explicit ProtectionDomain(struct ibv_pd* pd) : pd_(pd) {}
    struct ibv_pd* pd_{nullptr};
};

} // namespace adder::verbs
