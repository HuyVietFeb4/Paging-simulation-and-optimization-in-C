#include <stdio.h>
#include "ue_registry.h"

// Define the global variable here
ue_node_t* ue_head = NULL;

ue_node_t* create_new_ue(uint32_t ue_id) {
    ue_node_t* new_ue = malloc(sizeof(ue_node_t));
    if (new_ue == NULL) return NULL;
    new_ue->ue_id = ue_id;
    new_ue->last_seen = time(NULL);
    new_ue->ue_next = NULL;
    return new_ue;
}

int add_ue(ue_node_t* new_ue) {
    if (new_ue == NULL) return -1;
    new_ue->ue_next = ue_head;
    ue_head = new_ue;
    return 0;
}

int delete_ue_by_id(uint32_t ue_id) {
    ue_node_t* iter = ue_head;
    ue_node_t* prev = NULL;
    while (iter != NULL) {
        if (iter->ue_id == ue_id) {
            if (prev != NULL) {
                prev->ue_next = iter->ue_next;
            } else {
                ue_head = iter->ue_next;
            }
            free(iter);
            return 0;
        }
        prev = iter;
        iter = iter->ue_next;
    }
    return -1;
}

ue_node_t* search_ue_by_id(uint32_t ue_id) {
    ue_node_t* iter = ue_head;
    while (iter != NULL) { 
        if (iter->ue_id == ue_id) return iter;
        iter = iter->ue_next;
    }
    return NULL;
}

void purge_expired_ues(void) {
    time_t current_time = time(NULL);
    ue_node_t* iter = ue_head;
    
    while (iter != NULL) {
        ue_node_t* next_node = iter->ue_next; 
        
        if (current_time - iter->last_seen > TIMEOUT_THRESHOLD_SEC) {
            printf("[AMF DETACH] UE ID %u missed its heartbeat window. Purging from registry.\n", iter->ue_id);
            delete_ue_by_id(iter->ue_id);
        }
        
        iter = next_node;
    }
}

void init_rand_seed() {
    unsigned int seed = (unsigned int)time(NULL) ^ getpid();
    srand(seed);
}

uint32_t random_ue_id_generator() {
    uint32_t high_bits = (uint32_t)rand() & 0XFFFF;
    uint32_t low_bits = (uint32_t)rand() & 0XFFFF;

    uint32_t ue_id = (high_bits << 16) | low_bits;

    if(ue_id == 0) ue_id = 1;
    return ue_id;
}