#ifndef _MEM_QUEUE_H_
#define _MEM_QUEUE_H_

#include "esp_system.h"

#define MEM_QUEUE_SIZE  128

int mem_queue_get( char *buf, int8_t len);
void mem_queue_put( char *buf, int8_t len);

bool mem_queue_isempty();

esp_err_t mem_queue_init();

#endif /* _MEM_QUEUE_H_ */