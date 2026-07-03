#include <stdio.h>
#include "packet.h"
#include <arpa/inet.h>

void build_MIB_packet(struct MIB_packet* pkt, uint8_t message_id, uint16_t sfn_value) {
    if(pkt == NULL) return;
    pkt->message_id = message_id;
    pkt->sfn_value = sfn_value;
}

void build_register_packet(struct register_packet* pkt, uint8_t Message_type, uint32_t UE_ID) {
    if(pkt == NULL) return;
    pkt->Message_type = Message_type;
    pkt->UE_ID = UE_ID;
}

void build_NgAP_packet(struct NgAP_paging_packet* pkt, uint32_t UE_ID, uint32_t TAC, uint32_t CN_Domain) {
    if(pkt == NULL) return;
    pkt->Message_type = 100;
    pkt->UE_ID = UE_ID;
    pkt->TAC = TAC;
    pkt->CN_Domain = CN_Domain;
}

void build_RRC_packet(struct RRC_paging_packet* pkt, uint32_t UE_ID, uint32_t TAC, uint32_t CN_Domain) {
    if(pkt == NULL) return;
    pkt->Message_type = 100;
    pkt->UE_ID = UE_ID;
    pkt->TAC = TAC;
    pkt->CN_Domain = CN_Domain;
}

/**
 * Parses a raw socket buffer, determines its packet type by evaluating its layout, 
 * and strictly verifies that the received size matches the expected structural footprint.
 * 
 * @param buf The raw character array buffer containing the received network data.
 * @param bytes_received The actual number of bytes returned by recv() or recvfrom().
 * @return ParsedPacketType enum value identifying the validated packet.
 */
ParsedPacketType parse_and_validate_packet(const char* buf, ssize_t bytes_received) {
    if (buf == NULL || bytes_received <= 0) {
        return PACKET_UNKNOWN;
    }

    // --- STAGE 1: Extract Potential 1-Byte Identifiers (Offset 0) ---
    uint8_t first_byte = (uint8_t)buf[0];

    // --- STAGE 2: Extract Potential 4-Byte Identifiers (Offsets 0-3) ---
    uint32_t first_four_bytes = 0;
    if (bytes_received >= (ssize_t)sizeof(uint32_t)) {
        // Safe memcpy preserves memory alignment across different CPU architectures
        memcpy(&first_four_bytes, buf, sizeof(uint32_t));
    }

    // --- STAGE 3: Evaluate and Enforce Size Constraints ---

    // Look for 1-Byte Header matches first
    if (first_byte == MSG_ID_MIB) {
        if (bytes_received != (ssize_t)sizeof(struct MIB_packet)) {
            fprintf(stderr, "[Parser Alert] Expected MIB size %lu, got %ld\n", 
                    sizeof(struct MIB_packet), bytes_received);
            return PACKET_MALFORMED;
        }
        return PACKET_MIB;
    }

    if (first_byte== MSG_ID_REG_REQ) {
        if (bytes_received != (ssize_t)sizeof(struct register_packet)) {
            fprintf(stderr, "[Parser Alert] Expected Register packet size %lu, got %ld\n", 
                    sizeof(struct register_packet), bytes_received);
            return PACKET_MALFORMED;
        }
        return PACKET_REGISTER;
    }

    // Look for 4-Byte Header matches next
    if (ntohl((first_four_bytes)) == MSG_TYPE_NGAP_RRC) {
        if (bytes_received != (ssize_t)sizeof(struct NgAP_paging_packet)) {
            fprintf(stderr, "[Parser Alert] Expected NgAP size %lu, got %ld\n", 
                    sizeof(struct NgAP_paging_packet), bytes_received);
            return PACKET_MALFORMED;
        }
        return PACKET_NGAP_RRC_PAGING;
    }

    // Catch-all fall-through for completely unknown packet footprints
    return PACKET_UNKNOWN;
}

