#include "sysaxi.h"
#include "vfs.h"
#include <stddef.h>

static vfs_node* cwd = NULL;

static int streq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static void print_path(vfs_node* node) {
    if (!node || node == vfs_root) { sysaxi_print("/"); return; }
    // Simplified path printing for 1.3
    sysaxi_print("/");
    sysaxi_print(node->name);
}

int kern_main(long tty_fd) {
    sysaxi_print("\n[kern] Quartz Applet Shell initialized.\n");
    cwd = vfs_root;
    char buf[64];
    int pos = 0;

    while (1) {
        sysaxi_print("kern:");
        print_path(cwd);
        sysaxi_print("> ");
        pos = 0;
        
        // Read line
        while (1) {
            uint64_t c = sysaxi_getc();
            if (c == SYSAXI_EOF) return 0;
            if (c == SYSAXI_NO_INPUT) continue;
            char ch = (char)c;
            if (ch == '\n' || ch == '\r') { sysaxi_print("\n"); break; }
            if (ch == 0x7F || ch == '\b') {
                if (pos > 0) { pos--; sysaxi_print("\b \b"); }
                continue;
            }
            if (pos < sizeof(buf) - 1) { buf[pos++] = ch; sysaxi_putc(ch); }
        }
        buf[pos] = 0;
        if (pos == 0) continue;

        // Parse command and argument
        char* cmd = buf;
        char* arg = NULL;
        for (int i = 0; i < pos; i++) {
            if (buf[i] == ' ') {
                buf[i] = 0;
                arg = &buf[i + 1];
                break;
            }
        }

        // Dispatch
        if (streq(cmd, "ls")) {
            vfs_list(cwd);
        } else if (streq(cmd, "pwd")) {
            print_path(cwd);
            sysaxi_print("\n");
        } else if (streq(cmd, "cd")) {
            if (!arg || streq(arg, "/")) {
                cwd = vfs_root;
            } else {
                vfs_node* target = vfs_resolve(arg); // Simplified: only absolute paths for now
                if (target && target->type == VFS_DIR) {
                    cwd = target;
                } else {
                    sysaxi_print("cd: no such directory\n");
                }
            }
        } else if (streq(cmd, "cat")) {
            if (!arg) {
                sysaxi_print("usage: cat <file>\n");
            } else {
                vfs_node* file = vfs_resolve(arg);
                if (file && file->type == VFS_FILE && file->data) {
                    sysaxi_print(file->data);
                    sysaxi_print("\n");
                } else {
                    sysaxi_print("cat: file not found\n");
                }
            }
        } else if (streq(cmd, "exit")) {
            return 0;
        } else {
            sysaxi_print("Unknown command: ");
            sysaxi_print(cmd);
            sysaxi_print("\n");
        }
    }
}
