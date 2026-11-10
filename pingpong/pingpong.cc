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
#include <cassert>

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
#include "shared/verbs.h"

namespace {

constexpr size_t kUdGrhOffset = 40; // 40-byte Global Routing Header required for UD receives
constexpr uint32_t kDefaultQKey = 0x11111111;

struct PingPongConfig {
    bool is_server{false};
    std::string server_ip{"127.0.0.1"};
    uint16_t tcp_port{18515};
    std::string device_name{""};
    uint8_t ib_port{1};
    int g_index{1}; // Priority 2.3: Default to GID index 1 (RoCEv2 / IPv4) for Soft-RoCE (rxe)
    std::string transport{"rc"}; // "rc" or "ud"
    size_t message_size{64};
    size_t iterations{100'000};
    size_t warmup_iterations{5'000};
    bool use_inline{false};
    bool use_rdma_write{false}; // one-sided RDMA_WRITE vs two-sided SEND
    int pin_core{-1};
    std::string output_csv{""};
};

using adder::verbs::RemoteEndpoint;

// Priority 2.1: Robust streaming helpers to prevent TCP fragmentation bugs
inline bool write_exact(int fd, const void* buf, size_t len) {
    const char* p = static_cast<const char*>(buf);
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

inline bool read_exact(int fd, void* buf, size_t len) {
    char* p = static_cast<char*>(buf);
    while (len > 0) {
        ssize_t n = read(fd, p, len);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += n;
        len -= static_cast<size_t>(n);
    }
    return true;
}

// TCP Out-of-band side channel to exchange QPN, LID/GID, PSN, and memory keys
std::expected<RemoteEndpoint, std::string> exchange_qp_info(
    const PingPongConfig& cfg,
    const RemoteEndpoint& local_info
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
        serv_addr.sin_addr.s_addr = INADDR_ANY;
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

    // Send local QP information using write_exact
    if (!write_exact(sock_fd, &local_info, sizeof(local_info))) {
        close(sock_fd);
        return std::unexpected("TCP write of local QpExchangeInfo failed");
    }

    // Read peer QP information using read_exact
    RemoteEndpoint remote_info{};
    if (!read_exact(sock_fd, &remote_info, sizeof(remote_info))) {
        close(sock_fd);
        return std::unexpected("TCP read of remote RemoteEndpoint failed");
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
              << "  --gid-idx <idx>        GID table index (default: 1 for RoCEv2/IPv4)\n"
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
    using namespace adder::verbs;

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
              << " GID Index:  " << cfg.g_index << "\n"
              << "=========================================================\n";

    const auto role = cfg.is_server ? "[Server]" : "[Client]";

    // Replication of Rust's ? operator. Didn't want macro stuff like absl.
    auto check = [&](auto&& res) -> bool {
        if (!res) {
            std::cerr << role << " [Error] " << res.error() << "\n";
            return false;
        }
        return true;
    };

    // =========================================================================
    // Step 1: Device Enumeration & Context Creation
    // Goal: Enumerate available RDMA devices, select the target device matching
    // configuration (or default to first active device), and open an operational context.
    // =========================================================================

    auto device_list = DeviceList{};
    if (device_list.empty()) { std::cerr << role << " " << "DeviceList returned empty\n"; return 1; }

    auto device_ex = device_list.select_device(cfg.device_name);
    // if (!device_ex) { std::cerr << role << " [Error] " << device_ex.error() << "\n"; return 1; }
    if (!check(device_ex)) return 1;
    const auto device = std::move(*device_ex);

    auto context_ex = Context::open(device);
    // if(!context_ex) { std::cerr << role << " [Error] " << context_ex.error() << "\n"; return 1; }
    if (!check(context_ex)) return 1;
    const Context context = std::move(*context_ex);

    // Q: Use Immediately Invoked Lambdas (IIFE), instead of the `if (!capture) {}` code?
    // A: Maybe, but here the linear flow is more readable. The monadics add indentation creep, plus aren't easy in debugger.

    // Use Client or Server, instead of "main"? Done.

    // Q: Are the checks necessary, or should the program be simply left to crash?
    // A: Necessary.

    // =========================================================================
    // Step 2: Port Query & Protection Domain Allocation
    // Goal: Query the physical port to verify active link state (PORT_ACTIVE),
    // resolve the active MTU, and allocate a Protection Domain for resource isolation.
    // =========================================================================

    auto port_ex = context.query_port(cfg.ib_port);
    if (!check(port_ex)) return 1;
    if (port_ex->state != IBV_PORT_ACTIVE)
        { std::cerr << role << " [Error] Port (" <<cfg.ib_port<< ") is inactive\n"; return 1; }
    const auto port = std::move(*port_ex);

    auto bytes_ex = context.get_active_mtu_bytes(cfg.ib_port);
    if (!check(bytes_ex)) return 1;
    const auto active_mtu_bytes = std::move(*bytes_ex);

    // Validate UD message size against active MTU
    if (cfg.transport == "ud" && cfg.message_size > active_mtu_bytes) {
        std::cerr << role << " [Error] "
                  << "[Error] UD message size (" << cfg.message_size
                  << " B) exceeds active MTU (" << active_mtu_bytes
                  << " B). Set interface MTU to 9000 or reduce message size.\n";
        return 1;
    }

    auto gid_ex = context.query_gid(cfg.ib_port, cfg.g_index);
    if (!check(gid_ex)) return 1;
    const auto gid = std::move(*gid_ex);

    auto protection_domain_ex = ProtectionDomain::alloc(context);
    if (!check(protection_domain_ex)) return 1;
    const auto protection_domain = std::move(*protection_domain_ex);

    // =========================================================================
    // Step 3: Page-Aligned Buffer Allocation & Memory Registration
    // Goal: Allocate a page-aligned contiguous memory buffer (accounting for the
    // 40-byte GRH offset on UD receives) and register it with the Protection Domain.
    // =========================================================================

    const auto alloc_size = cfg.message_size + (cfg.transport == "ud" ? kUdGrhOffset : 0);
    int access_flags = IBV_ACCESS_LOCAL_WRITE | (cfg.use_rdma_write ? IBV_ACCESS_REMOTE_WRITE : 0) ;

    struct CleanupCrew {
        void operator() (std::byte* pointer) const noexcept {
            ::operator delete[] (pointer, std::align_val_t {4096});
        }
    };
    std::unique_ptr<std::byte[], CleanupCrew> buffer (new (std::align_val_t {4096}) std::byte[alloc_size]);

    auto memory_region_ex = MemoryRegion::register_buffer(protection_domain, buffer.get(), alloc_size, access_flags);
    if (!check(memory_region_ex)) return 1;
    const auto memory_region = std::move(*memory_region_ex);

    // =========================================================================
    // Step 4: Completion Queue (CQ) Creation
    // Goal: Instantiate a Completion Queue sized to hold all in-flight work
    // completions for the benchmark iterations.
    // =========================================================================

    auto completion_queue_ex = CompletionQueue::create(context);
    if (!check(completion_queue_ex)) return 1;
    auto completion_queue = std::move(*completion_queue_ex);

    // =========================================================================
    // Step 5: Queue Pair (QP) Instantiation
    // Goal: Configure and instantiate a Queue Pair attached to the Protection Domain
    // and Completion Queue for the selected transport (RC or UD), setting inline capacity.
    // =========================================================================

    QpConfig qp_cfg {};
    qp_cfg.qp_type = (cfg.transport == "ud") ? IBV_QPT_UD : IBV_QPT_RC;
    qp_cfg.max_send_wr = 128;
    qp_cfg.max_recv_wr = 128;
    qp_cfg.max_inline_data = cfg.use_inline ? 256u : 0u;

    auto queue_pair_ex = QueuePair::create(protection_domain, completion_queue, completion_queue, qp_cfg);
    if (!check(queue_pair_ex)) return 1;
    auto queue_pair = std::move(*queue_pair_ex);

    // =========================================================================
    // Step 6: Out-of-Band Parameter Exchange (TCP)
    // Goal: Package local Queue Pair endpoint parameters (QPN, LID/GID, starting PSN,
    // buffer virtual address, rkey) and exchange them with the peer over TCP.
    // =========================================================================
    RemoteEndpoint local_info{
        .qpn = queue_pair.qp_num(),
        .lid = port.lid,
        .psn = static_cast<uint32_t>(rand() & 0xffffff),
        .gid = gid,
        .vaddr = reinterpret_cast<uint64_t>(buffer.get()),
        .rkey = memory_region.rkey()
    };

    auto remote_info_ex = exchange_qp_info(cfg, local_info);
    if (!check(remote_info_ex)) return 1;
    const auto remote_info = std::move(*remote_info_ex);

    // =========================================================================
    // Step 7: Queue Pair State Machine Progression
    // Goal: Progress the Queue Pair state machine from RESET -> INIT -> RTR -> RTS
    // using the exchanged remote peer parameters to arm the hardware data path.
    // =========================================================================
    if (cfg.transport == "rc") {
        auto transition_ex = qp_state::transition_rc_to_init(queue_pair.get(), cfg.ib_port)
            .and_then([&] {
                return qp_state::transition_rc_to_rtr(
                    queue_pair.get(), remote_info, cfg.ib_port, cfg.g_index, port.active_mtu
                );
            })
            .and_then([&] {
                return qp_state::transition_rc_to_rts(queue_pair.get(), local_info.psn);
            });

        if (!check(transition_ex)) return 1;
    }
    else if (cfg.transport == "ud") {
        auto transition_ex = qp_state::transition_ud_to_init(queue_pair.get(), cfg.ib_port, kDefaultQKey)
            .and_then([&] {
                return qp_state::transition_ud_to_rtr(queue_pair.get());
            })
            .and_then([&] {
                return qp_state::transition_ud_to_rts(queue_pair.get(), local_info.psn);
            });

        if (!check(transition_ex)) return 1;
    }
    else {
        std::cerr << role << " [Error] Invalid transport mechanism. Use either 'rc' or 'ud'\n";
        return 1;
    }

    struct ibv_ah* address_handle = nullptr;
    if (cfg.transport == "ud") {
        struct ibv_ah_attr address_handle_attr {};
        address_handle_attr.is_global = 1;
        address_handle_attr.port_num = cfg.ib_port;

        address_handle_attr.grh = {};
        address_handle_attr.grh.dgid = remote_info.gid;
        address_handle_attr.grh.sgid_index = static_cast<uint8_t>(cfg.g_index);
        address_handle_attr.grh.hop_limit = 64;

        address_handle = ibv_create_ah(protection_domain.get(), &address_handle_attr);
        if (!address_handle) {
            std::cerr << role << " [Error] Failed to create Address Handle for UD\n";
            return 1;
        }
    }

    // =========================================================================
    // Step 8: Ping-Pong Benchmark Data Path & Latency Sampling
    // Goal: Execute the synchronized ping-pong loop (pre-posting receives, posting
    // sends or RDMA writes, and spin-polling completions with nanosecond timing),
    // recording one-way latency samples into the histogram after a warmup period.
    // =========================================================================
    // TODO: Step 8

    // The memory buffer everything hinges on.
    struct ibv_sge receive_scatter_gather_element {
        .addr = reinterpret_cast<uint64_t>(buffer.get()),
        .length = static_cast<uint32_t>(cfg.message_size + (cfg.transport == "ud" ? kUdGrhOffset : 0)),
        .lkey = memory_region.lkey(),
    };

    struct ibv_sge send_scatter_gather_element {
        .addr = reinterpret_cast<uint64_t>(buffer.get() + (cfg.transport == "ud" ? kUdGrhOffset : 0)),
        .length = static_cast<uint32_t>(cfg.message_size),
        .lkey = memory_region.lkey(),
    };

    // Arriving packets sit here.
    struct ibv_recv_wr receive_work_request {
        .wr_id = 1,               // User ID
        .next = nullptr,          // Linked List Terminator
        .sg_list = &receive_scatter_gather_element,     //Pointer to above
        .num_sge = 1,             // Number of SGEs
    };


    struct ibv_send_wr send_work_request {};
    send_work_request.wr_id = 2;
    send_work_request.next = nullptr;
    send_work_request.sg_list = &send_scatter_gather_element;
    send_work_request.num_sge = 1;
    send_work_request.opcode = cfg.use_rdma_write ? IBV_WR_RDMA_WRITE : IBV_WR_SEND;
    send_work_request.send_flags = IBV_SEND_SIGNALED;       // Always make a completion request.

    if (cfg.use_inline) send_work_request.send_flags |= IBV_SEND_INLINE;    // Push data to doorbell.

    if (cfg.transport == "ud") {
        send_work_request.wr.ud.ah = address_handle;
        send_work_request.wr.ud.remote_qpn = remote_info.qpn;
        send_work_request.wr.ud.remote_qkey = kDefaultQKey;
    }

    if (cfg.use_rdma_write) {
        send_work_request.wr.rdma.remote_addr = remote_info.vaddr;
        send_work_request.wr.rdma.rkey = remote_info.rkey;
    }

    auto poll_completion = [&](std::string_view tag = "completion") -> bool {
        struct ibv_wc wc{};
        size_t spins = 0;
        constexpr size_t kMaxSpins = 50'000'000; // ~1-2 seconds of busy polling

        while (!completion_queue.poll_one(wc)) {
            ADDER_CPU_PAUSE();
            if (++spins > kMaxSpins) {
                std::cerr << role << " [Timeout] Stuck waiting for: " << tag << "\n";
                return false;
            }
        }
        if (wc.status != IBV_WC_SUCCESS) {
            std::cerr << role << " [Error] Completion failed (" << tag << "): "
                      << ibv_wc_status_str(wc.status) << " (" << wc.status << ")\n";
            return false;
        }
        return true;
    };

    adder::shared::LatencyHistogram hist(cfg.iterations);

    if (cfg.is_server) {
        const size_t total_iters = cfg.warmup_iterations + cfg.iterations;
        struct ibv_recv_wr* bad_recieve_work_request = nullptr;
        struct ibv_send_wr* bad_send_work_request = nullptr;

        for (size_t idx {}; idx < total_iters; idx++) {
            // 1. PrePost Recieve.
            if (queue_pair.post_recv(&receive_work_request, &bad_recieve_work_request) != 0){
                std::cerr << role << " [Error] post_recv failed at iteration " << idx << "\n";
                return 1;
            }

            // 2. Wait for Ping.
            if (!poll_completion("Ping Receive")) return 1;

            // 3. Pong back.
            if (queue_pair.post_send(&send_work_request, &bad_send_work_request) != 0) {
                std::cerr << role << " [Error] post_send failed at iteration " << idx << "\n"; //Use idx inside the err?
                return 1;
            }

            // 4. Wait for send to complete.
            if (!poll_completion("Pong Send Completion")) return 1;
        }
    } else {
        struct ibv_recv_wr* bad_recieve_work_request = nullptr;
        struct ibv_send_wr* bad_send_work_request = nullptr;

        // Things we do for UD.
        usleep(200'000);

        // WarmUp Loop
        for (size_t idx {}; idx < cfg.warmup_iterations; ++idx) {
            if (queue_pair.post_recv(&receive_work_request, &bad_recieve_work_request) != 0) {
                std::cerr << role << " [Warmup Error] post_recv failed at " << idx << "\n";
                return 1;
            }
            if (queue_pair.post_send(&send_work_request, &bad_send_work_request) != 0) {
                std::cerr << role << " [Warmup Error] post_send failed at " << idx << "\n";
                return 1;
            }
            if (!poll_completion("Send Completion")) return 1;
            if (!poll_completion("Pong Receive")) return 1;
        }

        // Hot One
        for (size_t idx {}; idx < cfg.iterations; ++idx) {

            // PrePost Receive
            if (queue_pair.post_recv(&receive_work_request, &bad_recieve_work_request) != 0){
                std::cerr << role << " [Error] post_recv failed at iteration " << idx << "\n";
                return 1;
            }

            const auto start_t = adder::shared::NanosecondClock::now_ns();

            if (queue_pair.post_send(&send_work_request, &bad_send_work_request) != 0) {
                std::cerr << role << " [Error] post_send failed at iteration " << idx << "\n";
                return 1;
            }

            if (!poll_completion()) return 1;
            if (!poll_completion()) return 1;

            const auto end_t = adder::shared::NanosecondClock::now_ns();

            hist.record((end_t - start_t)/2);
        }
    }

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

    if (address_handle) ibv_destroy_ah(address_handle);

    return 0;
}
