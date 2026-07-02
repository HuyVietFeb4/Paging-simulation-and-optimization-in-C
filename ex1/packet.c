#include <stdio.h>
#include <packet.h>
#include <arpa/inet.h>

void build_MIB_packet(struct MIB_packet* pkt, uint8_t message_id, uint16_t sfn_value) {
    if(pkt == NULL) return;
    pkt->message_id = message_id;
    pkt->sfn_value = sfn_value;
}