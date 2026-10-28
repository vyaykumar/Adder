#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define MAX_GRADIENT_ELEMENTS 256
#define AGG_PORT 9999

// Custom UDP Packet Header for In-Network Gradient Aggregation
struct agg_hdr {
    __u32 chunk_id;
    __u32 seq_num;
    __u16 node_id;
    __u16 world_size;
    __u32 count; // Number of fixed-point int32 elements in payload
} __attribute__((packed));

// BPF Map storing aggregated chunk gradients
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __s32);
    __uint(max_entries, MAX_GRADIENT_ELEMENTS);
} grad_accum_map SEC(".maps");

// BPF Map tracking arrival counts per chunk
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, 16);
} chunk_counter_map SEC(".maps");

SEC("xdp")
int xdp_aggregate_prog(struct xdp_md *ctx) {
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;

    // Ethernet Header Bounds Check
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end) {
        return XDP_PASS;
    }
    if (eth->h_proto != bpf_htons(ETH_P_IP)) {
        return XDP_PASS;
    }

    // IP Header Bounds Check
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > data_end) {
        return XDP_PASS;
    }
    if (ip->protocol != IPPROTO_UDP) {
        return XDP_PASS;
    }

    // UDP Header Bounds Check
    struct udphdr *udp = (void *)((void *)ip + (ip->ihl * 4));
    if ((void *)(udp + 1) > data_end) {
        return XDP_PASS;
    }
    if (udp->dest != bpf_htons(AGG_PORT)) {
        return XDP_PASS;
    }

    // Aggregation Header Bounds Check
    struct agg_hdr *hdr = (void *)(udp + 1);
    if ((void *)(hdr + 1) > data_end) {
        return XDP_PASS;
    }

    // Gradient Payload Bounds Check
    __s32 *payload = (void *)(hdr + 1);
    if ((void *)(payload + MAX_GRADIENT_ELEMENTS) > data_end) {
        return XDP_PASS;
    }

    // TODO: Step 1: Parse chunk_id and look up arrival counter in chunk_counter_map.
    //
    // TODO: Step 2: Unroll bounded accumulation loop into grad_accum_map:
    // #pragma unroll
    // for (int i = 0; i < MAX_GRADIENT_ELEMENTS; ++i) {
    //     __u32 key = i;
    //     __s32 *accum = bpf_map_lookup_elem(&grad_accum_map, &key);
    //     if (accum) {
    //         *accum += payload[i];
    //     }
    // }
    //
    // TODO: Step 3: Increment arrival counter. If counter == hdr->world_size:
    // Chunk is complete! Copy fully reduced values back into payload and forward/redirect.

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
