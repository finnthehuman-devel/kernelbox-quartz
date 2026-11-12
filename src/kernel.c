#include "bus.h"

static void automount(void) {
    eipc_print("[kernel] Requesting Hypervisor chroot to kbox_fs...\n");
    hvcbus_call(HVC_CHROOT, "/data/data/com.termux/files/home/kbox_fs");
    
    eipc_print("[kernel] Mounting virtual paths...\n");
    hvcbus_call(HVC_MOUNT, "/system");
    hvcbus_call(HVC_MOUNT, "/dev");
    hvcbus_call(HVC_MOUNT, "/lib");
    hvcbus_call(HVC_MOUNT, "/boot");
    hvcbus_call(HVC_MOUNT, "/apex");
    hvcbus_call(HVC_MOUNT, "/etc");
    hvcbus_call(HVC_MOUNT, "/proc");
    hvcbus_call(HVC_MOUNT, "/sys");

    eipc_print("[kernel] Real FS is now /.\n");
}

extern int kern_main(long tty_fd);

void kbox_main(void) {
    eipc_print("[KBox] Quartz 1.4 Real FS Init...\n");
    automount();
    
    eipc_print("[kernel] Spawning /system/kern...\n");
    kern_main(0);
    
    eipc_print("[kernel] kern exited.\n");
}
