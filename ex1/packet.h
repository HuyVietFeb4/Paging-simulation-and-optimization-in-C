#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

struct __attribute__((packed)) MIB_packet {
    uint8_t message_id;
    uint16_t sfn_value;
};

void build_MIB_packet(struct MIB_packet* pkt, uint8_t message_id, uint16_t sfn_value);

#endif