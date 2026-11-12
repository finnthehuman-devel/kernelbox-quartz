#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stddef.h>

// --- EIPC (External IPC) Modes ---
#define EIPC_SYS_READ  1
#define EIPC_SYS_WRITE 64
#define EIPC_SYS_OPEN  2
#define EIPC_SYS_CLOSE 3
#define EIPC_SYS_EXIT  93

// --- HVC (Hypervisor Control Bus) Opcodes ---
#define HVC_MOUNT      100
#define HVC_UMOUNT     101
#define HVC_EXEC       102
#define HVC_CHROOT     103
#define HVC_LIST_DIR   104
#define HVC_READ_FILE  105
#define HVC_STAT_FILE     107
#define HVC_READ_BYTES    108

// --- ELF Loader v1.0.0 Stage-1 (EIPC side, kernel -> hypervisor) ---
#define EIPC_SYS_EXEC_BLOB   200
#define EIPC_SYS_EXEC_CANCEL 201

typedef struct {
    const void* ptr;
    uint64_t len;
} eipc_write_req;

// req for HVC_READ_BYTES: kernel provides buf/len; hv copies file bytes in
typedef struct {
    void*    buf;
    uint64_t len;
    uint64_t off;   // file offset (stage-2 will use; stage-1 always 0)
    uint64_t nread; // out: bytes actually copied
} hvc_read_req;

// reply for HVC_STAT_FILE
typedef struct {
    uint64_t size;
    uint32_t mode;  // 0 = regular file, 1 = dir, 2 = other
} hvc_stat_reply;

// EIPC Interface
long eipc_call(int mode, uint64_t num, void* req);
void eipc_print(const char* str);
void eipc_putc(char c);
uint64_t eipc_getc(void);

// HVC Interface
long hvcbus_call(uint64_t opcode, void* req);

#endif
