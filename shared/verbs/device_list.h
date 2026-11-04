#pragma once

#include <infiniband/verbs.h>
#include <expected>
#include <string>
#include <string_view>
#include <span>
#include <system_error>

namespace adder::verbs {

/// \brief RAII wrapper for RDMA device enumeration via libibverbs.
///
/// Encapsulates `ibv_get_device_list()` and guarantees safe resource release
/// via `ibv_free_device_list()` upon destruction or move reassignment.
///
/// \see docs/man/01_rdma_and_verbs_core.md Section 3 (Control Plane Hierarchy)
/// \see docs/man/11_common_functions_pitfalls_and_best_practices.md (ibv_get_device_list)
class DeviceList {
public:
    /// \brief Default constructor. Enumerates available RDMA devices.
    DeviceList() {
        ibv_device** devices = ibv_get_device_list(&count_);
        if (devices == nullptr) {
            count_ = 0;
            list_ = nullptr;
        }
        else list_ = devices;
    }

    /// \brief Destructor. Releases the device list pointer array.
    ~DeviceList() {
        if (list_ != nullptr) {
            ibv_free_device_list(list_);
            list_ = nullptr;
        }
    }

    // Move-only semantics (prevents double-freeing device list array)
    DeviceList(const DeviceList&) = delete;
    DeviceList& operator=(const DeviceList&) = delete;

    DeviceList(DeviceList&& other) noexcept
        : list_(other.list_), count_(other.count_) {
        other.list_ = nullptr;
        other.count_ = 0;
    }

    DeviceList& operator=(DeviceList&& other) noexcept {
        if (this != &other) {
            if (list_ != nullptr) ibv_free_device_list(list_);
            list_ = other.list_;
            count_ = other.count_;

            other.list_ = nullptr;
            other.count_ = 0;
        }
        return *this;
    }

    /// \brief Returns the number of RDMA devices discovered on this host.
    [[nodiscard]] size_t size() const noexcept {
        return static_cast<size_t>(count_);
    }

    /// \brief Checks if no RDMA devices were found.
    [[nodiscard]] bool empty() const noexcept {
        return count_ == 0 or list_ == nullptr ;
    }

    /// \brief Array indexing operator.
    [[nodiscard]] ibv_device* operator[](size_t index) const noexcept {
        return (index < static_cast<size_t>(count_)) ? list_[index] : nullptr;
    }

    // Iterator support for C++20/C++23 range-for loops:
    //   for (ibv_device* dev : DeviceList{}) { ... }
    [[nodiscard]] ibv_device** begin() const noexcept {
        return list_;
    }

    [[nodiscard]] ibv_device** end() const noexcept {
        return list_ + count_;
    }

    /// \brief Find a device matching an exact string name (e.g. "rxe0", "mlx5_0").
    /// \param name The device name to search for.
    /// \return Pointer to ibv_device if found, or nullptr.
    [[nodiscard]] ibv_device* find_by_name(std::string_view name) const noexcept {
        if (!list_ || count_ <= 0) return nullptr;

        for (int idx {0}; idx < count_; idx++) {
            const char *dev_name = ibv_get_device_name(list_[idx]);
            if (dev_name and name == dev_name) return list_[idx];
        }
        return nullptr;
    }

    /// \brief Returns the first available device, or nullptr if none exist.
    [[nodiscard]] ibv_device* first_or_default() const noexcept {
        return list_ and count_ > 0 ? list_[0] : nullptr;
    }

    /// \brief Select device by name, falling back to the first discovered device if name is empty.
    /// \param preferred_name Name of device, or empty string.
    /// \return Pointer to ibv_device if available, or error string.
    [[nodiscard]] std::expected<ibv_device*, std::string> select_device(std::string_view preferred_name) const {
        if (empty()) return std::unexpected("No RDMA devices found on host...");

        if (preferred_name.empty()) {
            ibv_device* device = find_by_name(preferred_name);
            if (device == nullptr)
                return std::unexpected("Specified device not found: " + std::string(preferred_name));
            return device;
        }

        return first_or_default();
    }

private:
    ibv_device** list_{nullptr};
    int count_{0};
};

} // namespace adder::verbs
