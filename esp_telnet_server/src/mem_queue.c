#include <string.h>
#include "mem_queue.h"
#include "esp_log.h"

typedef struct mem_queue_storage {
    char *holder;
    int head;
    int tail;
} mem_queue_storage_t;

mem_queue_storage_t *mem_queue = NULL;

/* void __mq_dump() {
    int t=mem_queue->head;
    while(t <= mem_queue->tail) {
        printf("%c", mem_queue->holder[t]);
        t++;
    }
    printf("\n");
}
*/
int mem_queue_get( char *buf, int8_t len) {
    int bytes = -1;
    if( mem_queue_init() != ESP_OK ) { return -1; }
    if(!mem_queue_isempty()) {
        if(mem_queue->tail >= (mem_queue->head + len - 1) ) {
            memcpy(buf, (mem_queue->holder+mem_queue->head), len);
            mem_queue->head += len;
            bytes = len;
        } else {
            bytes = (mem_queue->tail-mem_queue->head+1);
            memcpy(buf, (mem_queue->holder+mem_queue->head), bytes);
            mem_queue->head += (bytes);
        }
        if(mem_queue->tail < mem_queue->head) { 
            mem_queue->tail = -1;
            mem_queue->head = -1;
        }
        return bytes;
    }
    return 0;
}

void mem_queue_put( char *buf, int8_t len) {
    if( mem_queue_init() != ESP_OK ) return;
    if(mem_queue->tail == -1) { mem_queue->head = 0; }
    if((MEM_QUEUE_SIZE-1-len) < (mem_queue->tail )) {
        ESP_LOGE("memq", "overflow");
        return;
    }
    mem_queue->tail++;
    memcpy((mem_queue->holder+mem_queue->tail), buf, len);
    mem_queue->tail += len-1;
}

bool mem_queue_isempty() {
    if( mem_queue_init() != ESP_OK ) return true;
    return (mem_queue->tail == -1) ? true : false;
}

esp_err_t mem_queue_init() {
    if(mem_queue) return ESP_OK;
    mem_queue = (mem_queue_storage_t *)malloc(sizeof(mem_queue_storage_t));
    *mem_queue = (mem_queue_storage_t) {NULL, -1, -1};
    return (!(mem_queue->holder = (char *)malloc(MEM_QUEUE_SIZE))) ? ESP_FAIL : ESP_OK;
}