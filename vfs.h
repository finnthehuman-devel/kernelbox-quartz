#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include "sysaxi.h"

#define VFS_MAX_NODES   64
#define VFS_NAME_LEN    32
#define VFS_MAX_DEPTH   16
#define VFS_MAX_CHILDREN 8

typedef enum { VFS_DIR, VFS_FILE, VFS_DEVICE } vfs_type;

typedef struct vfs_node {
    char name[VFS_NAME_LEN];
    vfs_type type;
    uint64_t dev_addr;      // s0:addr for devices, 0 otherwise
    int fd_legacy;          // Legacy fd number for devices
    struct vfs_node* parent;
    struct vfs_node* children[VFS_MAX_CHILDREN];
    int child_count;
    // File content support (tiny)
    char* data;
    uint32_t size;
} vfs_node;
extern vfs_node* vfs_root;
void vfs_init(void);
vfs_node* vfs_resolve(const char* path);
vfs_node* vfs_create_file(const char* path, const char* content);
vfs_node* vfs_create_dir(const char* path);
vfs_node* vfs_create_device(const char* path, int fd_legacy, uint64_t addr);
void vfs_list(vfs_node* dir);
vfs_node* vfs_find_child(vfs_node* parent, const char* name);

#endif

