#include "sysaxi.h"
#include <stddef.h>

extern long cvm_trap_bus(int mode, uint64_t num, void* req);

long sysaxi(int mode, uint64_t num, void* req) {
    return cvm_trap_bus(mode, num, req);
}

long sysaxi_open(uint64_t addr, void* args) {
    uint64_t req[2] = { addr, (uint64_t)args };
    return sysaxi(SYSAXI_SVCIN, SYS_OPEN, req);
}

// Freestanding strlen. Keep it local; no libc.
static uint64_t kstrlen(const char* s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

void sysaxi_print(const char* str) {
    if (!str) return;
    sysaxi_write_req req = { str, kstrlen(str) };
    sysaxi(SYSAXI_SVCIN, SYS_WRITE, &req);
}

void sysaxi_putc(char c) {
    char cbuf = c;
    sysaxi_write_req req = { &cbuf, 1 };
    sysaxi(SYSAXI_SVCIN, SYS_WRITE, &req);
}

uint64_t sysaxi_getc(void) {
    char c = 0;
    long ret = sysaxi(SYSAXI_SVCOUT, SYS_READ, &c);
    if (ret == 1) return (uint64_t)(unsigned char)c;
    if (ret == 0) return SYSAXI_EOF;
    return SYSAXI_NO_INPUT;
}
