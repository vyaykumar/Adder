#pragma once

#include <infiniband/verbs.h>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <cstdint>

namespace adder::verbs {

/// \brief RAII wrapper for an opened RDMA device context (`ibv_context`).
///
/// Encapsulates `ibv_open_device()` and guarantees safe closure via `ibv_close_device()`.
/// Also provides typed helpers for port querying, GID retrieval, and MTU negotiation.
///
/// \see docs/man/01_rdma_and_verbs_core.md Section 3 (Control Plane Object Lifecycle)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_open_device / ibv_close_device)
class Context {
public:
    /// \brief Opens an RDMA device and constructs a managed Context.
    /// \param dev Pointer to discovered `ibv_device` (from DeviceList).
    /// \return Managed Context or error string on failure.
    [[nodiscard]] static std::expected<Context, std::string> open(struct ibv_device* dev) {
        if (!dev) return std::unexpected("Null device pointer");
        struct ibv_context* ctx = ibv_open_device(dev);

        // Recommendations came from query_port()
        // Upgrades to use std::format to provide more information to caller.
        // Also recommended to use std::error_code.
        if (!ctx) return std::unexpected(strerror(errno));
        return Context(ctx);
    }

    /// \brief Destructor. Closes the verbs context.
    ~Context() {
        if (ctx_) {
            ibv_close_device(ctx_);
            ctx_ = nullptr
        }
    }

    // Move-only semantics (prevents multiple wrappers attempting to close the same context)
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&& other) noexcept : ctx_(other.ctx_) {
        other.ctx_ = nullptr;
    }

    Context& operator=(Context&& other) noexcept {
        if (this != &other) {
            if (ctx_) ibv_close_device(ctx_);
            ctx_ = other.ctx_;
            other.ctx_ = nullptr;
        }

        return *this;
    }

    /// \brief Raw pointer accessor.
    [[nodiscard]] struct ibv_context* get() const noexcept { return ctx_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ctx_ != nullptr; }

    /// \brief Returns the canonical string name of the RDMA device (e.g. "rxe0").
    [[nodiscard]] std::string_view device_name() const noexcept {
        return ctx_ and ctx_->device ? ibv_get_device_name(ctx_->device) : "";
    }

    /// \brief Queries device port attributes (active MTU, LID, link state).
    /// \param port_num 1-based port index (typically 1).
    /// \return Port attributes struct or error description.
    [[nodiscard]] std::expected<struct ibv_port_attr, std::string> query_port(uint8_t port_num = 1) const {

        if (!ctx_) return std::unexpected("Null device pointer");

        struct ibv_port_attr port_attr{};
        int ret = ibv_query_port(ctx_, port_num, &port_attr);

        // Could use std::error_code here. And above too.
        if (ret != 0) {
            int err = std::abs(ret);
            return std::unexpected(
                std::format("Failed to query port {} : {} ({})", port_num, std::strerror(err), err)
            );
        }

        return port_attr;
    }

    /// \brief Queries a Global Identifier (GID) from the port's GID table.
    ///
    /// For RoCEv2 (Soft-RoCE / rxe), GID index 1 is typically the IPv4-mapped GID.
    ///
    /// \param port_num 1-based port index.
    /// \param gid_index GID table index (0 = RoCEv1 / MAC, 1 = RoCEv2 / IPv4).
    /// \return GID union or error description.
    [[nodiscard]] std::expected<union ibv_gid, std::string> query_gid(uint8_t port_num, int gid_index) const {

        if (!ctx_) return std::uncexpected("Null device pointer")

        union ibv_gid gid{};
        int ret = ibv_query_gid(ctx_, port_num, gid_index, &gid);

        if (ret != 0) {
            int err = std::abs(ret);
            return std::unexpected(
                std::format("Failed to query gid {} at port {} : {} ({})", gid_index, port_num, std::strerror(err), err)
            );
        }

        return gid;
    }

    /// \brief Returns the active MTU in bytes for the specified port.
    [[nodiscard]] std::expected<size_t, std::string> get_active_mtu_bytes(uint8_t port_num = 1) const {
        int port_attr = query_port(port_num);
        // Recommendation of using a Rust-style ? as a macro.
        if (!port_attr) return std::unexpected(std::move(ret).error());

        switch (port_attr.active_mtu) {
            case IBV_MTU_256 : return 256;
            case IBV_MTU_512 : return 512;
            case IBV_MTU_1024 : return 1024;
            case IBV_MTU_2048 : return 2048;
            case IBV_MTU_4096 : return 4096;
        }
    }

private:
    explicit Context(struct ibv_context* ctx) : ctx_(ctx) {}
    struct ibv_context* ctx_{nullptr};
};

} // namespace adder::verbs
