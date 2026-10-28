#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <expected>
#include <cstdint>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

namespace adder::shared {

enum class CoreType {
    Unknown,
    Performance, // P-Core
    Efficiency,  // E-Core
    Standard     // Homogeneous architecture (AMD, classic Intel, cloud vCPUs)
};

struct CpuCoreInfo {
    uint32_t cpu_id{0};
    uint32_t core_id{0};
    uint32_t physical_package_id{0};
    uint32_t numa_node{0};
    CoreType core_type{CoreType::Unknown};
    uint64_t max_freq_khz{0};
    bool is_smt_sibling{false};
};

class CpuTopology {
public:
    // Dynamically queries /sys/devices/system/cpu to discover core architecture
    static std::vector<CpuCoreInfo> discover() {
        std::vector<CpuCoreInfo> cores;
        const std::string base_path = "/sys/devices/system/cpu";

        if (!std::filesystem::exists(base_path)) {
            // Fallback for non-Linux or simulated environments
            uint32_t nprocs = static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
            for (uint32_t i = 0; i < nprocs; ++i) {
                cores.push_back(CpuCoreInfo{
                    .cpu_id = i,
                    .core_id = i,
                    .physical_package_id = 0,
                    .numa_node = 0,
                    .core_type = CoreType::Standard,
                    .max_freq_khz = 0,
                    .is_smt_sibling = false
                });
            }
            return cores;
        }

        uint64_t highest_freq = 0;
        uint64_t lowest_freq = UINT64_MAX;

        for (uint32_t cpu = 0; ; ++cpu) {
            std::string cpu_dir = base_path + "/cpu" + std::to_string(cpu);
            if (!std::filesystem::exists(cpu_dir)) {
                break;
            }

            CpuCoreInfo info;
            info.cpu_id = cpu;

            // Read core ID
            std::ifstream core_id_file(cpu_dir + "/topology/core_id");
            if (core_id_file >> info.core_id) {}

            // Read package ID (socket)
            std::ifstream pkg_id_file(cpu_dir + "/topology/physical_package_id");
            if (pkg_id_file >> info.physical_package_id) {}

            // Read maximum frequency to detect hybrid architectures (P-cores vs E-cores)
            std::ifstream freq_file(cpu_dir + "/cpufreq/cpuinfo_max_freq");
            if (freq_file >> info.max_freq_khz) {
                if (info.max_freq_khz > highest_freq) highest_freq = info.max_freq_khz;
                if (info.max_freq_khz < lowest_freq) lowest_freq = info.max_freq_khz;
            }

            // Check thread siblings list to determine primary vs SMT sibling
            std::ifstream siblings_file(cpu_dir + "/topology/thread_siblings_list");
            std::string line;
            if (std::getline(siblings_file, line)) {
                std::stringstream ss(line);
                uint32_t first_cpu = 0;
                char delim = 0;
                if (ss >> first_cpu) {
                    info.is_smt_sibling = (first_cpu != cpu);
                }
            }

            cores.push_back(info);
        }

        // Classify core types if hybrid frequency delta detected (> 15% discrepancy)
        bool is_hybrid = (highest_freq > 0 && lowest_freq > 0 && (highest_freq - lowest_freq) > (highest_freq / 7));
        for (auto& core : cores) {
            if (!is_hybrid) {
                core.core_type = CoreType::Standard;
            } else if (core.max_freq_khz == highest_freq) {
                core.core_type = CoreType::Performance;
            } else {
                core.core_type = CoreType::Efficiency;
            }
        }

        return cores;
    }

    // Selects N homogeneous cores (preferring primary non-SMT siblings of the same core type)
    // Prevents asymmetric hardware stragglers on Intel 12th/13th/14th Gen chips
    static std::vector<uint32_t> get_homogeneous_cores(uint32_t requested_count, CoreType preferred_type = CoreType::Efficiency) {
        auto topology = discover();
        std::vector<uint32_t> selected;

        // Try preferred core type first without SMT siblings
        for (const auto& core : topology) {
            if (selected.size() == requested_count) break;
            if (!core.is_smt_sibling && (core.core_type == preferred_type || core.core_type == CoreType::Standard)) {
                selected.push_back(core.cpu_id);
            }
        }

        // If not enough, try any core of the preferred type
        if (selected.size() < requested_count) {
            for (const auto& core : topology) {
                if (selected.size() == requested_count) break;
                if (core.core_type == preferred_type || core.core_type == CoreType::Standard) {
                    if (std::find(selected.begin(), selected.end(), core.cpu_id) == selected.end()) {
                        selected.push_back(core.cpu_id);
                    }
                }
            }
        }

        // Fallback: fill sequentially
        for (const auto& core : topology) {
            if (selected.size() == requested_count) break;
            if (std::find(selected.begin(), selected.end(), core.cpu_id) == selected.end()) {
                selected.push_back(core.cpu_id);
            }
        }

        return selected;
    }

    // Pins the calling thread to a specific CPU core ID
    static std::expected<void, std::string> pin_thread_to_core(pthread_t thread, uint32_t cpu_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);

        int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
        if (rc != 0) {
            return std::unexpected("Failed to set thread affinity to core " + std::to_string(cpu_id) + " (rc=" + std::to_string(rc) + ")");
        }
        return {};
    }

    static std::expected<void, std::string> pin_current_thread(uint32_t cpu_id) {
        return pin_thread_to_core(pthread_self(), cpu_id);
    }
};

} // namespace adder::shared
