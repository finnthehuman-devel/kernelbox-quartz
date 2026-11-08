#ifndef KBOX_H
#define KBOX_H

#include <stdint.h>

typedef struct {
    uint32_t pid;
    char name[16];
    uint64_t stack_ptr;
    uint32_t state;  // 0=ready, 1=running, 2=blocked
} kbox_task;

void kbox_init(void);
void kbox_create_task(const char* name);
void kbox_list_tasks(void);
void kbox_main(void);

#endif

