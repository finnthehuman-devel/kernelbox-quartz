#include <stddef.h>
#include "kbox.h"
#include "vfs.h"
#include "sysaxi.h"

extern int kern_main(long tty_fd);

void kbox_main(void) {
    vfs_init();
    sysaxi_print("[KBox] Quartz 1.3 Init...\n");

    long tty_fd = sysaxi_open(S0_SHELL, NULL);
    if (tty_fd < 0) {
        sysaxi_print("[KBox] PANIC: No TTY\n");
        sysaxi(SYSAXI_SVCIN, SYS_PANIC, "no tty");
    }

    vfs_node* app_node = vfs_resolve("/system/kern");
    if (!app_node || !app_node->data) {
        sysaxi_print("[KBox] PANIC: /system/kern not found in VFS\n");
        sysaxi(SYSAXI_SVCIN, SYS_PANIC, "missing kern");
    }

    sysaxi_print("[KBox] Spawning /system/kern...\n");
    int (*entry)(long) = (int (*)(long))app_node->data;
    entry(tty_fd);

    sysaxi(SYSAXI_SVCIN, SYS_PANIC, "init: kern exited");
}
