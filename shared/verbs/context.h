#pragma once

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <format>
#include <infiniband/verbs.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace adder::verbs {

/// \brief RAII wrapper for an opened RDMA device context (`ibv_context`).
///
/// \see docs/man/01_rdma_and_verbs_core.md Section 3 (Control Plane Object Lifecycle)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_open_device / ibv_close_device)
class Context {
public:
    [[nodiscard]] static std::expected<Context, std::string> open(struct ibv_device* dev) {
        if (!dev) return std::unexpected("[Context::open] Device pointer is null");

        struct ibv_context* ctx = ibv_open_device(dev);
        if (!ctx) {
            int err = errno;
            auto* name = ibv_get_device_name(dev);
            return std::unexpected (
                std::format("[Context::open] Failed to open device '{}' : {} ({})",
                    name ? name : "unknown",
                    std::generic_category().message(err),
                    err
                )
            );
        }

        return Context(ctx);
    }

    ~Context() {
        if (ctx_) {
            ibv_close_device(ctx_);
            ctx_ = nullptr;
        }
    }

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

    [[nodiscard]] struct ibv_context* get() const noexcept { return ctx_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ctx_ != nullptr; }

    /// \brief Returns the canonical string name of the RDMA device (e.g. "rxe0").
    [[nodiscard]] std::string_view device_name() const noexcept {
        return ctx_ and ctx_->device ? ibv_get_device_name(ctx_->device) : "";
    }

    /// \brief Queries device port attributes (active MTU, LID, link state).
    [[nodiscard]] std::expected<struct ibv_port_attr, std::string> query_port(uint8_t port_num = 1) const {
        if (!ctx_) return std::unexpected("[Context::query_port] Device context is uninitialized or closed");

        struct ibv_port_attr port_attr{};
        int ret = ibv_query_port(ctx_, port_num, &port_attr);

        if (ret != 0) {
            int err = (ret > 0) ? ret : errno;
            return std::unexpected(
                std::format("[Context::query_port] Failed to query port {} on device '{}': {} ({})",
                    port_num,
                    device_name(),
                    std::generic_category().message(err),
                    err
                )
            );
        }

        return port_attr;
    }

    /// \brief Queries a Global Identifier (GID) from the port's GID table.
    /// \param port_num 1-based port index.
    /// \param gid_index GID table index (0 = RoCEv1 / MAC, 1 = RoCEv2 / IPv4).
    [[nodiscard]] std::expected<union ibv_gid, std::string> query_gid(uint8_t port_num, int gid_index) const {
        if (!ctx_) return std::unexpected("[Context::query_gid] Device context is uninitialized or closed");

        union ibv_gid gid{};
        int ret = ibv_query_gid(ctx_, port_num, gid_index, &gid);

        if (ret != 0) {
            int err = (ret > 0) ? ret : errno;
            return std::unexpected(
                std::format("[Context::query_gid] Failed to query gid {} at port {} for '{}': {} ({})",
                    gid_index,
                    port_num,
                    device_name(),
                    std::generic_category().message(err),
                    err)
            );
        }

        return gid;
    }

    /// \brief Returns the active MTU in bytes for the specified port.
    [[nodiscard]] std::expected<size_t, std::string> get_active_mtu_bytes(uint8_t port_num = 1) const {
        auto port_attr = query_port(port_num);
        // Recommendation of using a Rust-style ? as a macro.
        if (!port_attr) return std::unexpected(std::move(port_attr).error());

        switch (port_attr->active_mtu) {
            case IBV_MTU_256 : return 256;
            case IBV_MTU_512 : return 512;
            case IBV_MTU_1024 : return 1024;
            case IBV_MTU_2048 : return 2048;
            case IBV_MTU_4096 : return 4096;
            default: return std::unexpected(
                std::format(
                    "[Context::get_active_mtu_bytes] Unrecognized active MTU enum ({}) on port {} for '{}'",
                    static_cast<int> (port_attr->active_mtu),
                    port_num,
                    device_name()
                )
            );
        }
    }

private:
    explicit Context(struct ibv_context* ctx) : ctx_(ctx) {}
    struct ibv_context* ctx_{nullptr};
};

} // namespace adder::verbs
