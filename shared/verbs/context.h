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
        // TODO: [Implement Device Open]
        // 1. Check if dev == nullptr. If so, return unexpected("Null device pointer").
        // 2. Call `struct ibv_context* ctx = ibv_open_device(dev);`
        // 3. If ctx is nullptr:
        //    Capture error with `std::strerror(errno)` and return unexpected.
        // 4. Return Context(ctx).
        (void)dev;
        return std::unexpected("TODO: Implement Context::open");
    }

    /// \brief Destructor. Closes the verbs context.
    ~Context() {
        // TODO: [Implement Destructor]
        // Pitfall: ibv_close_device() must ONLY be called after all child resources
        // (Queue Pairs, Completion Queues, Memory Regions, Protection Domains)
        // have been destroyed, otherwise it may return EBUSY or trigger kernel driver leaks.
        // If ctx_ != nullptr, call ibv_close_device(ctx_) and set ctx_ = nullptr.
    }

    // Move-only semantics (prevents multiple wrappers attempting to close the same context)
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    Context(Context&& other) noexcept : ctx_(other.ctx_) {
        other.ctx_ = nullptr;
    }

    Context& operator=(Context&& other) noexcept {
        if (this != &other) {
            // TODO: [Implement Move Assignment]
            // 1. Close existing ctx_ if non-null via ibv_close_device().
            // 2. Transfer ctx_ from other.
            // 3. Set other.ctx_ = nullptr.
        }
        return *this;
    }

    /// \brief Raw pointer accessor.
    [[nodiscard]] struct ibv_context* get() const noexcept { return ctx_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ctx_ != nullptr; }

    /// \brief Returns the canonical string name of the RDMA device (e.g. "rxe0").
    [[nodiscard]] std::string_view device_name() const noexcept {
        // TODO: If ctx_ && ctx_->device, call ibv_get_device_name(ctx_->device), else return "".
        return "";
    }

    /// \brief Queries device port attributes (active MTU, LID, link state).
    /// \param port_num 1-based port index (typically 1).
    /// \return Port attributes struct or error description.
    [[nodiscard]] std::expected<struct ibv_port_attr, std::string> query_port(uint8_t port_num = 1) const {
        // TODO: [Implement Port Query]
        // 1. Verify ctx_ != nullptr.
        // 2. Initialize `struct ibv_port_attr port_attr{};`
        // 3. Call `int ret = ibv_query_port(ctx_, port_num, &port_attr);`
        // 4. If ret != 0, return unexpected with strerror(errno).
        // 5. Return port_attr.
        (void)port_num;
        return std::unexpected("TODO: Implement query_port");
    }

    /// \brief Queries a Global Identifier (GID) from the port's GID table.
    ///
    /// For RoCEv2 (Soft-RoCE / rxe), GID index 1 is typically the IPv4-mapped GID.
    ///
    /// \param port_num 1-based port index.
    /// \param gid_index GID table index (0 = RoCEv1 / MAC, 1 = RoCEv2 / IPv4).
    /// \return GID union or error description.
    [[nodiscard]] std::expected<union ibv_gid, std::string> query_gid(uint8_t port_num, int gid_index) const {
        // TODO: [Implement GID Query]
        // 1. Verify ctx_ != nullptr.
        // 2. Initialize `union ibv_gid gid{};`
        // 3. Call `int ret = ibv_query_gid(ctx_, port_num, gid_index, &gid);`
        // 4. If ret != 0, return unexpected with strerror(errno).
        // 5. Return gid.
        (void)port_num;
        (void)gid_index;
        return std::unexpected("TODO: Implement query_gid");
    }

    /// \brief Returns the active MTU in bytes for the specified port.
    [[nodiscard]] std::expected<size_t, std::string> get_active_mtu_bytes(uint8_t port_num = 1) const {
        // TODO: [Implement MTU Conversion]
        // 1. Call query_port(port_num). Return unexpected if failed.
        // 2. Inspect port_attr.active_mtu enum:
        //    IBV_MTU_256  -> 256
        //    IBV_MTU_512  -> 512
        //    IBV_MTU_1024 -> 1024
        //    IBV_MTU_2048 -> 2048
        //    IBV_MTU_4096 -> 4096
        (void)port_num;
        return std::unexpected("TODO: Implement get_active_mtu_bytes");
    }

private:
    explicit Context(struct ibv_context* ctx) : ctx_(ctx) {}
    struct ibv_context* ctx_{nullptr};
};

} // namespace adder::verbs
