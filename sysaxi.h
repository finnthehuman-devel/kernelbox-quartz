#ifndef SYSAXI_H
#define SYSAXI_H

#include <stdint.h>

// Bus modes
#define SYSAXI_SVCIN  0   // Kernel -> Hypervisor
#define SYSAXI_SVCOUT 1   // Hypervisor -> Kernel

// Opcodes
#define SYS_NULL    0
#define SYS_READ    1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_MOVE    4
#define SYS_COPY    5
#define SYS_WRITE   64
#define SYS_PANIC   255

// Write request: {ptr, len}. The hypervisor writes exactly len bytes.
// This replaces the old NUL-terminated-string convention, which caused
// strlen() to read past KBox buffers.
typedef struct {
    const void* ptr;
    uint64_t    len;
} sysaxi_write_req;

// Device address constants (s0:addr)
#define S0_NULL      0
#define S0_HYPERVISOR 1
#define S0_SHELL     2000
#define S0_URANDOM   6969
#define S0_OPENTEST  1337

// Returns from sysaxi_getc
#define SYSAXI_NO_INPUT ((uint64_t)-1)
#define SYSAXI_EOF      ((uint64_t)-2)

long sysaxi(int mode, uint64_t num, void* req);
long sysaxi_open(uint64_t addr, void* args);
void sysaxi_print(const char* str);
void sysaxi_putc(char c);
uint64_t sysaxi_getc(void);

#endif
