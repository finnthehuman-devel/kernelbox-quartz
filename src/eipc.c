#include "bus.h"

extern long cvm_trap_bus(int mode, uint64_t num, void* req);

long eipc_call(int mode, uint64_t num, void* req) {
    return cvm_trap_bus(mode, num, req);
}

static uint64_t kstrlen(const char* s) {
    uint64_t n = 0; while (s[n]) n++; return n;
}

void eipc_print(const char* str) {
    if (!str) return;
    eipc_write_req req = { str, kstrlen(str) };
    eipc_call(0, EIPC_SYS_WRITE, &req);
}

void eipc_putc(char c) {
    char cbuf = c;
    eipc_write_req req = { &cbuf, 1 };
    eipc_call(0, EIPC_SYS_WRITE, &req);
}

uint64_t eipc_getc(void) {
    char c = 0;
    long ret = eipc_call(1, EIPC_SYS_READ, &c);
    if (ret == 1) return (uint64_t)(unsigned char)c;
    if (ret == 0) return (uint64_t)-2; // EOF
    return (uint64_t)-1; // No input
}
