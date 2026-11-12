#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <dirent.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include "bus.h"

void kbox_main();

#define FAKE_ROOT "/data/data/com.termux/files/home/kbox_fs"

// --- Stage-1 PIE state (kernel-side shell resume + process lock) ---
static jmp_buf        shell_resume;
static volatile int   process_lock;   // 0 = shell owns shell_resume, 1 = PIE running

// PIE teardown: auto-cancel, flush, release spinlock, jump to shell.
// Called from EXEC_BLOB's entry-return path, or from EXEC_CANCEL.
// TODO 1.4: replace munmap with mmb_reclaim (region-granular, §XIV).
static void pie_teardown(void* mapped_base, size_t mapped_len) {
    // 1. auto-cancel: any pending EIPC from the PIE is dropped here.
    //    Stage-1 has no EIPC queue for the PIE (it talks back synchronously),
    //    so this is a no-op placeholder that Stage-2 fills in when PIE
    //    requests become async. Kept as a named step so the invariant
    //    "auto-cancel on exit" is visible in code, not just prose.
    // 2. flush: munmap the mapped image.
    if (mapped_base && mapped_len) munmap(mapped_base, mapped_len);
    // 3. process spinlock: other processes spin (they see process_lock==0
    //    and race for it); linkerctl idles (it is not on this stack).
    process_lock = 0;
    // 4. jump to shell normally. longjmp skips linkerctl's frame entirely.
    longjmp(shell_resume, 1);
}

// Resume helper called from kern.c. kern.c is freestanding, so setjmp
// must live here. Returns only on refusal (never on normal PIE exit).

static long do_stat_file(const char* path, hvc_stat_reply* out) {
    char full[512];
    if (strcmp(path, "/") == 0) snprintf(full, sizeof(full), "%s", FAKE_ROOT);
    else snprintf(full, sizeof(full), "%s%s", FAKE_ROOT, path);

    struct stat st;
    if (stat(full, &st) != 0) return -1;
    out->size = (uint64_t)st.st_size;
    out->mode = S_ISREG(st.st_mode) ? 0 : S_ISDIR(st.st_mode) ? 1 : 2;
    return 0;
}

static long do_read_bytes(const char* path, hvc_read_req* rr) {
    char full[512];
    if (strcmp(path, "/") == 0) return -1;
    snprintf(full, sizeof(full), "%s%s", FAKE_ROOT, path);

    int fd = open(full, O_RDONLY);
    if (fd < 0) return -1;
    if (rr->off) lseek(fd, (off_t)rr->off, SEEK_SET);
    ssize_t n = read(fd, rr->buf, (size_t)rr->len);
    close(fd);
    if (n < 0) return -1;
    rr->nread = (uint64_t)n;
    return 0;
}

// Stage-1 PIE execution: map the blob (already in kernel memory via EIPC),
// jump to entry using bionic primitives. Blob is ET_EXEC or ET_DYN.
static long do_exec_blob(const void* blob, uint64_t len) {
    if (process_lock) return -EAGAIN;

    // Note: we are given a kernel-side blob pointer. It points into kernel
    // .bss (linkerctl's static buffer). We copy it into a fresh anon map so
    // the PIE can have its own address space for the segments. Stage-1:
    // single segment copy is enough because execldr validated e_phoff bounds,
    // and we are trusting the ELF to have a contiguous first PT_LOAD at
    // offset 0 (true for every Android toolchain output).
    //
    // Stage-2 will parse phdrs and map each PT_LOAD at its p_vaddr.

    // Read entry from the blob (already validated).
    const uint8_t* p = (const uint8_t*)blob;
    uint16_t e_type    = (uint16_t)p[16] | ((uint16_t)p[17] << 8);
    uint64_t e_entry   = 0;
    for (int i = 0; i < 8; i++) e_entry |= ((uint64_t)p[24 + i]) << (i * 8);

    // For ET_EXEC: map at a fixed high address. For ET_DYN: map anonymous
    // and compute load bias. Stage-1 uses the simple path: MAP_ANON whole
    // blob, memcpy, then jump to base + (entry - page_align(entry)).
    // We do not honor p_vaddr for ET_EXEC (Stage-1 simplification; refuses
    // any ET_EXEC whose e_entry doesn't match a writable anon map — i.e.,
    // Stage-1 executes PIEs by design, and ET_EXEC passes only because
    // execldr accepted it. cvm_hv tolerates ET_EXEC if entry is < blob_len.
    if (e_type == ET_EXEC && e_entry >= len) {
        return -ENOEXEC; // ET_EXEC with out-of-blob entry: stage-2 feature
    }

    size_t page = (size_t)sysconf(_SC_PAGESIZE);
    size_t map_len = (size_t)len;
    // round up to page
    map_len = (map_len + page - 1) & ~(page - 1);

    void* base = mmap(NULL, map_len, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return -ENOMEM;
    memcpy(base, blob, (size_t)len);

    // mprotect to RX for ET_EXEC (its segments are already at their vaddrs
    // conceptually inside the blob). For ET_DYN, the whole blob is R+X with
    // data segments handled by the PIE's own relocations at runtime (which
    // work because the PIE was built PIE and its relocs are self-relative).
    // Stage-1: R+X the whole map. It's not W^X-safe for data segments,
    // but Stage-1 is single-PIE and the PIE trusts itself.
    if (mprotect(base, map_len, PROT_READ | PROT_EXEC) != 0) {
        munmap(base, map_len);
        return -EPERM;
    }

    // Entry point: for ET_DYN, entry is an offset from load base; base is
    // the mmap return. For ET_EXEC, entry is absolute but we've placed the
    // blob at `base`, so we treat entry as an offset into blob if it's within
    // [0, len), else refuse above. This is the Stage-1 "PIE in kernel" trick.
    void (*entry_fn)(void) = (void (*)(void))((uintptr_t)base + (uintptr_t)e_entry);

    process_lock = 1;

    // Save a resume point here so that even if the PIE returns normally
    // (rather than via EIPC_SYS_EXEC_CANCEL), we still unwind to the shell.
    // We use a per-exec setjmp distinct from shell_resume so the shell's
    // resume point stays stable across multiple execs.
    jmp_buf pie_return;
    if (setjmp(pie_return) == 0) {
        entry_fn();
        // PIE returned normally — fall through to teardown.
    }
    pie_teardown(base, map_len);
    return 0; // unreachable (pie_teardown longjmps)
}

long cvm_trap_bus(int mode, uint64_t num, void* req) {
    if (mode == 0) { // SVCIN (Kernel -> Hypervisor)
        switch (num) {
            case EIPC_SYS_WRITE: {
                const eipc_write_req* r = (const eipc_write_req*)req;
                if (!r || !r->ptr) return -1;
                return write(1, r->ptr, (size_t)r->len);
            }
            case HVC_CHROOT: {
                const char* path = (const char*)req;
                printf("[CVMpipe] Fake chroot initialized at %s\n", path);
                return 0;
            }
            case HVC_MOUNT: {
                const char* path = (const char*)req;
                printf("[CVMpipe] Mounted %s\n", path);
                return 0;
            }
            case HVC_LIST_DIR: {
                const char* path = (const char*)req;
                char full_path[512];
                if (strcmp(path, "/") == 0) snprintf(full_path, sizeof(full_path), "%s", FAKE_ROOT);
                else snprintf(full_path, sizeof(full_path), "%s%s", FAKE_ROOT, path);
                DIR* dir = opendir(full_path);
                if (!dir) { printf("ls: cannot access '%s'\n", path); return -1; }
                struct dirent* entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] == '.') continue;
                    printf("%s\n", entry->d_name);
                }
                closedir(dir);
                return 0;
            }
            case HVC_READ_FILE: {
                const char* path = (const char*)req;
                char full_path[512];
                if (strcmp(path, "/") == 0) return -1;
                snprintf(full_path, sizeof(full_path), "%s%s", FAKE_ROOT, path);
                FILE* f = fopen(full_path, "r");
                if (!f) { printf("cat: %s: No such file\n", path); return -1; }
                char line[256];
                while (fgets(line, sizeof(line), f)) printf("%s", line);
                fclose(f);
                return 0;
            }
            case HVC_STAT_FILE: {
                struct { const char* path; hvc_stat_reply* out; }* r = req;
                if (!r || !r->path || !r->out) return -1;
                return do_stat_file(r->path, r->out);
            }
            case HVC_READ_BYTES: {
                struct { const char* path; hvc_read_req* rr; }* r = req;
                if (!r || !r->path || !r->rr) return -1;
                return do_read_bytes(r->path, r->rr);
            }
            case EIPC_SYS_EXEC_BLOB: {
                const eipc_write_req* r = (const eipc_write_req*)req;
                if (!r || !r->ptr) return -1;
                return do_exec_blob(r->ptr, r->len);
            }
            case EIPC_SYS_EXEC_CANCEL:
                // auto-cancel path: PIE called _exit via the EIPC cancel trap.
                // pie_teardown needs the mapping; Stage-1 keeps it in a static
                // so the cancel path (which has no args) can find it.
                // See do_exec_blob: `base` and `map_len` must be stashed.
                // (Kept as a Stage-1 TODO: stash in static globals; the normal
                // path via pie_return handles it without this opcode.)
                return 0;
            case EIPC_SYS_EXIT:
                exit(0);
            default:
                return -1;
        }
    } else if (mode == 1) { // SVCOUT (Hypervisor -> Kernel)
        char* buf = (char*)req;
        ssize_t ret = read(0, buf, 1);
        return (long)ret;
    }
    return -1;
}

int main() {
    printf("[CVMpipe] Quartz 1.3.02 Hypervisor (ELF Loader v1.0.0 Stage-1)\n");
    kbox_main();
    return 0;
}
