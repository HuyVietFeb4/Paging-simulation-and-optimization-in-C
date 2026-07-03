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
#include "packet.h"

#define PORT "3490"          
#define MAX_UES 32

uint16_t gNodeB_sfn = 0;
uint64_t ticks = 0;

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

int main(void) {
    int server_fd = get_listener_socket();
    int timer_fd = get_timer_fd();

    if (server_fd < 0 || timer_fd < 0) {
        if (server_fd >= 0) close(server_fd);
        if (timer_fd >= 0) close(timer_fd);
        return 1;
    }

    printf("gNodeB Server Running on port %s...\n", PORT);

    struct pollfd fds[2];
    fds[0].fd = server_fd;
    fds[0].events = POLLIN;
    fds[1].fd = timer_fd;
    fds[1].events = POLLIN;

    while (1) {
        int poll_rv = poll(fds, 2, -1);
        if (poll_rv < 0) break;

        // EVENT 1: Incoming Structured Packet on Server Socket
        if (fds[0].revents & POLLIN) {
            struct sockaddr_in ue_addr;
            socklen_t ue_addr_len = sizeof(ue_addr);
            struct register_packet rx_pkt;
            
            ssize_t rx_bytes = recvfrom(server_fd, &rx_pkt, sizeof(rx_pkt), 0, 
                                        (struct sockaddr *)&ue_addr, &ue_addr_len);
            if (rx_bytes >= (ssize_t)sizeof(struct register_packet)) {
                // Verify this is a UE -> gNodeB registration request (0x00)
                if (rx_pkt.Message_type == 0x00) {
                    uint32_t incoming_ue_id = ntohl(rx_pkt.UE_ID);
                    register_ue(&ue_addr, incoming_ue_id);
                }
            }
        }

        // EVENT 2: 10ms Timer Heartbeat
        if (fds[1].revents & POLLIN) {
            uint64_t expirations;
            if (read(timer_fd, &expirations, sizeof(expirations)) > 0) {
                gNodeB_sfn = (gNodeB_sfn + 1) % 1024;
                ticks++;

                if (ticks % 8 == 0) {
                    struct MIB_packet mib;
                    mib.message_id = 0x01;
                    mib.sfn_value = htons(gNodeB_sfn); 

                    for (int i = 0; i < ue_count; i++) {
                        sendto(server_fd, &mib, sizeof(mib), 0, 
                               (struct sockaddr *)&registered_ues[i], sizeof(registered_ues[i]));
                    }
                }
            }
        }
    }

    close(server_fd);
    close(timer_fd);
    return 0;
}