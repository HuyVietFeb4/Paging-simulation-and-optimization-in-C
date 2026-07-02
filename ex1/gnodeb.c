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
#include "packet.h"

#define SERVER_IP "127.0.0.1" 
#define PORT "3490"          

uint16_t gNodeB_sfn = 0;
uint64_t ticks = 0;

int get_ue_socket() { 
    int sock_fd = -1;
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;       
    hints.ai_socktype = SOCK_DGRAM;  

    if ((rv = getaddrinfo(SERVER_IP, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "gNodeB getaddrinfo error: %s\n", gai_strerror(rv));
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

    if (p == NULL) {
        fprintf(stderr, "gNodeB: failed to create or connect socket\n");
        return -1;
    }
    return sock_fd;
}

void clear_resource(int sock_fd, int timer_fd) {
    if(sock_fd >= 0) close(sock_fd);
    if(timer_fd >= 0) close(timer_fd);
}

int get_timer_fd() {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (timer_fd < 0) {
        perror("timerfd_create failed");
        return -1;
    }
    struct itimerspec timer_spec;
    timer_spec.it_interval.tv_sec = 0;
    timer_spec.it_interval.tv_nsec = 10000000; // 10ms
    timer_spec.it_value.tv_sec = 0;
    timer_spec.it_value.tv_nsec = 10000000;
    
    if (timerfd_settime(timer_fd, 0, &timer_spec, NULL) < 0) {
        perror("timerfd_settime failed");
        close(timer_fd);
        return -1;
    }
    return timer_fd;
}

int main(void) {
    int sock_fd = get_ue_socket();
    int timer_fd = get_timer_fd();

    if (sock_fd < 0 || timer_fd < 0) {
        fprintf(stderr, "Initialization failed. Exiting.\n");
        clear_resource(sock_fd, timer_fd);
        return 1;
    }

    printf("gNodeB Broadcaster Started. Sync master clock running...\n");
    uint64_t expirations;
    
    while (read(timer_fd, &expirations, sizeof(expirations)) > 0) {
        gNodeB_sfn = (gNodeB_sfn + 1) % 1024;
        ticks++;

        if (ticks % 8 == 0) {
            struct MIB_packet mib;
            mib.message_id = 0x01;
            mib.sfn_value = htons(gNodeB_sfn); 

            ssize_t bytes_sent = send(sock_fd, &mib, sizeof(mib), 0);
            
            if (bytes_sent < 0) {
                perror("gNodeB send failed");
            } else {
                printf("[gNodeB] Sent MIB. Master SFN: %d\n", gNodeB_sfn);
            }
        }
    }

    clear_resource(sock_fd, timer_fd);
    return 0;
}