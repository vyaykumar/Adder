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
        // TODO: [Implement Constructor]
        // 1. Call `ibv_get_device_list(&count_)`.
        // 2. If the return pointer is nullptr:
        //    - Set count_ = 0 and list_ = nullptr.
        // 3. Otherwise store the pointer in `list_`.
    }

    /// \brief Destructor. Releases the device list pointer array.
    ~DeviceList() {
        // TODO: [Implement Destructor]
        // 1. If list_ is non-null, call `ibv_free_device_list(list_)`.
        // 2. Set list_ = nullptr.
        // Pitfall: ibv_free_device_list only frees the array of pointers,
        // it does NOT close contexts opened via ibv_open_device().
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
            // TODO: [Implement Move Assignment]
            // 1. Clean up existing list_ if non-null via ibv_free_device_list().
            // 2. Transfer list_ and count_ from other.
            // 3. Reset other.list_ to nullptr and other.count_ to 0.
        }
        return *this;
    }

    /// \brief Returns the number of RDMA devices discovered on this host.
    [[nodiscard]] size_t size() const noexcept {
        // TODO: Return count_ as size_t
        return 0;
    }

    /// \brief Checks if no RDMA devices were found.
    [[nodiscard]] bool empty() const noexcept {
        // TODO: Return true if count_ == 0 or list_ == nullptr
        return true;
    }

    /// \brief Array indexing operator.
    [[nodiscard]] ibv_device* operator[](size_t index) const noexcept {
        // TODO: Return list_[index] if index < count_, otherwise nullptr
        (void)index;
        return nullptr;
    }

    // Iterator support for C++20/C++23 range-for loops:
    //   for (ibv_device* dev : DeviceList{}) { ... }
    [[nodiscard]] ibv_device** begin() const noexcept {
        // TODO: Return pointer to start of device list array
        return nullptr;
    }

    [[nodiscard]] ibv_device** end() const noexcept {
        // TODO: Return pointer to one past the end of array (list_ + count_)
        return nullptr;
    }

    /// \brief Find a device matching an exact string name (e.g. "rxe0", "mlx5_0").
    /// \param name The device name to search for.
    /// \return Pointer to ibv_device if found, or nullptr.
    [[nodiscard]] ibv_device* find_by_name(std::string_view name) const noexcept {
        // TODO: [Implement Device Lookup]
        // 1. If list_ == nullptr or count_ == 0, return nullptr.
        // 2. Loop through i in [0, count_):
        //    - Query name via `const char* dev_name = ibv_get_device_name(list_[i]);`
        //    - If dev_name matches `name`, return list_[i].
        // 3. Return nullptr if not found.
        (void)name;
        return nullptr;
    }

    /// \brief Returns the first available device, or nullptr if none exist.
    [[nodiscard]] ibv_device* first_or_default() const noexcept {
        // TODO: Return list_[0] if count_ > 0 and list_ != nullptr, else nullptr
        return nullptr;
    }

    /// \brief Select device by name, falling back to the first discovered device if name is empty.
    /// \param preferred_name Name of device, or empty string.
    /// \return Pointer to ibv_device if available, or error string.
    [[nodiscard]] std::expected<ibv_device*, std::string> select_device(std::string_view preferred_name) const {
        // TODO: [Implement Device Selection]
        // 1. If empty(), return std::unexpected("No RDMA devices found on host...").
        // 2. If !preferred_name.empty():
        //    - Call find_by_name(preferred_name).
        //    - If not found, return std::unexpected("Specified device not found: ...").
        //    - Return matched device pointer.
        // 3. Otherwise return first_or_default().
        (void)preferred_name;
        return std::unexpected("TODO: Implement select_device");
    }

private:
    ibv_device** list_{nullptr};
    int count_{0};
};

} // namespace adder::verbs
