#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#include "packet.h"

#define SERVER_IP "127.0.0.1" 
#define PORT "3490"
#define MAX_MISSED_TICKS 200 // Break connection if silent for ~2 seconds (200 * 10ms)

uint16_t UE_sfn = 0;
int timeout_ms = 10;

typedef enum { UNSYNCED, SYNCED } SyncState;
SyncState state = UNSYNCED;
uint64_t ticks_since_last_sync = 0;

int get_ue_socket() { 
    int sock_fd = -1;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_DGRAM;  

    if ((rv = getaddrinfo(SERVER_IP, PORT, &hints, &servinfo)) != 0) {
        return -1;
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sock_fd);
            continue;
        }
        break;
    }

    freeaddrinfo(servinfo);
    return (p == NULL) ? -1 : sock_fd;
}

int run_ue_loop(int sock_fd, uint32_t my_ue_id) {
    struct pollfd fds[1];
    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;
    
    // Construct and transmit the binary structured packet to gNodeB
    struct register_packet reg_pkt;
    build_register_packet(&reg_pkt, 0x00, my_ue_id); 
    
    send(sock_fd, &reg_pkt, sizeof(reg_pkt), 0);
    printf("[UE ID: %u] Operational loop running... Awaiting MIB sync.\n", my_ue_id);

    while (1) {
        int rv = poll(fds, 1, timeout_ms);
        if (rv < 0) {
            perror("Poll error inside operation loop");
            return -1;
        }
        
        // SCENARIO A: A packet arrived or a network error happened
        if (rv > 0 && (fds[0].revents & POLLIN)) {
            struct MIB_packet incoming_mib;
            ssize_t bytes = recv(sock_fd, &incoming_mib, sizeof(incoming_mib), 0);
            
            if (bytes < 0) {
                // If the gNodeB died, the OS will return ECONNREFUSED here
                if (errno == ECONNREFUSED) {
                    printf("\n[UE ID: %u] ALERT: gNodeB connection dropped (Port Unreachable)!\n", my_ue_id);
                    return -1; // Fallback trigger
                }
                perror("recv error during operations");
                return -1;
            }
            
            if (bytes > 0 && incoming_mib.message_id == 0x01) {
                uint16_t rx_sfn = ntohs(incoming_mib.sfn_value);
                if (state == UNSYNCED) {
                    printf("[UE] FIRST SYNC! Latched gNodeB SFN: %d\n", rx_sfn);
                    UE_sfn = rx_sfn;
                    state = SYNCED;
                    ticks_since_last_sync = 0;
                } else if (state == SYNCED) {
                    if (ticks_since_last_sync >= 80) {
                        UE_sfn = rx_sfn;
                        ticks_since_last_sync = 0;
                        printf("[UE] === 800ms Resync Window Triggered and Adjusted ===\n");
                    }
                }
            }
        }
        
        // SCENARIO B: 10ms Epoch boundary crossed
        UE_sfn = (UE_sfn + 1) % 1024;
        ticks_since_last_sync++;

        // Watchdog: If gNodeB stops sending MIBs but doesn't close the port cleanly, 
        // we notice via a timeout threshold
        if (ticks_since_last_sync > MAX_MISSED_TICKS) {
            printf("\n[UE ID: %u] ALERT: gNodeB became silent for too long!\n", my_ue_id);
            return -1; // Fallback trigger
        }

        if (UE_sfn % 100 == 0) {
            printf("[UE] State: %s | Local SFN: %d\n", 
                   (state == SYNCED) ? "SYNCED" : "UNSYNCED", UE_sfn);
        }
    }
    return 0;
}

int wait_for_gnodeb(int sock_fd, uint32_t my_ue_id) {
    struct pollfd fds[1];
    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;

    struct register_packet reg_pkt;
    build_register_packet(&reg_pkt, 0x00, my_ue_id); // 0x00: UE -> gNodeB

    printf("[UE ID: %u] Attempting to reach gNodeB...\n", my_ue_id);

    while (1) {
        if (send(sock_fd, &reg_pkt, sizeof(reg_pkt), 0) < 0) {
            perror("Initial send failed, retrying");
            sleep(1);
            continue;
        }

        int rv = poll(fds, 1, 500); 

        if (rv < 0) {
            perror("Poll error during connection retry");
            return -1;
        }

        if (rv == 0) {
            printf("[UE ID: %u] gNodeB silent. Retrying in 1 second...\n", my_ue_id);
            sleep(1);
            continue;
        }

        if (fds[0].revents & POLLIN) {
            struct MIB_packet incoming_mib;
            ssize_t bytes = recv(sock_fd, &incoming_mib, sizeof(incoming_mib), MSG_PEEK | MSG_DONTWAIT);
            
            if (bytes < 0) {
                if (errno == ECONNREFUSED) {
                    printf("[UE ID: %u] gNodeB port unreachable. Retrying in 1 second...\n", my_ue_id);
                } else {
                    perror("recv error during handshake");
                }
                sleep(1);
                continue;
            }

            printf("[UE ID: %u] Connection confirmed! gNodeB is online.\n", my_ue_id);
            return 0; 
        }
    }
}

int main(void) {
    srand(time(NULL) ^ getpid()); 
    uint32_t mock_ue_id = (rand() % 900000) + 100000; 

    int sock_fd = -1;
    
    while ((sock_fd = get_ue_socket()) < 0) {
        fprintf(stderr, "Failed to create local socket context. Retrying in 2 seconds...\n");
        sleep(2);
    }

    // Infinite resilience state machine loop
    while (1) {
        // Step 1: Force wait/handshake with gNodeB until it's online
        if (wait_for_gnodeb(sock_fd, mock_ue_id) == 0) {
            // Step 2: Drop into operations
            int status = run_ue_loop(sock_fd, mock_ue_id);
            
            // Step 3: If operations broken, reset local tracking states before looping back to wait
            if (status == -1) {
                printf("[UE ID: %u] Falling back to connection recovery mode...\n\n", mock_ue_id);
                state = UNSYNCED;
                ticks_since_last_sync = 0;
                // Add a small breather sleep before firing handshake packets again
                sleep(1); 
            }
        }
    }

    close(sock_fd);
    return 0;
}