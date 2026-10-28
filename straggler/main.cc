#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <chrono>

#include "straggler/detector.h"
#include "straggler/scheduler.h"
#include "straggler/injection.h"
#include "shared/latency_harness.h"
#include "shared/affinity.h"

namespace {

struct StragglerBenchmarkConfig {
    uint32_t rank{0};
    uint32_t world_size{4};
    size_t total_chunks{16};
    size_t iterations{1000};
    size_t warmup{100};
    bool enable_mitigation{false};
    uint32_t straggler_rank{2};
    uint64_t injected_delay_us{500}; // 500us delay
    int pin_core{-1};
    std::string output_csv{""};
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --rank <id>               Node rank\n"
              << "  --world-size <N>          World size (default: 4)\n"
              << "  --chunks <count>          Total chunks in reduction (default: 16)\n"
              << "  --mitigate                Enable adaptive straggler chunk rescheduling\n"
              << "  --inject-rank <id>        Rank to inject synthetic delay (default: 2)\n"
              << "  --delay-us <us>           Injected delay in microseconds (default: 500)\n"
              << "  --iters <count>           Iterations (default: 1000)\n"
              << "  --pin-core <id>           Pin thread to CPU core\n"
              << "  --output <file>           CSV output file\n";
}

} // namespace

int main(int argc, char* argv[]) {
    StragglerBenchmarkConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--rank" && i + 1 < argc) {
            cfg.rank = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--world-size" && i + 1 < argc) {
            cfg.world_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--chunks" && i + 1 < argc) {
            cfg.total_chunks = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--mitigate") {
            cfg.enable_mitigation = true;
        } else if (arg == "--inject-rank" && i + 1 < argc) {
            cfg.straggler_rank = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--delay-us" && i + 1 < argc) {
            cfg.injected_delay_us = static_cast<uint64_t>(std::stoul(argv[++i]));
        } else if (arg == "--iters" && i + 1 < argc) {
            cfg.iterations = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--pin-core" && i + 1 < argc) {
            cfg.pin_core = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            cfg.output_csv = argv[++i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if (cfg.pin_core >= 0) {
        adder::shared::CpuTopology::pin_current_thread(static_cast<uint32_t>(cfg.pin_core));
    }

    std::cout << "=========================================================\n"
              << " Adder Straggler Scheduling Benchmark (D3)\n"
              << " Rank:            " << cfg.rank << "\n"
              << " World Size:      " << cfg.world_size << "\n"
              << " Mitigation:      " << (cfg.enable_mitigation ? "Enabled (Adaptive)" : "Disabled (Static)") << "\n"
              << " Target Straggler:" << cfg.straggler_rank << " (" << cfg.injected_delay_us << " us delay)\n"
              << "=========================================================\n";

    adder::straggler::StragglerDetector detector(cfg.world_size);
    adder::straggler::AdaptiveScheduler scheduler(cfg.world_size, static_cast<uint32_t>(cfg.total_chunks));
    adder::straggler::StragglerInjector injector(
        cfg.straggler_rank,
        adder::straggler::InjectionType::SoftwareDelay,
        cfg.injected_delay_us * 1000ULL
    );

    adder::shared::LatencyHistogram hist(cfg.iterations);

    std::cout << "// TODO: Run all-reduce loop with injection and epoch-based rescheduling\n";

    for (size_t round = 0; round < cfg.iterations; ++round) {
        uint64_t t0 = adder::shared::NanosecondClock::now_ns();

        // Inject delay on target rank
        injector.maybe_inject(cfg.rank, round);

        // TODO: Execute chunk reduction per active schedule
        uint64_t t1 = adder::shared::NanosecondClock::now_ns();
        uint64_t round_latency_ns = t1 - t0;
        hist.record(round_latency_ns);

        if (cfg.rank == 0) {
            // Rank 0 monitors round completions
            detector.record_completion(cfg.straggler_rank, round_latency_ns);
            if (cfg.enable_mitigation && detector.check_and_update_stragglers()) {
                auto new_sched = scheduler.rebalance(detector, round, scheduler.active_schedule().epoch);
                // TODO: Broadcast new_sched over TCP side-channel with epoch and start_round
                scheduler.apply_schedule(new_sched);
            }
        }
    }

    hist.sort_samples();
    double p50 = hist.percentile(50.0);
    double p99 = hist.percentile(99.0);
    double p999 = hist.percentile(99.9);

    if (cfg.rank == 0) {
        std::cout << "\n---------------- Benchmark Summary ----------------\n"
                  << " Mitigation: " << (cfg.enable_mitigation ? "YES" : "NO") << "\n"
                  << " P50 Latency: " << p50 / 1000.0 << " us\n"
                  << " P99 Latency: " << p99 / 1000.0 << " us\n"
                  << " P99.9 Latency: " << p999 / 1000.0 << " us\n"
                  << "---------------------------------------------------\n";

        if (!cfg.output_csv.empty()) {
            auto prov = adder::shared::ProvenanceMetadata::capture("straggler_test");
            std::ofstream csv(cfg.output_csv, std::ios::app);
            csv << prov.to_csv_header_comment();
            csv << "mitigation_enabled,straggler_delay_us,p50_ns,p99_ns,p999_ns,samples\n";
            csv << (cfg.enable_mitigation ? 1 : 0) << ","
                << cfg.injected_delay_us << ","
                << p50 << ","
                << p99 << ","
                << p999 << ","
                << hist.count() << "\n";
            std::cout << "[Output] Results written to " << cfg.output_csv << "\n";
        }
    }

    return 0;
}
