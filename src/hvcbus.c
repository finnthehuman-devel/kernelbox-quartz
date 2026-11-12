#include "bus.h"

extern long cvm_trap_bus(int mode, uint64_t num, void* req);

long hvcbus_call(uint64_t opcode, void* req) {
    return cvm_trap_bus(0, opcode, req);
}
