#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cerrno>
#include <expected>
#include <algorithm>

// Network socket headers for out-of-band QP parameter exchange
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

// InfiniBand Verbs
#include <infiniband/verbs.h>

// Project Adder shared headers
#include "shared/latency_harness.h"
#include "shared/affinity.h"
#include "shared/busy_poll.h"

namespace {

constexpr size_t kUdGrhOffset = 40; // 40-byte Global Routing Header required for UD receives
constexpr uint32_t kDefaultQKey = 0x11111111;

struct PingPongConfig {
    bool is_server{false};
    std::string server_ip{"127.0.0.1"};
    uint16_t tcp_port{18515};
    std::string device_name{""};
    uint8_t ib_port{1};
    int g_index{0};
    std::string transport{"rc"}; // "rc" or "ud"
    size_t message_size{64};
    size_t iterations{100'000};
    size_t warmup_iterations{5'000};
    bool use_inline{false};
    bool use_rdma_write{false}; // one-sided RDMA_WRITE vs two-sided SEND
    int pin_core{-1};
    std::string output_csv{""};
};

// QP exchange packet passed over TCP side channel
struct QpExchangeInfo {
    uint32_t qpn{0};
    uint16_t lid{0};
    uint32_t psn{0};
    union ibv_gid gid{};
    uint64_t vaddr{0};  // For RDMA_WRITE
    uint32_t rkey{0};   // For RDMA_WRITE
} __attribute__((packed));

// Helper: Convert ibv_mtu enum to actual byte count
size_t mtu_to_bytes(enum ibv_mtu mtu) {
    switch (mtu) {
        case IBV_MTU_256:  return 256;
        case IBV_MTU_512:  return 512;
        case IBV_MTU_1024: return 1024;
        case IBV_MTU_2048: return 2048;
        case IBV_MTU_4096: return 4096;
        default:           return 1024;
    }
}

// TCP Out-of-band side channel to exchange QPN, LID/GID, and PSN
std::expected<QpExchangeInfo, std::string> exchange_qp_info(
    const PingPongConfig& cfg,
    const QpExchangeInfo& local_info
) {
    int sock_fd = -1;
    if (cfg.is_server) {
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) return std::unexpected("Failed to create TCP listen socket");

        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_addr.s_addr = INADDR_ANY;
        serv_addr.sin_port = htons(cfg.tcp_port);

        if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            close(listen_fd);
            return std::unexpected("TCP bind failed on port " + std::to_string(cfg.tcp_port));
        }

        listen(listen_fd, 1);
        std::cout << "[TCP Server] Waiting for peer connection on port " << cfg.tcp_port << "...\n";
        sock_fd = accept(listen_fd, nullptr, nullptr);
        close(listen_fd);
        if (sock_fd < 0) return std::unexpected("TCP accept failed");
    } else {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd < 0) return std::unexpected("Failed to create TCP client socket");

        struct sockaddr_in serv_addr{};
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(cfg.tcp_port);
        if (inet_pton(AF_INET, cfg.server_ip.c_str(), &serv_addr.sin_addr) <= 0) {
            close(sock_fd);
            return std::unexpected("Invalid server IP address: " + cfg.server_ip);
        }

        std::cout << "[TCP Client] Connecting to peer " << cfg.server_ip << ":" << cfg.tcp_port << "...\n";
        int retries = 10;
        while (connect(sock_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
            if (--retries == 0) {
                close(sock_fd);
                return std::unexpected("TCP connection failed to " + cfg.server_ip);
            }
            usleep(100'000);
        }
    }

    // Send local QP information
    if (write(sock_fd, &local_info, sizeof(local_info)) != sizeof(local_info)) {
        close(sock_fd);
        return std::unexpected("TCP write of local QpExchangeInfo failed");
    }

    // Read peer QP information
    QpExchangeInfo remote_info{};
    if (read(sock_fd, &remote_info, sizeof(remote_info)) != sizeof(remote_info)) {
        close(sock_fd);
        return std::unexpected("TCP read of remote QpExchangeInfo failed");
    }

    close(sock_fd);
    return remote_info;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  --server               Run in server/receiver mode\n"
              << "  --client <ip>          Run in client/sender mode and connect to <ip>\n"
              << "  --port <port>          TCP side-channel exchange port (default: 18515)\n"
              << "  --device <dev>         RDMA device name (e.g. rxe0, mlx5_0)\n"
              << "  --ib-port <num>        InfiniBand port number (default: 1)\n"
              << "  --gid-idx <idx>        GID table index (default: 0)\n"
              << "  --transport <rc|ud>    Transport type (default: rc)\n"
              << "  --size <bytes>         Message size in bytes (default: 64)\n"
              << "  --iters <count>        Number of timed iterations (default: 100000)\n"
              << "  --warmup <count>       Number of warmup iterations (default: 5000)\n"
              << "  --inline               Use IBV_SEND_INLINE flag\n"
              << "  --write                Use IBV_WR_RDMA_WRITE instead of IBV_WR_SEND\n"
              << "  --pin-core <id>        Pin process to specific CPU core\n"
              << "  --output <file>        Write latency results to CSV\n";
}

} // namespace

int main(int argc, char* argv[]) {
    PingPongConfig cfg;

    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--server") {
            cfg.is_server = true;
        } else if (arg == "--client" && i + 1 < argc) {
            cfg.is_server = false;
            cfg.server_ip = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            cfg.tcp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--device" && i + 1 < argc) {
            cfg.device_name = argv[++i];
        } else if (arg == "--ib-port" && i + 1 < argc) {
            cfg.ib_port = static_cast<uint8_t>(std::stoi(argv[++i]));
        } else if (arg == "--gid-idx" && i + 1 < argc) {
            cfg.g_index = std::stoi(argv[++i]);
        } else if (arg == "--transport" && i + 1 < argc) {
            cfg.transport = argv[++i];
        } else if (arg == "--size" && i + 1 < argc) {
            cfg.message_size = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--iters" && i + 1 < argc) {
            cfg.iterations = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--warmup" && i + 1 < argc) {
            cfg.warmup_iterations = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--inline") {
            cfg.use_inline = true;
        } else if (arg == "--write") {
            cfg.use_rdma_write = true;
        } else if (arg == "--pin-core" && i + 1 < argc) {
            cfg.pin_core = std::stoi(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            cfg.output_csv = argv[++i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    // Pin core if requested to guarantee cache and latency stability
    if (cfg.pin_core >= 0) {
        auto pin_res = adder::shared::CpuTopology::pin_current_thread(static_cast<uint32_t>(cfg.pin_core));
        if (!pin_res) {
            std::cerr << "[Warning] " << pin_res.error() << "\n";
        } else {
            std::cout << "[Affinity] Pinned thread to CPU core " << cfg.pin_core << "\n";
        }
    }

    std::cout << "=========================================================\n"
              << " Adder RDMA Ping-Pong Latency Benchmark (D1)\n"
              << " Role:       " << (cfg.is_server ? "Server (Receiver first)" : "Client (Sender first)") << "\n"
              << " Transport:  " << cfg.transport << "\n"
              << " Msg Size:   " << cfg.message_size << " bytes\n"
              << " Mode:       " << (cfg.use_rdma_write ? "One-Sided (RDMA_WRITE)" : "Two-Sided (SEND/RECV)") << "\n"
              << " Inline:     " << (cfg.use_inline ? "Enabled" : "Disabled") << "\n"
              << " Iterations: " << cfg.iterations << " (warmup: " << cfg.warmup_iterations << ")\n"
              << "=========================================================\n";

    // =========================================================================
    // Step 1: Find RDMA Device with ibv_get_device_list
    // =========================================================================
    // TODO: Step 1: Enumerate available RDMA devices via ibv_get_device_list().
    // If cfg.device_name is empty, pick the first active device. Otherwise match by name.
    // Call ibv_open_device() to get struct ibv_context*.
    struct ibv_context* ib_ctx = nullptr;
    std::cout << "// TODO: Step 1: Find and open RDMA device (ibv_get_device_list, ibv_open_device)\n";

    // =========================================================================
    // Step 2: Query Port and Allocate Protection Domain (ibv_alloc_pd)
    // =========================================================================
    // TODO: Step 2: Query port attributes with ibv_query_port() to verify PORT_ACTIVE
    // and retrieve the active_mtu. For UD transport, ensure cfg.message_size <= mtu_to_bytes(port_attr.active_mtu).
    // Allocate protection domain using ibv_alloc_pd(ib_ctx).
    struct ibv_pd* pd = nullptr;
    size_t active_mtu_bytes = 1024;
    std::cout << "// TODO: Step 2: Query port active_mtu and allocate PD (ibv_alloc_pd)\n";

    // =========================================================================
    // Step 3: Register Memory Region (ibv_reg_mr)
    // =========================================================================
    // TODO: Step 3: Allocate send and receive buffers.
    // CRITICAL UD RULE: For UD receives, allocate (cfg.message_size + 40) bytes
    // to accommodate the 40-byte Global Routing Header (GRH).
    // Register the buffer with ibv_reg_mr() using flags:
    // IBV_ACCESS_LOCAL_WRITE | (cfg.use_rdma_write ? IBV_ACCESS_REMOTE_WRITE : 0)
    struct ibv_mr* mr = nullptr;
    uint8_t* buf = nullptr;
    std::cout << "// TODO: Step 3: Register memory buffer (account for 40-byte GRH offset on UD)\n";

    // =========================================================================
    // Step 4: Create Completion Queue (ibv_create_cq)
    // =========================================================================
    // TODO: Step 4: Create CQ with ibv_create_cq() with sufficient depth (e.g. 128 entries).
    struct ibv_cq* cq = nullptr;
    std::cout << "// TODO: Step 4: Create Completion Queue (ibv_create_cq)\n";

    // =========================================================================
    // Step 5: Create Queue Pair (ibv_create_qp)
    // =========================================================================
    // TODO: Step 5: Configure ibv_qp_init_attr:
    // Set qp_type = (cfg.transport == "ud" ? IBV_QPT_UD : IBV_QPT_RC).
    // Attach send_cq and recv_cq. Call ibv_create_qp(pd, &qp_init_attr).
    struct ibv_qp* qp = nullptr;
    std::cout << "// TODO: Step 5: Create Queue Pair for " << cfg.transport << "\n";

    // =========================================================================
    // Step 6: Move QP through State Transitions (INIT -> RTR -> RTS)
    // =========================================================================
    // TODO: Step 6: Transition QP to INIT, RTR, RTS.
    // - For RC:
    //   INIT: Set qp_access_flags, port_num, pkey_index.
    //   RTR:  Set path_mtu, dest_qp_num, rq_psn, max_dest_rd_atomic, min_rnr_timer, ah_attr.
    //   RTS:  Set timeout, retry_cnt, rnr_retry, sq_psn, max_rd_atomic.
    // - For UD:
    //   INIT: Set qkey = kDefaultQKey, port_num, pkey_index.
    //   RTR:  Requires NO destination attributes.
    //   RTS:  Set sq_psn only.
    std::cout << "// TODO: Step 6: Implement QP State Machine transitions (INIT -> RTR -> RTS)\n";

    // =========================================================================
    // Step 7: Exchange QP Parameters over TCP Side Channel
    // =========================================================================
    QpExchangeInfo local_info{};
    // TODO: Populate local_info.qpn, local_info.lid/gid, local_info.psn, vaddr, rkey
    std::cout << "// TODO: Step 7: Populate local QP info and exchange over TCP\n";
    auto remote_info_expected = exchange_qp_info(cfg, local_info);
    if (!remote_info_expected) {
        std::cerr << "[Error] QP exchange failed: " << remote_info_expected.error() << "\n";
        // Clean up and exit
    }

    // =========================================================================
    // Step 8: Execute Ping-Pong Benchmark Loop
    // =========================================================================
    // TODO: Step 8: Execute the timed ping-pong loop:
    // Client:
    //   For each iteration:
    //     t_start = NanosecondClock::now_ns()
    //     ibv_post_send()
    //     poll_cq_spin() for send completion
    //     poll_cq_spin() for recv completion
    //     t_end = NanosecondClock::now_ns()
    //     rtt_ns = t_end - t_start
    //     one_way_ns = rtt_ns / 2
    //     record sample in LatencyHistogram (after warmup)
    // Server:
    //   For each iteration:
    //     ibv_post_recv() before send arrives
    //     poll_cq_spin() for recv completion
    //     ibv_post_send() (or RDMA_WRITE) back to client
    //     poll_cq_spin() for send completion
    std::cout << "// TODO: Step 8: Run ping-pong loop and collect 1M+ samples in LatencyHistogram\n";

    adder::shared::LatencyHistogram hist(cfg.iterations);

    // =========================================================================
    // Step 9: Report Results & Output CSV
    // =========================================================================
    hist.sort_samples();
    double p50 = hist.percentile(50.0);
    double p90 = hist.percentile(90.0);
    double p99 = hist.percentile(99.0);
    double p999 = hist.percentile(99.9);
    double p9999 = hist.percentile(99.99);

    std::cout << "\n---------------- Benchmark Summary ----------------\n"
              << " Samples: " << hist.count() << "\n"
              << " P50:     " << p50 / 1000.0 << " us (" << p50 << " ns)\n"
              << " P90:     " << p90 / 1000.0 << " us (" << p90 << " ns)\n"
              << " P99:     " << p99 / 1000.0 << " us (" << p99 << " ns)\n"
              << " P99.9:   " << p999 / 1000.0 << " us (" << p999 << " ns)\n"
              << " P99.99:  " << p9999 / 1000.0 << " us (" << p9999 << " ns)\n"
              << " Mean:    " << hist.mean() / 1000.0 << " us\n"
              << "---------------------------------------------------\n";

    if (!cfg.output_csv.empty()) {
        auto prov = adder::shared::ProvenanceMetadata::capture(
            "pingpong_" + cfg.transport,
            (cfg.pin_core >= 0 ? std::to_string(cfg.pin_core) : "none")
        );
        std::ofstream csv(cfg.output_csv, std::ios::app);
        csv << prov.to_csv_header_comment();
        csv << "message_size,transport,direction,percentile,latency_ns,samples,active_mtu\n";
        for (double p : {50.0, 90.0, 99.0, 99.9, 99.99}) {
            csv << cfg.message_size << ","
                << cfg.transport << ","
                << (cfg.is_server ? "rx" : "tx") << ","
                << p << ","
                << hist.percentile(p) << ","
                << hist.count() << ","
                << active_mtu_bytes << "\n";
        }
        std::cout << "[Output] Results written to " << cfg.output_csv << "\n";
    }

    return 0;
}
