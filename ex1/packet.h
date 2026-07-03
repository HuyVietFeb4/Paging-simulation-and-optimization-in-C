#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

struct __attribute__((packed)) MIB_packet {
    uint8_t message_id;
    uint16_t sfn_value;
};

struct __attribute__((packed)) register_packet {
    // 0x00: UE -> gNodeB (Please register me)
    // 0x01: UE -> AMF (Please register me)
    // 0x02: AMF -> UE (Registration successful, here is your ID)
    // 0x03: AMF -> UE (Registration failed)
    // 0x04: UE -> AMF (Hearbeat)
    uint8_t Message_type;
    uint32_t UE_ID;
};

void build_MIB_packet(struct MIB_packet* pkt, uint8_t message_id, uint16_t sfn_value);

void build_register_packet(struct register_packet* pkt, uint8_t Message_type, uint32_t UE_ID);

#endif