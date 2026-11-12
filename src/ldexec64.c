// ldexec64.c — AArch64 unified ELF loader.
//
// One function does the whole load: read, validate, exec. No intermediate
// linkerctl.c pass, no second HVC_STAT_FILE, no separate elf_validate
// caller. The blob is in ldmain's static buffer; the verdict is computed
// in-place; cvm_hv.c's EIPC_SYS_EXEC_BLOB writes it to tmp and execve's it.
//
// Freestanding. No libc. No malloc. All I/O via hvcbus_call / eipc_call.
//
// Refusals are -ENOSYS + "stage-2: <feature>" (never -EPERM) for features
// we don't implement, and -ENOEXEC for blobs this machine cannot run.
// That distinction is a 1.4 constraint: refuse because we don't implement
// it, not because it's "unsafe". See §XIV and the security thread.

#include "ldmain.h"
#include "bus.h"

// Static blob buffer. 256KB cap. .bss, not stack — the shell's stack is
// small and the blob is read before any exec happens.
static uint8_t g_blob[LD_BLOB_MAX];

// --- little helpers (freestanding) ---

static int lk_streq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static uint64_t lk_strlen(const char* s) {
    uint64_t n = 0; while (s[n]) n++; return n;
}

static void lk_print(const char* s) { eipc_print(s); }

static void lk_print_hex(uint64_t v) {
    char buf[19]; buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int nib = (int)((v >> ((15 - i) * 4)) & 0xF);
        buf[2 + i] = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
    }
    buf[18] = 0;
    lk_print(buf);
}

// --- path canonicalization (Stage-1: no cwd, no "..") ---
//
// "./foo" -> "/foo"
// "foo"   -> "/foo"
// "/foo"  -> "/foo"
// "/.//foo" -> "/foo"
//
// Collapses consecutive slashes, drops "." segments. Refuses ".." with
// -EINVAL (Stage-2 feature; Stage-1 has no cwd to make ".." meaningful).

static int canonicalize(const char* in, char* out, int out_max) {
    if (!in || !*in) { out[0] = '/'; out[1] = 0; return 0; }
    int j = 0;
    int i = 0;
    // skip leading slashes and "./" prefixes
    while (in[i] == '/') i++;
    if (in[i] == '.' && (in[i+1] == '/' || in[i+1] == 0)) {
        i++;
        while (in[i] == '/') i++;
    }
    if (in[i] == 0) { out[0] = '/'; out[1] = 0; return 0; }
    out[j++] = '/';
    while (in[i]) {
        // check for ".." segment
        if (in[i] == '.' && in[i+1] == '.' &&
            (in[i+2] == '/' || in[i+2] == 0)) {
            return ELF_EINVAL; // Stage-2: ".." unsupported
        }
        // collapse "/./"
        if (in[i] == '.' && (in[i+1] == '/' || in[i+1] == 0)) {
            i++;
            while (in[i] == '/') i++;
            continue;
        }
        if (in[i] == '/') { i++; continue; } // collapse multiple
        if (j >= out_max - 1) return ELF_EINVAL; // path too long
        out[j++] = in[i++];
        if (in[i] == '/') {
            out[j++] = '/';
            i++;
            while (in[i] == '/') i++;
        }
    }
    if (j > 1 && out[j-1] == '/') j--; // trim trailing slash
    out[j] = 0;
    return 0;
}

// --- ELF header readers (LE, ELF64) ---

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t* p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

// --- elf_validate: the two layers ---
//
// Layer 1: where is the header? (not "is this ELF")
// Layer 2: does the blob speak the slot ABI?
//
// See the "one bit smarter" turn: format is a consequence of the ABI,
// not a precondition for being run. Layer 1 is only there so we know
// where to read e_entry and e_phoff from.

static int elf_validate(const uint8_t* blob, size_t len,
                        struct elf_verdict* out) {
    if (!out) return ELF_EINVAL;

    // zero the verdict
    out->class = 0; out->machine = 0; out->type = 0; out->entry = 0;
    out->err = ELF_EINVAL; out->reason = "stage-1: null or short blob";
    out->has_interp = 0; out->interp_found = 0; out->interp[0] = 0;

    // LAYER 1: where is the header?
    if (!blob || len < 64) return ELF_EINVAL;
    if (blob[0] != ELF_MAGIC0 || blob[1] != ELF_MAGIC1 ||
        blob[2] != ELF_MAGIC2 || blob[3] != ELF_MAGIC3) {
        out->err = ELF_ENOEXEC; out->reason = "stage-1: not ELF (bad magic)";
        return 0;
    }
    out->class = blob[4];
    if (out->class != ELFCLASS64) {
        out->err = ELF_ENOSYS;
        out->reason = (out->class == ELFCLASS32)
            ? "stage-2: ELFCLASS32 / ARM32 (Legacy Floor deferred)"
            : "stage-1: unknown ELF class";
        return 0;
    }
    if (blob[5] != ELFDATA2LSB) {
        out->err = ELF_ENOEXEC; out->reason = "stage-1: big-endian unsupported";
        return 0;
    }

    // LAYER 2: does the blob speak the slot ABI?
    out->type    = rd16(blob + 16);
    out->machine = rd16(blob + 18);
    out->entry   = rd64(blob + 24);

    if (out->machine != EM_AARCH64) {
        if (out->machine == EM_ARM) {
            out->err = ELF_ENOSYS; out->reason = "stage-2: ARM32 (Legacy Floor)";
        } else if (out->machine == EM_X86_64) {
            out->err = ELF_ENOEXEC;
            out->reason = "stage-1: x86_64 unsupported (no runtime translation, §XIV)";
        } else {
            out->err = ELF_ENOEXEC; out->reason = "stage-1: unsupported machine";
        }
        return 0;
    }
    if (out->type == ET_REL) {
        out->err = ELF_ENOSYS;
        out->reason = "stage-2: relocatable object (.o) — use linkerctl -EC";
        return 0;
    }
    if (out->type != ET_EXEC && out->type != ET_DYN) {
        out->err = ELF_ENOEXEC; out->reason = "stage-1: unsupported e_type";
        return 0;
    }
    if (out->entry == 0) {
        out->err = ELF_ENOSYS;
        out->reason = "stage-2: e_entry==0 (loaded as library, not exec)";
        return 0;
    }

    // program headers in bounds
    uint64_t phoff     = rd64(blob + 32);
    uint16_t phentsize = rd16(blob + 54);
    uint16_t phnum     = rd16(blob + 56);
    if (phoff == 0 || phnum == 0) {
        out->err = ELF_ENOEXEC; out->reason = "stage-1: no program headers";
        return 0;
    }
    if (phentsize < 56) {
        out->err = ELF_ENOEXEC; out->reason = "stage-1: bad e_phentsize";
        return 0;
    }
    uint64_t need = phoff + (uint64_t)phnum * (uint64_t)phentsize;
    if (need < phoff || need > (uint64_t)len) {
        out->err = ELF_ENOEXEC; out->reason = "stage-1: phdrs truncated";
        return 0;
    }

    // walk phdrs: find PT_INTERP (record path), PT_DYNAMIC (mark), PT_LOAD (count)
    // Stage-1 does NOT refuse dynamic PIEs. Bionic is at root and linker64
    // resolves DT_NEEDED at execve. We only stat-check PT_INTERP under
    // FAKE_ROOT so a PIE with a missing interpreter gets a clean error
    // instead of -ENOENT from the host execve.
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t* ph = blob + phoff + (uint64_t)i * phentsize;
        uint32_t p_type = rd32(ph + 0);
        uint64_t p_offset = rd64(ph + 8);
        uint64_t p_filesz = rd64(ph + 32);
        if (p_type == PT_INTERP) {
            if (p_filesz == 0 || p_offset + p_filesz > (uint64_t)len) {
                out->err = ELF_ENOEXEC; out->reason = "stage-1: bad PT_INTERP";
                return 0;
            }
            uint64_t copy = p_filesz;
            if (copy >= sizeof(out->interp)) copy = sizeof(out->interp) - 1;
            for (uint64_t k = 0; k < copy; k++) {
                out->interp[k] = (char)blob[p_offset + k];
            }
            out->interp[copy] = 0;
            out->has_interp = 1;
        }
    }

    // stat-check PT_INTERP under FAKE_ROOT, if present.
    // No interp (musl static-pie): accepted, no stat.
    if (out->has_interp) {
        // build a stat request the same way ld_read_blob does
        struct { const char* path; hvc_stat_reply out; } sr;
        sr.path = out->interp;
        long rc = hvcbus_call(HVC_STAT_FILE, &sr);
        if (rc != 0) {
            out->err = ELF_ENOEXEC;
            out->reason = "stage-1: PT_INTERP not found under FAKE_ROOT";
            return 0;
        }
        out->interp_found = 1;
    }

    out->err = 0;
    out->reason = "ok";
    return 0;
}

// --- blob reading via the bus ---

// Reads the file at `path` into g_blob. Returns bytes read (>0) on success,
// negative on error. `path` must be canonical (leading '/', no "..").
static long ld_read_blob(const char* path, uint64_t* out_len) {
    struct { const char* path; hvc_stat_reply out; } sr;
    sr.path = path;
    long rc = hvcbus_call(HVC_STAT_FILE, &sr);
    if (rc != 0) {
        lk_print("ld: stat failed: "); lk_print(path); lk_print("\n");
        return -1;
    }
    if (sr.out.mode != 0) {
        lk_print("ld: not a regular file: "); lk_print(path); lk_print("\n");
        return -1;
    }
    if (sr.out.size == 0) {
        lk_print("ld: empty file: "); lk_print(path); lk_print("\n");
        return -1;
    }
    if (sr.out.size > LD_BLOB_MAX) {
        lk_print("ld: blob exceeds Stage-1 limit (256KB)\n");
        return -1;
    }

    struct { const char* path; hvc_read_req rr; } rq;
    rq.path = path;
    rq.rr.buf = g_blob;
    rq.rr.len = sr.out.size;
    rq.rr.off = 0;
    rq.rr.nread = 0;

    rc = hvcbus_call(HVC_READ_BYTES, &rq);
    if (rc != 0 || rq.rr.nread == 0) {
        lk_print("ld: read failed: "); lk_print(path); lk_print("\n");
        return -1;
    }
    *out_len = rq.rr.nread;
    return 0;
}

// --- public entry points ---

int ld_validate(const char* path) {
    char canon[256];
    if (canonicalize(path, canon, sizeof(canon)) != 0) {
        lk_print("ld: invalid path\n");
        return ELF_EINVAL;
    }
    uint64_t len = 0;
    if (ld_read_blob(canon, &len) != 0) return ELF_EINVAL;

    struct elf_verdict v;
    (void)elf_validate(g_blob, (size_t)len, &v);
    if (v.err != 0) {
        lk_print("ld: refused: "); lk_print(v.reason); lk_print("\n");
        return v.err;
    }
    lk_print("ld: ok AArch64 ");
    lk_print(v.type == ET_DYN ? "PIE" : "EXEC");
    lk_print(" entry="); lk_print_hex(v.entry);
    if (v.has_interp) {
        lk_print(" interp="); lk_print(v.interp);
    } else {
        lk_print(" (static-pie)");
    }
    lk_print("\n");
    return 0;
}

int ld_load_and_exec(const char* path) {
    char canon[256];
    if (canonicalize(path, canon, sizeof(canon)) != 0) {
        lk_print("ld: invalid path\n");
        return ELF_EINVAL;
    }
    uint64_t len = 0;
    if (ld_read_blob(canon, &len) != 0) return ELF_EINVAL;

    struct elf_verdict v;
    (void)elf_validate(g_blob, (size_t)len, &v);
    if (v.err != 0) {
        lk_print("ld: refused: "); lk_print(v.reason); lk_print("\n");
        return v.err;
    }

    // Pass the blob and length straight to cvm_hv.c. cvm_hv.c writes
    // it to tmp, forks, execve's, waitpid's, drains the slot page,
    // and longjmp's back to the shell. On the normal path, this call
    // does NOT return to us.
    eipc_write_req er = { g_blob, len };
    long rc = eipc_call(0, EIPC_SYS_EXEC_BLOB, &er);
    if (rc != 0) {
        lk_print("ld: exec busy or failed\n");
        return (int)rc;
    }
    return 0; // unreachable on the normal path
}
