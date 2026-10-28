#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdckdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_ELEMENTS 256
#define AGG_PORT 9999

struct agg_hdr {
    uint32_t chunk_id;
    uint32_t seq_num;
    uint16_t node_id;
    uint16_t world_size;
    uint32_t count;
} __attribute__((packed));

struct ChunkAccumulator {
    int32_t values[MAX_ELEMENTS];
    uint32_t arrivals;
    uint64_t overflow_count;
};

int main(int argc, char* argv[]) {
    int port = AGG_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in servaddr = {};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons((uint16_t)port);

    if (bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("[Userspace Aggregator C23] Listening on UDP port %d...\n", port);

    uint8_t buffer[2048];
    struct ChunkAccumulator accum = {};

    while (true) {
        struct sockaddr_in clientaddr = {};
        socklen_t len = sizeof(clientaddr);
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (struct sockaddr*)&clientaddr, &len);
        if (n < (ssize_t)sizeof(struct agg_hdr)) continue;

        struct agg_hdr* hdr = (struct agg_hdr*)buffer;
        int32_t* payload = (int32_t*)(buffer + sizeof(struct agg_hdr));

        // TODO: Step 1: Accumulate payload values with overflow tracking using C23 ckd_add
        // for (uint32_t i = 0; i < hdr->count && i < MAX_ELEMENTS; ++i) {
        //     int32_t result = 0;
        //     if (ckd_add(&result, accum.values[i], payload[i])) {
        //         accum.overflow_count++;
        //     }
        //     accum.values[i] = result;
        // }
        //
        // TODO: Step 2: Check if chunk arrival count == hdr->world_size:
        // When complete, send reduced chunk back to nodes or broadcast.
        (void)hdr;
        (void)payload;
        (void)accum;
    }

    close(sockfd);
    return 0;
}
