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
#include "ue_registry.h"

#define SERVER_IP "127.0.0.1" 
#define PORT "5000"
#define MAX_MISSED_TICKS 200 // Break connection if silent for ~2 seconds (200 * 10ms)


uint16_t UE_sfn = 0;
int timeout_ms = 10;

typedef enum { UNSYNCED, SYNCED } SyncState;
SyncState state = UNSYNCED;
uint64_t ticks_since_last_sync = 0;
uint32_t my_ue_id = 0;


int register_on_boot_up() {
    my_ue_id = random_ue_id_generator();
    return 1;
}

int get_gnodeb_socket() { 
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

void packet_handler(ParsedPacketType pkt_type, const void* payload) {
    if (payload == NULL) return;
    switch (pkt_type) {
        case PACKET_MIB: {
            const struct MIB_packet *mib = (const struct MIB_packet *)payload;
            uint16_t rx_sfn = ntohs(mib->sfn_value);
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
            break;
        }
        case PACKET_NGAP_RRC_PAGING: {
            const struct RRC_paging_packet *rrc = (const struct RRC_paging_packet *)payload;
            uint32_t ue_id = ntohl(rrc->UE_ID);
            uint32_t TAC = ntohl(rrc->TAC);
            // Apply network validation logic (e.g., check Tracking Area Code)
            if (TAC != 100) {
                printf("[UE] Dropping RRC Paging: TAC mismatch (Got %u, expected 100)\n", rrc->TAC);
            } else if (ue_id == my_ue_id) {
                printf("[UE ID: %u] !!! ALERT !!! Matched incoming RRC Paging event!\n", my_ue_id);
            }
            break;
        }
        case PACKET_MALFORMED:
            printf("[UE Engine] Discarded dangerous or truncated packet frame structure.\n");
            break;
        case PACKET_UNKNOWN:
        default:
            printf("[UE Engine] Discarded unhandled or unknown message signature format.\n");
            break;
    }
}

int run_ue_loop(int sock_fd) {
    struct pollfd fds[1];
    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;
    
    // struct register_packet reg_pkt;
    // build_register_packet(&reg_pkt, 0x0A, htonl(my_ue_id)); 
    // send(sock_fd, &reg_pkt, sizeof(reg_pkt), 0);
    
    struct timespec next_tick;
    clock_gettime(CLOCK_MONOTONIC, &next_tick);

    printf("[UE ID: %u] Operational loop running... Awaiting MIB/Paging.\n", my_ue_id);

    while (1) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        // Calculate how much time is left until the next 10ms boundary
        long long remaining_ms = (next_tick.tv_sec - now.tv_sec) * 1000 + 
                                 (next_tick.tv_nsec - now.tv_nsec) / 1000000;

        if (remaining_ms <= 0) {
            // SCENARIO B: 10ms Epoch boundary crossed safely
            UE_sfn = (UE_sfn + 1) % 1024; 
            ticks_since_last_sync++;      

            // Watchdog check
            if (ticks_since_last_sync > MAX_MISSED_TICKS) {
                printf("\n[UE ID: %u] ALERT: gNodeB became silent!\n", my_ue_id);
                return -1;
            }

            if (UE_sfn % 100 == 0) {
                printf("[UE] State: %s | Local SFN: %d\n", 
                       (state == SYNCED) ? "SYNCED" : "UNSYNCED", UE_sfn);
            }

            // Target the next 10ms mark
            next_tick.tv_nsec += 10000000; // +10ms
            if (next_tick.tv_nsec >= 1000000000) {
                next_tick.tv_sec += 1;
                next_tick.tv_nsec -= 1000000000;
            }
            continue;
        }

        // Wait only for the remaining time left in this 10ms slot
        int rv = poll(fds, 1, (int)remaining_ms);
        if (rv < 0) {
            perror("Poll error");
            return -1;
        }
        
        // SCENARIO A: A packet arrived (Can be MIB or RRC Paging!) [source: 1]
        if (rv > 0 && (fds[0].revents & POLLIN)) {
            char buffer[2048]; 
            ssize_t bytes = recv(sock_fd, buffer, sizeof(buffer), 0);
            
            if (bytes < 0) {
                if (errno == ECONNREFUSED) return -1;
                perror("recv error");
                return -1;
            }
            
            if (bytes > 0) {
                // Let your existing packet_handler parse it dynamically!
                // It already separates PACKET_MIB and PACKET_NGAP_RRC_PAGING.
                ParsedPacketType pkt_type = parse_and_validate_packet(buffer, bytes); 
                packet_handler(pkt_type, buffer); 
            }
        }
    }
    return 0;
}

int wait_for_gnodeb(int sock_fd) {
    struct pollfd fds[1];
    fds[0].fd = sock_fd;
    fds[0].events = POLLIN;

    printf("[UE] Attempting to reach gNodeB...\n");

    while (1) {
        struct register_packet reg_pkt;
        build_register_packet(&reg_pkt, 0x0A, htonl(my_ue_id)); 
        send(sock_fd, &reg_pkt, sizeof(reg_pkt), 0);

        int rv = poll(fds, 1, 500); 

        if (rv < 0) {
            perror("Poll error during connection retry");
            return -1;
        }

        if (rv == 0) {
            printf("[UE] gNodeB silent. Retrying in 1 second...\n");
            sleep(1);
            continue;
        }

        if (fds[0].revents & POLLIN) {
            struct MIB_packet incoming_mib;
            ssize_t bytes = recv(sock_fd, &incoming_mib, sizeof(incoming_mib), MSG_PEEK | MSG_DONTWAIT);
            
            if (bytes < 0) {
                if (errno == ECONNREFUSED) {
                    printf("[UE] gNodeB port unreachable. Retrying in 1 second...\n");
                } else {
                    perror("recv error during handshake");
                }
                sleep(1);
                continue;
            }

            printf("[UE] Connection confirmed! gNodeB is online.\n");
            return 0; 
        }
    }
}

// int register_protocol(int register_fd) {
//     if(register_to_amf(register_fd) != 0) {
//         printf("[UE] Can't send register packet (0x00)! Try again.\n");
//         return -1;
//     } 
//     if(recieve_assigned_ued(register_fd) != 0) {
//         printf("[UE] Can't receive assigned ue_id packet (0x01)! Try again.\n");
//         return -1;
//     } 
//     return 0;
// }



int main(void) {
    init_rand_seed();
    register_on_boot_up();
    int sock_fd = -1;
    while ((sock_fd = get_gnodeb_socket()) < 0) {
        fprintf(stderr, "Failed to create local socket context. Retrying in 2 seconds...\n");
        sleep(2);
    }

    while (1) {
        // Step 1: Force wait/handshake with gNodeB until it's online
        if (wait_for_gnodeb(sock_fd) == 0) {
            // Step 2: Drop into operations
            int status = run_ue_loop(sock_fd);
            
            // Step 3: If operations broken, reset local tracking states before looping back to wait
            if (status == -1) {
                printf("[UE ID: %u] Falling back to connection recovery mode...\n\n", my_ue_id);
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