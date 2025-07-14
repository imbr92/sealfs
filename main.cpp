// TODO: Double check used version
#define FUSE_USE_VERSION 34

#include <fuse3/fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <iostream>


static const struct fuse_lowlevel_ops hello_ll_oper = {
    /*
     * Basics:
     *  - Path traversal: lookup, getattr, readdir
     *  - Creating/Modifying files: create/mknod, mkdir, read, write
     *  - File Lifecycle: unlink, rmdir, rename
     *  - Open/Close flow: open, release
     *  - Optional but Useful: flush, fsync, init, destroy
     */
        // .lookup = hello_ll_lookup,
        // .getattr = hello_ll_getattr,
        // .readdir = hello_ll_readdir,
        // .open = hello_ll_open,
        // .read = hello_ll_read,
        // .setxattr = hello_ll_setxattr,
        // .getxattr = hello_ll_getxattr,
        // .removexattr = hello_ll_removexattr,
};

int main(){
    std::cout << "Hello World!";
    return 0;
}
