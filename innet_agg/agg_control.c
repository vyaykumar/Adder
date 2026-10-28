#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <net/if.h>

#include <bpf/libbpf.h>
#include <bpf/bpf.h>

static volatile sig_atomic_t g_stop = 0;

static void sig_handler(int sig) {
    (void)sig;
    g_stop = 1;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ifname> [bpf_obj_path]\n", argv[0]);
        return 1;
    }

    const char* ifname = argv[1];
    const char* bpf_obj = (argc > 2) ? argv[2] : "xdp_agg.bpf.o";

    unsigned int ifindex = if_nametoindex(ifname);
    if (ifindex == 0) {
        fprintf(stderr, "Invalid interface name: %s\n", ifname);
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    printf("[libbpf] Loading eBPF program %s onto interface %s (idx=%u)...\n", bpf_obj, ifname, ifindex);

    // TODO: Step 1: Open and load BPF object using libbpf
    // struct bpf_object *obj = bpf_object__open_file(bpf_obj, nullptr);
    // bpf_object__load(obj);
    //
    // TODO: Step 2: Attach XDP program to interface
    // struct bpf_program *prog = bpf_object__find_program_by_name(obj, "xdp_aggregate_prog");
    // int prog_fd = bpf_program__fd(prog);
    // bpf_xdp_attach(ifindex, prog_fd, XDP_FLAGS_SKB_MODE, nullptr);
    printf("// TODO: agg_control - bpf_object__open_file, bpf_object__load, bpf_xdp_attach\n");

    printf("[libbpf] Aggregator active. Press Ctrl+C to detach and exit...\n");
    while (!g_stop) {
        sleep(1);
    }

    // TODO: Step 3: Detach XDP program on shutdown
    // bpf_xdp_detach(ifindex, XDP_FLAGS_SKB_MODE, nullptr);
    // bpf_object__close(obj);
    printf("[libbpf] Detached and cleaned up.\n");

    return 0;
}
