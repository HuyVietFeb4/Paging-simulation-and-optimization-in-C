#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <poll.h> // Added for non-blocking timing/reception management

#include "packet.h"
#include "ue_registry.h"

#define SERVER_IP "127.0.0.1"
#define PORT "6000"          // gNodeB TCP Listening Port

int get_gnodeb_tcp_socket() {
    int sock_fd = -1;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM; 

    if ((rv = getaddrinfo(SERVER_IP, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "[AMF] getaddrinfo error: %s\n", gai_strerror(rv));
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

int main(void) {
    init_rand_seed();
    
    printf("[AMF] Booting Core Infrastructure Layer 3...\n");

    int socket_fd = -1;
    
    while (1) {
        printf("[AMF] Attempting connection link to gNodeB at %s:%s...\n", SERVER_IP, PORT);
        socket_fd = get_gnodeb_tcp_socket();
        
        if (socket_fd >= 0) {
            printf("[AMF] Connection Context Established! Connected to gNodeB Server Stack.\n");
            break;
        }
        
        fprintf(stderr, "[AMF] gNodeB context offline or unreachable. Retrying in 3 seconds...\n");
        sleep(3);
    }

    printf("\n[AMF] Operational loop running... Simulating NgAP Core triggers.\n");
    printf("      Press [Ctrl+C] to exit simulation interface.\n\n");

    // Setup poll structures to watch our TCP socket link without blocking indefinitely
    struct pollfd fds[1];
    fds[0].fd = socket_fd;
    fds[0].events = POLLIN;

    time_t last_paging_time = time(NULL);
    int current_paging_delay = 3 + (rand() % 4); // Initial randomized delay (3-6 seconds)

    while (1) {
        time_t now = time(NULL);
        
        // Calculate remaining time until the next scheduled paging window
        int time_elapsed = (int)(now - last_paging_time);
        int remaining_time_ms = (current_paging_delay - time_elapsed) * 1000;
        if (remaining_time_ms < 0) remaining_time_ms = 0;

        // --- HOUSEKEEPING & EVENT RECEPTION ---
        // Wait for incoming registrations from gNodeB, or wake up when the paging timer hits 0
        int poll_rv = poll(fds, 1, remaining_time_ms);

        if (poll_rv < 0) {
            perror("[AMF Error] Poll system error");
            break;
        }

        // Handle Incoming Registration forwardings from gNodeB
        if (poll_rv > 0 && (fds[0].revents & POLLIN)) {
            char rx_buf[1024];
            ssize_t rx_bytes = recv(socket_fd, rx_buf, sizeof(rx_buf), 0);

            if (rx_bytes <= 0) {
                if (rx_bytes == 0) {
                    printf("\n[AMF] ALERT: Link dropped by gNodeB edge node (EOF).\n");
                } else {
                    perror("[AMF Core Error] Link error on TCP read stream");
                }
                break; // Break loop for connection safety recovery
            }

            ParsedPacketType type = parse_and_validate_packet(rx_buf, rx_bytes);
            if (type == PACKET_REGISTER) {
                struct register_packet* reg = (struct register_packet*)rx_buf;
                uint32_t forwarded_ue_id = ntohl(reg->UE_ID);

                ue_node_t* existing_ue = search_ue_by_id(forwarded_ue_id);
                if (existing_ue == NULL) {
                    ue_node_t* new_ue = create_new_ue(forwarded_ue_id);
                    if (new_ue != NULL) {
                        add_ue(new_ue);
                        printf("[AMF CORE] New Network UE Registered dynamically: ID %u\n", forwarded_ue_id);
                    }
                } else {
                    // Update its keepalive timestamp
                    existing_ue->last_seen = time(NULL);
                    printf("[AMF CORE] Heartbeat/Keepalive renewed for UE ID: %u\n", forwarded_ue_id);
                }
            }
        }

        // --- SCHEDULING DISPATCH LOOP (PAGING SYSTEM) ---
        now = time(NULL);
        if (now - last_paging_time >= current_paging_delay) {
            // Periodically clean up dead tracking contexts if they missed deadlines
            // purge_expired_ues();

            // Pick an active registered target UE from the database linked list to page
            if (ue_head == NULL) {
                printf("[AMF Warning] Local registry drained or empty. Awaiting dynamic network registrations...\n");
            } else {
                // Count active elements inside registry list to safely index a random element
                int total_nodes = 0;
                ue_node_t* count_iter = ue_head;
                while (count_iter != NULL) {
                    total_nodes++;
                    count_iter = count_iter->ue_next;
                }

                ue_node_t* iter = ue_head;
                int target_index = rand() % total_nodes; 
                for (int i = 0; i < target_index && iter->ue_next != NULL; i++) {
                    iter = iter->ue_next;
                }

                uint32_t chosen_ue_id = iter->ue_id;
                
                // Exercise 2 Rules: Alternating CN Domains (100 = Voice Call, 101 = Data App Notification)
                uint32_t chosen_cn_domain = (rand() % 2 == 0) ? 100 : 101; 
                uint32_t fixed_tac = 100; // Expected matching routing zone code

                // Build structural payload via packet.h function signature
                struct NgAP_paging_packet outbound_pkt;
                build_NgAP_packet(&outbound_pkt, chosen_ue_id, fixed_tac, chosen_cn_domain);

                // Explicit Big-Endian Network Translation to ensure portable communication bytes
                outbound_pkt.Message_type = htonl(outbound_pkt.Message_type); 
                outbound_pkt.UE_ID        = htonl(outbound_pkt.UE_ID);
                outbound_pkt.TAC          = htonl(outbound_pkt.TAC);          
                outbound_pkt.CN_Domain    = htonl(outbound_pkt.CN_Domain);    

                printf("[AMF -> NgAP] Dispatched Paging Event Request to gNodeB:\n");
                printf("              | Target UE ID:  %u\n", chosen_ue_id);
                printf("              | Area Code TAC: %u\n", fixed_tac);
                printf("              | Core Domain:   %s (%u)\n", 
                       (chosen_cn_domain == 100) ? "Voice Session" : "Data Session VIA App", chosen_cn_domain);

                // Dispatch over structured TCP stream
                ssize_t bytes_sent = send(socket_fd, &outbound_pkt, sizeof(outbound_pkt), 0);
                if (bytes_sent == -1) {
                    perror("[AMF Core Error] Transmission across TCP stream failed");
                    if (errno == EPIPE || errno == ECONNRESET) {
                        break;
                    }
                }
                printf("              | Transmission Status: %ld bytes flushed safely.\n\n", bytes_sent);
            }

            // Reset paging timing constraints for next iteration loop window
            last_paging_time = now;
            current_paging_delay = 3 + (rand() % 4); 
        }
    }

    close(socket_fd);
    printf("[AMF Core] Context terminated cleanly.\n");
    return 0;
}