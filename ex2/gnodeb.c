#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/timerfd.h>
#include <poll.h>
#include <errno.h>
#include "packet.h"

#define PORT "5000"          
#define AMF_PORT "6000"
#define MAX_UES 32
#define MAX_PENDING_PAGINGS 64

uint16_t gNodeB_sfn = 0;
uint64_t ticks = 0;

struct PendingPaging {
    struct RRC_paging_packet rrc_pkt;
    uint16_t target_sfn;
    int is_active;
} paging_queue[MAX_PENDING_PAGINGS];

struct sockaddr_in registered_ues[MAX_UES];
int ue_count = 0;

void register_ue(struct sockaddr_in *client_addr, uint32_t ue_id) {
    for (int i = 0; i < ue_count; i++) {
        if (registered_ues[i].sin_addr.s_addr == client_addr->sin_addr.s_addr &&
            registered_ues[i].sin_port == client_addr->sin_port) {
            return; // Address already registered
        }
    }
    if (ue_count < MAX_UES) {
        registered_ues[ue_count] = *client_addr;
        ue_count++;
        printf("[gNodeB] Registered new UE ID: %u from %s:%d (Total: %d)\n", 
               ue_id, inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), ue_count);
    } else {
        fprintf(stderr, "[gNodeB] Warning: Max UEs reached.\n");
    }
}

int get_listener_socket() {
    int listener;
    int rv;
    struct addrinfo hints, *ai, *p;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "gNodeB getaddrinfo error: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) continue;

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    freeaddrinfo(ai);
    return (p == NULL) ? -1 : listener;
}

int get_tcp_listener_socket() {
    int listener;
    int rv;
    struct addrinfo hints, *ai, *p;
    int yes = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, AMF_PORT, &hints, &ai)) != 0) {
        fprintf(stderr, "gNodeB TCP getaddrinfo error: %s\n", gai_strerror(rv));
        return -1;
    }

    for (p = ai; p != NULL; p = p->ai_next) {
        listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (listener < 0) continue;

        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

        if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
            close(listener);
            continue;
        }
        break;
    }

    freeaddrinfo(ai);
    if (p == NULL) return -1;

    if (listen(listener, 10) == -1) {
        perror("TCP Listen failed");
        close(listener);
        return -1;
    }

    return listener;
}

int get_timer_fd() {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd < 0) return -1;
    struct itimerspec timer_spec;
    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_interval.tv_nsec = 10000000; // 10ms
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = 10000000;
    
    if (timerfd_settime(timer_fd, 0, &timer_spec, NULL) < 0) {
        close(timer_fd);
        return -1;
    }
    return timer_fd;
}

uint16_t calculate_target_sfn(uint32_t ue_id) {
    uint16_t current = gNodeB_sfn;
    for (int i = 0; i < 1024; i++) {
        uint16_t candidate_sfn = (current + i) % 1024;
        if ((candidate_sfn + 0) % 64 == (64 / 1) * (ue_id % 1)) {
            return candidate_sfn;
        }
    }
    return (current + 64) % 1024; 
}

void schedule_rrc_paging(struct NgAP_paging_packet *ngap) {
    uint32_t raw_ue_id = ntohl(ngap->UE_ID);
    uint16_t target = calculate_target_sfn(raw_ue_id);
    
    for (int i = 0; i < MAX_PENDING_PAGINGS; i++) {
        if (!paging_queue[i].is_active) {
            paging_queue[i].rrc_pkt.Message_type = ngap->Message_type;
            paging_queue[i].rrc_pkt.UE_ID = ngap->UE_ID;
            paging_queue[i].rrc_pkt.TAC = ngap->TAC;
            paging_queue[i].rrc_pkt.CN_Domain = ngap->CN_Domain;
            paging_queue[i].target_sfn = target;
            paging_queue[i].is_active = 1;
            
            printf("[gNodeB] Scheduled RRC Paging for UE ID: %u at target SFN: %d (Current SFN: %d)\n", 
                   raw_ue_id, target, gNodeB_sfn);
            return;
        }
    }
    fprintf(stderr, "[gNodeB] Error: Paging tracking queue buffer full!\n");
}

int main(void) {
    int server_fd = get_listener_socket();        // UDP Engine 
    int timer_fd = get_timer_fd();                // 10ms Tick Engine
    int tcp_listen_fd = get_tcp_listener_socket();  // TCP Server Context
    int amf_fd = -1;                              // Connection reference holder for AMF

    if (server_fd < 0 || timer_fd < 0 || tcp_listen_fd < 0) {
        fprintf(stderr, "Fatal initialization failure.\n");
        return 1;
    }

    printf("gNodeB Server Stack Initialized.\n");
    printf(" -> Listening for UEs (UDP Port: %s)\n", PORT);
    printf(" -> Listening for AMF (TCP Port: %s)\n", AMF_PORT);

    struct pollfd fds[4];
    fds[0].fd = server_fd;     fds[0].events = POLLIN;
    fds[1].fd = timer_fd;      fds[1].events = POLLIN;
    fds[2].fd = tcp_listen_fd; fds[2].events = POLLIN;
    fds[3].fd = -1;            fds[3].events = POLLIN; 

    while (1) {
        int poll_rv = poll(fds, 4, -1);
        if (poll_rv < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // EVENT 1: Packet received on UDP Server Socket (UE Actions)
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in ue_addr;
            socklen_t ue_addr_len = sizeof(ue_addr);
            struct register_packet rx_pkt;
            
            ssize_t rx_bytes = recvfrom(server_fd, &rx_pkt, sizeof(rx_pkt), 0, 
                                        (struct sockaddr *)&ue_addr, &ue_addr_len);
            if (rx_bytes >= (ssize_t)sizeof(struct register_packet)) {
                if (rx_pkt.Message_type == 0x0A) { // Core Handshake Signatures
                    uint32_t incoming_ue_id = ntohl(rx_pkt.UE_ID);
                    register_ue(&ue_addr, incoming_ue_id);
                    if (amf_fd != -1) {
                        printf("[gNodeB -> AMF] Forwarding registration for UE ID: %u\n", incoming_ue_id);
                        // Send the exact same raw registration packet over TCP
                        send(amf_fd, &rx_pkt, sizeof(rx_pkt), 0);
                    }
                }
            }
        }

        // EVENT 2: Incoming Handshake on TCP Server Port (AMF client joining)
        if (fds[2].revents & POLLIN) {
            struct sockaddr_in amf_addr;
            socklen_t amf_len = sizeof(amf_addr);
            int incoming_client = accept(tcp_listen_fd, (struct sockaddr *)&amf_addr, &amf_len);
            if (incoming_client >= 0) {
                if (amf_fd != -1) {
                    close(amf_fd); // Cleanup stale links
                }
                amf_fd = incoming_client;
                fds[3].fd = amf_fd; // Active scanning inside poll index 3
                printf("[gNodeB] AMF Client Link Established on Port %s.\n", AMF_PORT);
            }
        }

        // EVENT 3: Message read availability over the AMF stream link (NgAP Paging)
        if (amf_fd != -1 && (fds[3].revents & POLLIN)) {
            struct NgAP_paging_packet ngap_pkt;
            ssize_t tcp_bytes = recv(amf_fd, &ngap_pkt, sizeof(ngap_pkt), 0);
            
            if (tcp_bytes <= 0) {
                printf("[gNodeB] AMF Core disconnected from server frame.\n");
                close(amf_fd);
                amf_fd = -1;
                fds[3].fd = -1; // Reset poll context
            } else if (tcp_bytes >= (ssize_t)sizeof(struct NgAP_paging_packet)) {
                if (ntohl(ngap_pkt.Message_type) == 100) { // Valid Paging request verified
                    printf("[gNodeB] Received NgAP Paging from AMF (UE ID: %u, TAC: %u, Domain: %u)\n", 
                           ntohl(ngap_pkt.UE_ID), ntohl(ngap_pkt.TAC), ntohl(ngap_pkt.CN_Domain));
                    schedule_rrc_paging(&ngap_pkt);
                }
            }
        }

        // EVENT 4: 10ms System Timer Heartbeat
        if (fds[1].revents & POLLIN) {
            uint64_t expirations;
            if (read(timer_fd, &expirations, sizeof(expirations)) > 0) {
                gNodeB_sfn = (gNodeB_sfn + 1) % 1024; // Standard modulo increment
                ticks++;

                // Trigger MIB Broadcast down every 80ms (8 ticks)
                if (ticks % 8 == 0) {
                    struct MIB_packet mib;
                    mib.message_id = 0x01; // Identity token
                    mib.sfn_value = htons(gNodeB_sfn); 

                    for (int i = 0; i < ue_count; i++) {
                        sendto(server_fd, &mib, sizeof(mib), 0, 
                               (struct sockaddr *)&registered_ues[i], sizeof(registered_ues[i]));
                    }
                }

                // Scan, check and dump matching active Pagings matching this specific SFN tick
                for (int i = 0; i < MAX_PENDING_PAGINGS; i++) {
                    if (paging_queue[i].is_active && paging_queue[i].target_sfn == gNodeB_sfn) {
                        
                        // Emit RRC Paging across to registered instances
                        for (int u = 0; u < ue_count; u++) {
                            sendto(server_fd, &paging_queue[i].rrc_pkt, sizeof(struct RRC_paging_packet), 0,
                                   (struct sockaddr *)&registered_ues[u], sizeof(registered_ues[u]));
                        }
                        printf("[gNodeB] Sent scheduled RRC Paging downstream to UEs at matching SFN: %d\n", gNodeB_sfn);
                        paging_queue[i].is_active = 0; // Release tracking entry
                    }
                }
            }
        }
    }

    // Stack closure
    close(server_fd);
    close(timer_fd);
    close(tcp_listen_fd);
    if (amf_fd != -1) close(amf_fd);
    return 0;
}