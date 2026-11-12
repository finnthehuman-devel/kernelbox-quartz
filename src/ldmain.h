#ifndef LDMAIN_H
#define LDMAIN_H

#include <stdint.h>
#include <stddef.h>

// Unified ELF loader interface.
//
// ld_load_and_exec(path) does the whole load pipeline in one call:
//   1. canonicalize path (Stage-1: caller passes a fake-chroot-absolute path)
//   2. HVC_STAT_FILE       -> size, mode
//   3. HVC_READ_BYTES      -> blob (into the loader's static buffer)
//   4. elf_validate(blob)  -> verdict
//   5. EIPC_SYS_EXEC_BLOB  -> cvm_hv.c writes tmp, forks, execve, waitpid,
//                             drains slot, longjmp back to the shell
//
// "Skip the extra file load" = no HVC_STAT_FILE + HVC_READ_BYTES in
// linkerctl, then a second pass in cvm_hv.c. The blob is read once, here.
//
// Returns:
//   0                  on normal PIE completion (shell resumed via longjmp)
//   ELF_EINVAL  (-22)  on malformed input, unreadable path, or short read
//   ELF_ENOEXEC (-8)   on unsupported machine / type / truncated phdrs
//   ELF_ENOSYS  (-38)  on Stage-2 features (dynlink, ARM32, .o, interp missing)
//
// ld_load_and_exec does NOT return on the PIE's normal exit path.
// It returns only when the load is refused.

int ld_load_and_exec(const char* path);

// Mode variants for `ld` (validate only) and `exec` (validate + exec).
// ld_validate(path) never execs; it stops after elf_validate and prints
// the verdict. Stage-1 keeps both because the shell has `ld` and `exec`.

int ld_validate(const char* path);

// ELF verdict, filled by elf_validate. Shared with kern.c so the shell
// can print a one-line refusal without re-reading the blob.

struct elf_verdict {
    uint8_t  class;      // ELFCLASS64 (2) on success
    uint8_t  machine;    // EM_AARCH64 (183) on success
    uint16_t type;       // ET_DYN or ET_EXEC on success
    uint64_t entry;      // e_entry
    int32_t  err;        // 0 on success; one of ELF_E*
    const char* reason;  // static string; "stage-2: …" for ELF_ENOSYS
    uint8_t  has_interp; // PT_INTERP present?
    uint8_t  interp_found; // if has_interp: exists under FAKE_ROOT?
    char     interp[128];  // PT_INTERP path (Stage-1: for diagnostics only)
};

// errno-ish values (freestanding — no errno.h)
#define ELF_EINVAL  (-22)
#define ELF_ENOEXEC (-8)
#define ELF_ENOSYS  (-38)

// ELF constants (freestanding — <elf.h> not available on kernel side)
#define ELF_MAGIC0 0x7f
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'

#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN  3

#define EM_ARM     40
#define EM_X86_64  62
#define EM_AARCH64 183

#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3

// Stage-1 blob cap. ldmain.c's static buffer.
#define LD_BLOB_MAX (256u * 1024u)

#endif
