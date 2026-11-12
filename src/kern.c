#include "bus.h"

extern int ld_validate(const char* path);
extern int ld_load_and_exec(const char* path);

static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void handle_path(const char* arg, char* out, int max) {
    if (!arg) { out[0] = '/'; out[1] = 0; return; }
    int i = 0, j = 0;
    if (arg[0] != '/') { out[j++] = '/'; }
    while (arg[i] && j < max - 1) { out[j++] = arg[i++]; }
    out[j] = 0;
}

int kern_main(long tty_fd) {
    eipc_print("\n[kern] Quartz Applet Shell (1.3.02).\n");
    eipc_print("[kern] Commands: ls, cat, ld <file>, exec <file>, exit.\n");

    char buf[128];
    int pos = 0;

    while (1) {
        eipc_print("kern:/> ");
        pos = 0;
        while (1) {
            uint64_t c = eipc_getc();
            if (c == (uint64_t)-2) return 0;
            if (c == (uint64_t)-1) continue;
            char ch = (char)c;
            if (ch == '\n' || ch == '\r') { eipc_print("\n"); break; }
            if (ch == 0x7F || ch == '\b') {
                if (pos > 0) { pos--; eipc_print("\b \b"); }
                continue;
            }
            if (pos < sizeof(buf) - 1) { buf[pos++] = ch; eipc_putc(ch); }
        }
        buf[pos] = 0;
        if (pos == 0) continue;

        char* cmd = buf;
        char* arg = NULL;
        for (int i = 0; i < pos; i++) {
            if (buf[i] == ' ') { buf[i] = 0; arg = &buf[i + 1]; break; }
        }

        if (streq(cmd, "ls")) {
            char path[128]; handle_path(arg, path, sizeof(path));
            hvcbus_call(HVC_LIST_DIR, path);
        } else if (streq(cmd, "cat")) {
            if (!arg) eipc_print("usage: cat <file>\n");
            else { char path[128]; handle_path(arg, path, sizeof(path));
                   hvcbus_call(HVC_READ_FILE, path); }
        } else if (streq(cmd, "ld")) {
            if (!arg) eipc_print("usage: ld <elf>\n");
            else (void)ld_validate(arg);  // -l only
        } else if (streq(cmd, "exec")) {
            if (!arg) eipc_print("usage: exec <elf>\n");
            else (void)ld_load_and_exec(arg);  // -lE
            // on normal PIE exit, cvm_hv.c longjmps shell_resume and the
            // loop continues from the top, as if exec returned 0 here.
        } else if (streq(cmd, "exit")) {
            return 0;
        } else {
            eipc_print("Unknown command: "); eipc_print(cmd); eipc_print("\n");
        }
    }
}
