#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include <string.h>

#define MSG_ID_MIB           0x01
#define MSG_ID_REG_REQ       0x0A

#define MSG_TYPE_NGAP_RRC       100

typedef enum {
    PACKET_UNKNOWN = 0,
    PACKET_MIB,
    PACKET_REGISTER,
    PACKET_NGAP_RRC_PAGING,
    PACKET_MALFORMED       // Packet identifier matched, but data size was truncated/wrong
} ParsedPacketType;

struct __attribute__((packed)) MIB_packet {
    uint8_t message_id;
    uint16_t sfn_value;
};

struct __attribute__((packed)) register_packet {
    // 0x0A: UE -> gNodeB (Please register me)
    uint8_t Message_type;
    uint32_t UE_ID;
};


struct __attribute__((packed)) NgAP_paging_packet {
    uint32_t Message_type;
    uint32_t UE_ID;
    uint32_t TAC; // If not 100, drop
    uint32_t CN_Domain;
};

struct __attribute__((packed)) RRC_paging_packet {
    uint32_t Message_type;
    uint32_t UE_ID;
    uint32_t TAC; // If not 100, drop
    uint32_t CN_Domain;
};



void build_MIB_packet(struct MIB_packet* pkt, uint8_t message_id, uint16_t sfn_value);

void build_register_packet(struct register_packet* pkt, uint8_t Message_type, uint32_t UE_ID);

void build_NgAP_packet(struct NgAP_paging_packet* pkt, uint32_t UE_ID, uint32_t TAC, uint32_t CN_Domain);

void build_RRC_packet(struct RRC_paging_packet* pkt, uint32_t UE_ID, uint32_t TAC, uint32_t CN_Domain);

ParsedPacketType parse_and_validate_packet(const char* buf, ssize_t bytes_received);

#endif