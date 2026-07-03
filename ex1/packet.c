#include <stdio.h>
#include <packet.h>
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