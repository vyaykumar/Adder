#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cmath>

#include "shared/latency_harness.h"

int main(int argc, char* argv[]) {
    std::cout << "=========================================================\n"
              << " Adder In-Network Aggregation Benchmark Driver (D4)\n"
              << "=========================================================\n";

    size_t chunk_elements = 256;
    uint32_t world_size = 4;
    std::string output_csv = "../results/innet_bytes_per_node.csv";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--world-size" && i + 1 < argc) {
            world_size = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--output" && i + 1 < argc) {
            output_csv = argv[++i];
        }
    }

    // Measure bytes sent per node:
    // In standard ring: 2 * (N - 1) / N * S
    // In in-network aggregation: S
    size_t data_bytes = chunk_elements * sizeof(int32_t);
    double ring_bytes = 2.0 * static_cast<double>(world_size - 1) / static_cast<double>(world_size) * static_cast<double>(data_bytes);
    double innet_bytes = static_cast<double>(data_bytes);

    std::cout << "[Analysis] Data Size S:       " << data_bytes << " bytes\n"
              << "  - Standard Ring Volume:     " << ring_bytes << " bytes/node\n"
              << "  - In-Network Agg Volume:    " << innet_bytes << " bytes/node\n"
              << "  - Network Volume Reduction: " << (1.0 - (innet_bytes / ring_bytes)) * 100.0 << "%\n";

    // TODO: Step 1: Send fixed-point gradients over UDP to aggregator
    // TODO: Step 2: Compare latency against SocketRingBaseline
    // TODO: Step 3: Compute quantization error against double/float sum

    auto prov = adder::shared::ProvenanceMetadata::capture("innet_agg");
    std::ofstream csv(output_csv, std::ios::app);
    csv << prov.to_csv_header_comment();
    csv << "world_size,data_size_bytes,ring_bytes_per_node,innet_bytes_per_node,reduction_pct\n";
    csv << world_size << ","
        << data_bytes << ","
        << ring_bytes << ","
        << innet_bytes << ","
        << (1.0 - (innet_bytes / ring_bytes)) * 100.0 << "\n";

    std::cout << "[Output] Results written to " << output_csv << "\n";
    return 0;
}
