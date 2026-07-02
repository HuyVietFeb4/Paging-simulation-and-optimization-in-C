#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include "packet.h"

uint16_t UE_sfn = 0;
int timeout_ms = 10;


#define PORT "3490"
typedef enum { UNSYNCED, SYNCED } SyncState;
SyncState state = UNSYNCED;
uint64_t ticks_since_last_sync = 0;

int get_listener_socket() {
    int listener;
    int rv;

    struct addrinfo hints, *ai, *p;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "UE (server): %s\n", gai_strerror(rv));
        exit(1);
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if(listener < 0) {
            continue;
        }

        // setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    if(p == NULL) {
        return -1;
    }

    freeaddrinfo(ai);
    return listener;
}

void run_ue_loop(int listener_fd) {
    struct pollfd fds[1];
    fds[0].fd = listener_fd;
    fds[0].events = POLLIN;
    printf("UE Loop running ...\n");
    while(1) {
        int rv = poll(fds, 1, timeout_ms);
        if(rv < 0) {
            perror("poll error");
            break;
        }
        // SCENARIO A: A UDP packet arrived before the 10ms timer expired
        if(rv > 0 && (fds[0].revents & POLLIN)) {
            struct MIB_packet incoming_mib;
            struct sockaddr_in remote_addr;
            socklen_t addr_len = sizeof(remote_addr);
            ssize_t bytes = recvfrom(listener_fd, &incoming_mib, sizeof(incoming_mib), 0,
                                    (struct sockaddr *)&remote_addr, &addr_len);
            if(bytes > 0 && incoming_mib.message_id == 0x01) {
                // printf("[UE] recieved packet from gNodeB.\n");
                uint16_t rx_sfn = ntohs(incoming_mib.sfn_value);
                if(state == UNSYNCED) {
                    printf("[UE] FIRST SYNC! Latched gNodeB SFN: %d (Old UE SFN: %d)\n", rx_sfn, UE_sfn);
                    UE_sfn = rx_sfn;
                    state = SYNCED;
                    ticks_since_last_sync = 0;
                } else if (state == SYNCED) {
                    printf("[UE] Received MIB every 80ms -> gNodeB SFN: %d | Current UE SFN: %d\n", rx_sfn, UE_sfn);
                    if (ticks_since_last_sync >= 80) {
                        UE_sfn = rx_sfn;
                        ticks_since_last_sync = 0;
                        printf("[UE] === 800ms Resync Window Triggered and Adjusted ===\n");
                    }
                }
            }
        }
        // SCENARIO B: Ret == 0 (Timeout hit) OR we finished processing the early packet. Either way, a 10ms epoch boundary has been crossed!
        UE_sfn = (UE_sfn + 1) % 1024;
        ticks_since_last_sync++;
        if (UE_sfn % 100 == 0) {
            printf("[UE] State: %s | Local SFN: %d\n", 
                   (state == SYNCED) ? "SYNCED" : "UNSYNCED", UE_sfn);
        }
    }
}

int main(void) {
    int listener_fd = get_listener_socket();
    if (listener_fd < 0) {
        fprintf(stderr, "Failed to initialize listener socket\n");
        return 1;
    }
    run_ue_loop(listener_fd);
    return 0;
}