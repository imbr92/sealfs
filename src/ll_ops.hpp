#pragma once

#include "common.hpp"

#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

void sealfs_init(void* userdata, struct fuse_conn_info *conn);

void sealfs_lookup(fuse_req_t req, fuse_ino_t parent, const char* name);

void sealfs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

static const struct fuse_lowlevel_ops sealfs_oper = {
    .init = sealfs_init,
    .lookup = sealfs_lookup,
    .getattr = sealfs_getattr,

    /*
     * Basics:
     *  - Path traversal: lookup, getattr, readdir
     *  - Creating/Modifying files: create/mknod, mkdir, read, write
     *  - File Lifecycle: unlink, rmdir, rename
     *  - Open/Close flow: open, release
     *  - Optional but Useful: flush, fsync, init, destroy
     */
    // .fuse_lowlevel_init = stuff
        // .lookup = hello_ll_lookup,
        // .getattr = hello_ll_getattr,
        // .readdir = hello_ll_readdir,
        // .open = hello_ll_open,
        // .read = hello_ll_read,
        // .setxattr = hello_ll_setxattr,
        // .getxattr = hello_ll_getxattr,
        // .removexattr = hello_ll_removexattr,
};
