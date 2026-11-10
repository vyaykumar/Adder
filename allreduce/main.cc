#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <chrono>

#include <allreduce/verbs_qp.h>
#include <allreduce/ring.h>
#include <allreduce/tree.h>
#include <shared/verbs.h>
#include <shared/latency_harness.h>
#include <shared/affinity.h>
#include <shared/free_list_pool.h>

#ifdef HAVE_MPI
#include <mpi.h>
#endif

namespace {

struct BenchmarkConfig {
    uint32_t rank{0};
    uint32_t world_size{4};
    std::string algorithm{"ring"}; // "ring" or "tree"
    std::string device_name{""};   // RDMA device (e.g. rxe0, mlx5_0)
    uint8_t ib_port{1};            // InfiniBand port number (default: 1)
    int g_index{1};                // GID index (default: 1 for RoCEv2/IPv4)
    std::string master_ip{"127.0.0.1"};
    uint16_t tcp_port{18515};
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
              << "  --device <dev>        RDMA device name (e.g. rxe0, mlx5_0)\n"
              << "  --ib-port <num>       InfiniBand port number (default: 1)\n"
              << "  --gid-idx <idx>       GID table index (default: 1 for RoCEv2/IPv4)\n"
              << "  --master-ip <ip>      Coordinator / peer base IP (default: 127.0.0.1)\n"
              << "  --port <port>         TCP side-channel base port (default: 18515)\n"
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
        } else if (arg == "--device" && i + 1 < argc) {
            cfg.device_name = argv[++i];
        } else if (arg == "--ib-port" && i + 1 < argc) {
            cfg.ib_port = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--gid-idx" && i + 1 < argc) {
            cfg.g_index = std::stoi(argv[++i]);
        } else if (arg == "--master-ip" && i + 1 < argc) {
            cfg.master_ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.tcp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
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

    // =========================================================================
    // Step 1: Device Enumeration & Shared Context Creation
    // Goal: Enumerate available RDMA devices via adder::verbs::DeviceList,
    // select cfg.device_name, and open the rank's shared Context (identical to D1 Step 1).
    // =========================================================================
    // TODO: Step 1 (DeviceList -> select_device -> Context::open)

    // =========================================================================
    // Step 2: Port Query & Shared Protection Domain Allocation
    // Goal: Verify active port state (IBV_PORT_ACTIVE), query local GID at cfg.g_index,
    // and allocate the single shared ProtectionDomain for all peer QPs on this rank.
    // =========================================================================
    // TODO: Step 2 (context.query_port -> context.query_gid -> ProtectionDomain::alloc)

    // =========================================================================
    // Step 3: Page-Aligned Tensor & Staging Buffer Registration
    // Goal: Register two MemoryRegions on the shared ProtectionDomain:
    //   1. tensor_mr: wraps the rank's data buffer (cfg.message_size bytes).
    //   2. staging_mr: wraps a chunk-sized (or full-buffer for tree) scratchpad
    //      where incoming remote packets land before SIMD accumulation.
    // =========================================================================
    // TODO: Step 3 (MemoryRegion::register_mr for tensor_mr and staging_mr)

    // =========================================================================
    // Step 4: Shared Completion Queue Creation
    // Goal: Create a shared CompletionQueue sized to handle concurrent Send/Recv
    // completions across all peer links (e.g. cq_depth = 256).
    // =========================================================================
    // TODO: Step 4 (CompletionQueue::create)

    // =========================================================================
    // Step 5: Topology Queue Pair Instantiation (Ring or Tree)
    // Goal: Instantiate VerbsQp links attached to the shared Context, PD, and CQ:
    //   - Ring: qp_next (to (rank + 1) % N) and qp_prev (from (rank + N - 1) % N).
    //   - Tree: qp_parent, qp_left, qp_right per TreeTopology::compute().
    // =========================================================================
    // TODO: Step 5 (VerbsQp::init on shared Context/PD/CQ)

    // =========================================================================
    // Step 6: Out-of-Band Parameter Exchange (TCP Ring or MPI_Allgather)
    // Goal: Exchange QpConnectionParams (QPN, LID, PSN, GID, vaddr, rkey) between
    // connected neighbors in the Ring or Binary Tree.
    // =========================================================================
    // TODO: Step 6 (Exchange local and remote QpConnectionParams)

    // =========================================================================
    // Step 7: Queue Pair State Machine Progression (RESET -> INIT -> RTR -> RTS)
    // Goal: Transition all active peer QPs to RTS via VerbsQp::connect_rc(...)
    // (or .transition_to_init -> .transition_to_rtr -> .transition_to_rts).
    // =========================================================================
    // TODO: Step 7 (Arm all peer QPs to RTS and call RingAllReduce::setup_mesh / TreeAllReduce::setup)

    std::cout << "// TODO: Initialize RingAllReduce / TreeAllReduce and QP mesh\n";
    std::cout << "// TODO: Run " << cfg.warmup << " warmup rounds and " << cfg.iterations << " timed rounds\n";

    // =========================================================================
    // Step 8: Collective Benchmark Loop, Bit-Exact Verification & MPI Comparison
    // Goal: Run warmup rounds, then time cfg.iterations collective executions into
    // LatencyHistogram, verify every element against reference[i] == N*(N+1)/2,
    // and optionally benchmark MPI_Allreduce side-by-side when --compare-mpi is set.
    // =========================================================================
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
