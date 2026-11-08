#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

#include "sysaxi.h"

void kbox_main();

// Host fd table. KBox doesn't see these; they're the hypervisor's
// private backing for s0 addresses.
static int host_fd_for_addr(uint64_t addr) {
    switch (addr) {
        case S0_NULL:       return open("/dev/null", O_RDWR);
        case S0_HYPERVISOR: return -1;   // no host backing yet
        case S0_SHELL:      return 0;    // stdin/stdout
        case S0_URANDOM:    return open("/dev/urandom", O_RDONLY);
        case S0_OPENTEST:   return -1;   // no host backing
        default:            return -1;
    }
}

long cvm_trap_bus(int mode, uint64_t num, void* req) {
    if (mode == SYSAXI_SVCIN) {
        switch (num) {
            case SYS_WRITE: {
                const sysaxi_write_req* r = (const sysaxi_write_req*)req;
                if (!r || !r->ptr || r->len == 0) return 0;
                // Write exactly r->len bytes. No strlen, no OOB read.
                ssize_t w = write(1, r->ptr, (size_t)r->len);
                return (long)w;
            }
            case SYS_OPEN: {
                uint64_t* params = (uint64_t*)req;
                if (!params) return -1;
                int fd = host_fd_for_addr(params[0]);
                return (long)fd;
            }
            case SYS_CLOSE:
                return 0;
            case SYS_PANIC: {
                const char* msg = (const char*)req;
                fprintf(stderr, "\n[KBox PANIC] %s\n", msg ? msg : "(null)");
                exit(1);
            }
            default:
                return -1;
        }
    }

    if (mode == SYSAXI_SVCOUT) {
        // Input: read exactly one byte into req. Return 1 on success,
        // 0 on EOF, -1 on no input / error.
        char* buf = (char*)req;
        if (!buf) return -1;
        ssize_t r = read(0, buf, 1);
        return (long)r;   // 1 / 0 / -1
    }

    return -1;
}

int main(void) {
    printf("[CVMpipe] KBox 1.2 Hypervisor\n");
    printf("[CVMpipe] Booting...\n");
    kbox_main();
    return 0;
}
