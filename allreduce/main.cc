#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <chrono>

#include "allreduce/verbs_qp.h"
#include "allreduce/ring.h"
#include "allreduce/tree.h"
#include "shared/latency_harness.h"
#include "shared/affinity.h"
#include "shared/free_list_pool.h"

#ifdef HAVE_MPI
#include <mpi.h>
#endif

namespace {

struct BenchmarkConfig {
    uint32_t rank{0};
    uint32_t world_size{4};
    std::string algorithm{"ring"}; // "ring" or "tree"
    size_t message_size{1024 * 1024}; // 1MB default
    size_t iterations{1000};
    size_t warmup{100};
    int pin_core{-1};
    bool check_correctness{true};
    bool run_mpi_comparison{false};
    std::string output_csv{""};
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --rank <id>           Node rank (0 to world_size - 1)\n"
              << "  --world-size <N>      Total number of nodes (2, 4, 8)\n"
              << "  --algorithm <alg>     Collective algorithm: ring, tree (default: ring)\n"
              << "  --size <bytes>        Message size in bytes (1024 to 16777216)\n"
              << "  --iters <count>       Timed iterations (default: 1000)\n"
              << "  --warmup <count>      Warmup iterations (default: 100)\n"
              << "  --pin-core <id>       Pin node to core ID\n"
              << "  --check               Verify bit-exact reduction against reference\n"
              << "  --compare-mpi         Run side-by-side MPI_Allreduce comparison\n"
              << "  --output <file>       CSV output file\n";
}

} // namespace

int main(int argc, char* argv[]) {
#ifdef HAVE_MPI
    MPI_Init(&argc, &argv);
#endif

    BenchmarkConfig cfg;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--rank" && i + 1 < argc) {
            cfg.rank = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--world-size" && i + 1 < argc) {
            cfg.world_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--algorithm" && i + 1 < argc) {
            cfg.algorithm = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            cfg.message_size = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--iters" && i + 1 < argc) {
            cfg.iterations = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--warmup" && i + 1 < argc) {
            cfg.warmup = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--pin-core" && i + 1 < argc) {
            cfg.pin_core = std::stoi(argv[++i]);
        } else if (arg == "--check") {
            cfg.check_correctness = true;
        } else if (arg == "--compare-mpi") {
            cfg.run_mpi_comparison = true;
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

    if (cfg.rank == 0) {
        std::cout << "=========================================================\n"
                  << " Adder All-Reduce Benchmark (D2)\n"
                  << " Algorithm:   " << cfg.algorithm << "\n"
                  << " World Size:  " << cfg.world_size << "\n"
                  << " Buffer Size: " << cfg.message_size / 1024 << " KB (" << cfg.message_size << " B)\n"
                  << " Iterations:  " << cfg.iterations << " (warmup: " << cfg.warmup << ")\n"
                  << "=========================================================\n";
    }

    // Allocate memory buffer with integer elements for bit-exact verification
    const size_t num_elements = cfg.message_size / sizeof(int32_t);
    std::vector<int32_t> buffer(num_elements, static_cast<int32_t>(cfg.rank + 1));
    std::vector<int32_t> reference(num_elements, 0);

    // Theoretical bit-exact sum: sum_{r=1}^N r = N*(N+1)/2
    int32_t expected_sum = static_cast<int32_t>(cfg.world_size * (cfg.world_size + 1) / 2);
    for (size_t i = 0; i < num_elements; ++i) {
        reference[i] = expected_sum;
    }

    // Benchmark harness
    adder::shared::LatencyHistogram hist(cfg.iterations);

    std::cout << "// TODO: Initialize RingAllReduce / TreeAllReduce and QP mesh\n";
    std::cout << "// TODO: Run " << cfg.warmup << " warmup rounds and " << cfg.iterations << " timed rounds\n";

    // Timing simulation placeholder for scaffolding
    for (size_t iter = 0; iter < cfg.iterations; ++iter) {
        uint64_t t0 = adder::shared::NanosecondClock::now_ns();
        // TODO: invoke allreduce execute()
        uint64_t t1 = adder::shared::NanosecondClock::now_ns();
        hist.record(t1 - t0);
    }

    hist.sort_samples();
    double p50_ns = hist.percentile(50.0);
    double p99_ns = hist.percentile(99.0);
    double mean_ns = hist.mean();

    // Collective effective bandwidth = Data Size (bytes) / elapsed time (sec)
    double time_sec = mean_ns / 1e9;
    double bw_gbps = (time_sec > 0.0) ? (static_cast<double>(cfg.message_size) * 8.0 / (time_sec * 1e9)) : 0.0;

    if (cfg.rank == 0) {
        std::cout << "\n---------------- Benchmark Summary ----------------\n"
                  << " Nodes:        " << cfg.world_size << "\n"
                  << " Mean Latency: " << mean_ns / 1000.0 << " us\n"
                  << " P50 Latency:  " << p50_ns / 1000.0 << " us\n"
                  << " P99 Latency:  " << p99_ns / 1000.0 << " us\n"
                  << " Effective BW: " << bw_gbps << " Gbps\n"
                  << "---------------------------------------------------\n";

        if (!cfg.output_csv.empty()) {
            auto prov = adder::shared::ProvenanceMetadata::capture("allreduce_" + cfg.algorithm);
            std::ofstream csv(cfg.output_csv, std::ios::app);
            csv << prov.to_csv_header_comment();
            csv << "algorithm,world_size,message_size,mean_latency_ns,p50_ns,p99_ns,bandwidth_gbps,samples\n";
            csv << cfg.algorithm << ","
                << cfg.world_size << ","
                << cfg.message_size << ","
                << mean_ns << ","
                << p50_ns << ","
                << p99_ns << ","
                << bw_gbps << ","
                << hist.count() << "\n";
            std::cout << "[Output] Results written to " << cfg.output_csv << "\n";
        }
    }

#ifdef HAVE_MPI
    MPI_Finalize();
#endif
    return 0;
}
