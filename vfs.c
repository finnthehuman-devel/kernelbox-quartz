#include "vfs.h"
#include <stddef.h>

vfs_node  vfs_pool[VFS_MAX_NODES];
int       vfs_pool_count = 0;
vfs_node* vfs_root = NULL;

static vfs_node* vfs_alloc(void) {
    if (vfs_pool_count >= VFS_MAX_NODES) return NULL;
    vfs_node* n = &vfs_pool[vfs_pool_count++];
    // Zero the slot. Static pool is already zero, but vfs_init may be
    // called again; be explicit.
    for (uint32_t i = 0; i < sizeof(vfs_node); i++) ((uint8_t*)n)[i] = 0;
    return n;
}

// strcmp-shape: cannot read past the NUL of either side.
static int vfs_name_eq(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

vfs_node* vfs_find_child(vfs_node* parent, const char* name) {
    if (!parent || !name) return NULL;
    for (int i = 0; i < parent->child_count; i++) {
        if (vfs_name_eq(parent->children[i]->name, name))
            return parent->children[i];
    }
    return NULL;
}

static void vfs_add_child(vfs_node* parent, vfs_node* child) {
    if (parent->child_count >= VFS_MAX_CHILDREN) return;
    parent->children[parent->child_count++] = child;
    child->parent = parent;
}

// Copy src into dst (capacity cap), NUL-terminating. Returns bytes written
// excluding the NUL. Cannot leave dst unterminated.
static int vfs_strcpy_n(char* dst, int cap, const char* src) {
    int i = 0;
    while (src[i] && i < cap - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return i;
}

vfs_node* vfs_create_dir(const char* path) {
    if (!path || path[0] != '/') return NULL;

    char component[VFS_NAME_LEN];
    vfs_node* curr = vfs_root;
    int i = (path[0] == '/') ? 1 : 0;
    int j = 0;
    int depth = 0;

    while (path[i] && depth < VFS_MAX_DEPTH) {
        if (path[i] == '/') {
            if (j > 0) {
                component[j] = 0;
                vfs_node* next = vfs_find_child(curr, component);
                if (!next) {
                    next = vfs_alloc();
                    if (!next) return NULL;
                    vfs_strcpy_n(next->name, VFS_NAME_LEN, component);
                    next->type = VFS_DIR;
                    vfs_add_child(curr, next);
                }
                curr = next;
                j = 0;
            }
            depth++;
        } else {
            if (j < VFS_NAME_LEN - 1) component[j++] = path[i];
        }
        i++;
    }

    // Trailing component (path didn't end in '/')
    if (j > 0) {
        component[j] = 0;
        vfs_node* next = vfs_find_child(curr, component);
        if (!next) {
            next = vfs_alloc();
            if (!next) return NULL;
            vfs_strcpy_n(next->name, VFS_NAME_LEN, component);
            next->type = VFS_DIR;
            vfs_add_child(curr, next);
        }
        curr = next;
    }

    return curr;
}

vfs_node* vfs_create_file(const char* path, const char* content) {
    if (!path) return NULL;

    // Split into parent_path + filename.
    int last_slash = -1;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') last_slash = i;
    }

    char parent_path[VFS_NAME_LEN * VFS_MAX_DEPTH];
    char filename[VFS_NAME_LEN];

    int parent_len = (last_slash > 0) ? last_slash : 1;
    if (parent_len >= (int)sizeof(parent_path)) return NULL;

    if (last_slash > 0) {
        for (int i = 0; i < last_slash; i++) parent_path[i] = path[i];
        parent_path[last_slash] = 0;
    } else {
        parent_path[0] = '/';
        parent_path[1] = 0;
    }

    int fn_start = (last_slash >= 0) ? last_slash + 1 : 0;
    int fn_len = vfs_strcpy_n(filename, VFS_NAME_LEN, path + fn_start);
    if (fn_len == 0) return NULL;   // "/foo/" -> empty filename

    vfs_node* parent = vfs_resolve(parent_path);
    if (!parent) parent = vfs_create_dir(parent_path);
    if (!parent) return NULL;

    vfs_node* existing = vfs_find_child(parent, filename);
    if (existing) return existing;

    vfs_node* file = vfs_alloc();
    if (!file) return NULL;

    vfs_strcpy_n(file->name, VFS_NAME_LEN, filename);
    file->type = VFS_FILE;
    file->data = (char*)content;
    file->size = 0;
    if (content) {
        uint32_t n = 0;
        while (content[n]) n++;
        file->size = n;
    }
    vfs_add_child(parent, file);
    return file;
}

vfs_node* vfs_create_device(const char* path, int fd_legacy, uint64_t addr) {
    (void)fd_legacy;   // kept for source compat; dev_addr is the identity
    vfs_node* dev = vfs_create_file(path, NULL);
    if (!dev) return NULL;
    dev->type = VFS_DEVICE;
    dev->fd_legacy = fd_legacy;
    dev->dev_addr = addr;
    return dev;
}

vfs_node* vfs_resolve(const char* path) {
    if (!path || path[0] != '/') return NULL;
    if (path[1] == 0) return vfs_root;

    vfs_node* curr = vfs_root;
    char component[VFS_NAME_LEN];
    int i = 1, j = 0, depth = 0;

    while (path[i] && depth < VFS_MAX_DEPTH) {
        if (path[i] == '/') {
            if (j > 0) {
                component[j] = 0;
                curr = vfs_find_child(curr, component);
                if (!curr) return NULL;
                j = 0;
            }
            depth++;
        } else {
            if (j < VFS_NAME_LEN - 1) component[j++] = path[i];
        }
        i++;
    }

    if (j > 0) {
        component[j] = 0;
        curr = vfs_find_child(curr, component);
    }

    return curr;
}

// Small decimal printer that writes via sysaxi_putc (safe with {ptr,len} ABI).
static void vfs_put_uint(uint64_t v) {
    char buf[20];
    int n = 0;
    if (v == 0) { sysaxi_putc('0'); return; }
    while (v > 0 && n < (int)sizeof(buf)) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n > 0) sysaxi_putc(buf[--n]);
}

void vfs_list(vfs_node* dir) {
    if (!dir || dir->type != VFS_DIR) {
        sysaxi_print("  [not a directory]\n");
        return;
    }
    for (int i = 0; i < dir->child_count; i++) {
        vfs_node* child = dir->children[i];
        sysaxi_print("  ");
        if (child->type == VFS_DIR)         sysaxi_print("[D] ");
        else if (child->type == VFS_DEVICE) sysaxi_print("[C] ");
        else                                sysaxi_print("[F] ");
        sysaxi_print(child->name);
        if (child->type == VFS_DEVICE) {
            sysaxi_print("  (s0:");
            vfs_put_uint(child->dev_addr);
            sysaxi_print(")");
        }
        sysaxi_print("\n");
    }
}

void vfs_init(void) {
    vfs_pool_count = 0;
    vfs_root = vfs_alloc();
    if (!vfs_root) return;
    vfs_root->name[0] = '/';
    vfs_root->name[1] = 0;
    vfs_root->type = VFS_DIR;
    vfs_root->parent = NULL;
    vfs_root->child_count = 0;

    vfs_create_dir("/system");
    vfs_create_dir("/root");
    vfs_create_dir("/sysfs");
    vfs_create_dir("/dev");
    vfs_create_dir("/boot");

    vfs_create_file("/system/kernel.c", "// KBox kernel main\n");
    vfs_create_file("/system/sysaxi.c", "// System bus\n");
    vfs_create_file("/system/vfs.c",    "// VFS layer\n");
    vfs_create_file("/system/shell.c",  "// Shell\n");
    vfs_create_file("/system/memory.c", "// MM\n");

    vfs_create_file("/boot/initrd.img", "[binary data]\n");
    vfs_create_file("/boot/config",     "KBOX_CFG_V1\n");

    vfs_create_file("/sysfs/cpuinfo", "KBox CPU v1\n");
    vfs_create_file("/sysfs/meminfo", "64K RAM\n");

    vfs_create_device("/dev/null",  0,    S0_NULL);
    vfs_create_device("/dev/kvm",   1,    S0_HYPERVISOR);
    vfs_create_device("/dev/tty0",  2000, S0_SHELL);
    vfs_create_device("/dev/urandom", 6969, S0_URANDOM);
    vfs_create_device("/dev/poopooheadDEADBEEF", 1337, S0_OPENTEST);

    /* Quartz 1.3 Applet Staging */
    extern int kern_main(long tty_fd);
    vfs_node* kern_node = vfs_create_file("/system/kern", NULL);
    if (kern_node) {
        kern_node->data = (char*)kern_main;
        kern_node->size = 0;
    }

}
