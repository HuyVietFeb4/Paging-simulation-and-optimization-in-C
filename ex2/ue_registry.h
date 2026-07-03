#ifndef UE_REGISTRY_H
#define UE_REGISTRY_H

#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define TIMEOUT_THRESHOLD_SEC 15

// Linked List Node Structure
typedef struct ue_node {
    uint32_t ue_id; 
    time_t last_seen;
    struct ue_node* ue_next;
} ue_node_t;

// Public Function Declarations (Prototypes)
ue_node_t* create_new_ue(uint32_t ue_id);
int add_ue(ue_node_t* new_ue);
int delete_ue_by_id(uint32_t ue_id);
ue_node_t* search_ue_by_id(uint32_t ue_id);
void purge_expired_ues(void);

void init_rand_seed();
uint32_t random_ue_id_generator();

// Expose the head pointer if other files need to iterate through it directly
extern ue_node_t* ue_head;

#endif /* UE_REGISTRY_H */