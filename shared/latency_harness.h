#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstdint>
#include <sys/utsname.h>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

namespace adder::shared {

struct ProvenanceMetadata {
    std::string date;
    std::string git_commit;
    std::string hardware_config_id;
    std::string config_name;
    std::string cpu_model;
    std::string kernel_version;
    std::string compiler_version;
    std::string compiler_flags;
    std::string cpu_governor;
    std::string turbo_state;
    std::string pinned_cores;
    std::string mpi_transport; // Optional for MPI comparisons

    static ProvenanceMetadata capture(std::string_view config_name = "default", std::string_view pinned_cores = "none") {
        ProvenanceMetadata meta;
        meta.config_name = config_name;
        meta.pinned_cores = pinned_cores;

        // Current UTC date ISO-8601
        auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf{};
        gmtime_r(&tt, &tm_buf);
        char date_buf[64];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
        meta.date = date_buf;

        // Git commit hash
        FILE* fp = popen("git rev-parse --short HEAD 2>/dev/null", "r");
        if (fp) {
            char buf[64];
            if (fgets(buf, sizeof(buf), fp)) {
                std::string s(buf);
                s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
                meta.git_commit = s;
            }
            pclose(fp);
        }
        if (meta.git_commit.empty()) {
            meta.git_commit = "unknown_commit";
        }

        // Kernel version
        struct utsname uts{};
        if (uname(&uts) == 0) {
            meta.kernel_version = std::string(uts.sysname) + " " + uts.release;
        } else {
            meta.kernel_version = "unknown_kernel";
        }

        // CPU Model
        std::ifstream cpuinfo("/proc/cpuinfo");
        std::string line;
        while (std::getline(cpuinfo, line)) {
            if (line.rfind("model name", 0) == 0) {
                auto pos = line.find(':');
                if (pos != std::string::npos) {
                    meta.cpu_model = line.substr(pos + 2);
                    break;
                }
            }
        }
        if (meta.cpu_model.empty()) {
            meta.cpu_model = "Generic / Virtual CPU";
        }

        // CPU Governor
        std::ifstream gov_file("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
        if (!(gov_file >> meta.cpu_governor)) {
            meta.cpu_governor = "unsupported";
        }

        // Turbo State
        std::ifstream turbo_file("/sys/devices/system/cpu/intel_pstate/no_turbo");
        int no_turbo = -1;
        if (turbo_file >> no_turbo) {
            meta.turbo_state = (no_turbo == 1) ? "disabled" : "enabled";
        } else {
            meta.turbo_state = "unknown";
        }

        // Compiler
#if defined(__clang__)
        meta.compiler_version = "Clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(__GNUC__)
        meta.compiler_version = "GCC " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
        meta.compiler_version = "Unknown Compiler";
#endif

#ifdef CMAKE_CXX_FLAGS
        meta.compiler_flags = CMAKE_CXX_FLAGS;
#else
        meta.compiler_flags = "-O3 -march=native";
#endif

        meta.hardware_config_id = meta.cpu_model + "_" + meta.kernel_version;
        std::replace(meta.hardware_config_id.begin(), meta.hardware_config_id.end(), ' ', '_');

        return meta;
    }

    std::string to_csv_header_comment() const {
        std::ostringstream ss;
        ss << "# provenance: date=" << date
           << ", git_commit=" << git_commit
           << ", hardware_config_id=" << hardware_config_id
           << ", config_name=" << config_name
           << ", cpu_model=\"" << cpu_model << "\""
           << ", kernel_version=\"" << kernel_version << "\""
           << ", compiler_version=\"" << compiler_version << "\""
           << ", compiler_flags=\"" << compiler_flags << "\""
           << ", cpu_governor=" << cpu_governor
           << ", turbo_state=" << turbo_state
           << ", pinned_cores=" << pinned_cores;
        if (!mpi_transport.empty()) {
            ss << ", mpi_transport=" << mpi_transport;
        }
        ss << "\n";
        return ss.str();
    }
};

class LatencyHistogram {
public:
    explicit LatencyHistogram(size_t expected_samples = 1'000'000) {
        samples_.reserve(expected_samples);
    }

    // Hot-path record: appends sample to pre-allocated buffer
    inline void record(uint64_t latency_ns) noexcept {
        samples_.push_back(latency_ns);
    }

    void sort_samples() {
        if (!sorted_) {
            std::sort(samples_.begin(), samples_.end());
            sorted_ = true;
        }
    }

    double percentile(double p) {
        if (samples_.empty()) return 0.0;
        sort_samples();
        if (p <= 0.0) return static_cast<double>(samples_.front());
        if (p >= 100.0) return static_cast<double>(samples_.back());

        double rank = (p / 100.0) * static_cast<double>(samples_.size() - 1);
        size_t lower = static_cast<size_t>(std::floor(rank));
        size_t upper = static_cast<size_t>(std::ceil(rank));
        double weight = rank - static_cast<double>(lower);

        return static_cast<double>(samples_[lower]) * (1.0 - weight) +
               static_cast<double>(samples_[upper]) * weight;
    }

    double mean() const {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (auto val : samples_) {
            sum += static_cast<double>(val);
        }
        return sum / static_cast<double>(samples_.size());
    }

    uint64_t min() {
        sort_samples();
        return samples_.empty() ? 0 : samples_.front();
    }

    uint64_t max() {
        sort_samples();
        return samples_.empty() ? 0 : samples_.back();
    }

    size_t count() const noexcept {
        return samples_.size();
    }

    void reset() noexcept {
        samples_.clear();
        sorted_ = false;
    }

private:
    std::vector<uint64_t> samples_;
    bool sorted_{false};
};

// High-resolution timestamping
class NanosecondClock {
public:
    static inline uint64_t now_ns() noexcept {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
    }
};

} // namespace adder::shared
