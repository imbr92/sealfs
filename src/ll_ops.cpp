#include "common.hpp"
#include "state.hpp"
#include "ll_ops.hpp"

#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <iostream>
#include <format>

void sealfs_init(void* userdata, struct fuse_conn_info *conn){
    (void) conn;

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(userdata);
    fs->log_info("[sealfs_init]");

    const auto root = fs->create_inode_entry(-1, "", SealFS::sealfs_ino_t::DIR, 0777);
    if(!root){
        fs->log_error("Failed to create root dir");
        return;
    }

    fs->log_info("Added root dir");


    {
        const char* name = "hello.txt";
        fs->create_inode_entry(root.value().get().ino, name, SealFS::sealfs_ino_t::FILE, 0777);
        fs->log_info("Added hello.txt");
    }

    {
        const char* name = "hello2.txt";
        fs->create_inode_entry(root.value().get().ino, name, SealFS::sealfs_ino_t::FILE, 0777);
        fs->log_info("Added hello2.txt");
    }
}

void sealfs_lookup(fuse_req_t req, fuse_ino_t parent, const char* name){
    struct fuse_entry_param e;

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_lookup] parent: {} name: {}", parent, name);

    memset(&e, 0, sizeof(e));

    const auto ret = fs->lookup_entry(parent, name);

    if(!ret){
        fs->log_error("ret is nullptr");
        fuse_reply_err(req, ENOENT);
    }
    else{
        auto& unwrapped_entry = ret.value().get();
        e.ino = unwrapped_entry.ino;
        // TODO: Maybe do something else for timeouts?
        e.attr_timeout = 1.0;
        e.entry_timeout = 1.0;

        e.attr = unwrapped_entry.st;
        fs->log_info("ret fields are name: {} ino: {} st_ino: {}", unwrapped_entry.name, unwrapped_entry.ino, unwrapped_entry.st.st_ino);

        fuse_reply_entry(req, &e);
    }
}

void sealfs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
    (void) fi;

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_getattr] ino: {}", ino);

    const auto ret = fs->lookup_entry(ino);
    if(!ret){
        fs->log_error("ret is nullptr");
        fuse_reply_err(req, ENOENT);
    }
    else{
        // TODO: Maybe do something else for timeouts?
        const double attr_timeout = 1.0;
        auto& unwrapped_entry = ret.value().get();
        fs->log_info("ret fields are name: {} ino: {} st_ino: {}", unwrapped_entry.name, unwrapped_entry.ino, unwrapped_entry.st.st_ino);

        fuse_reply_attr(req, &unwrapped_entry.st, attr_timeout);
    }
}

void sealfs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi){
    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_opendir] ino: {}", ino);

    // TODO: Maybe store file handle here?
    fi->fh = 0;

    fuse_reply_open(req, fi);
}

void sealfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
    // TODO: Maybe use? If needed might need to implement opendir
    (void) fi;

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_readdir] ino: {} size: {} off: {}", ino, size, off);

    SealFS::DirBuf buf(req);
    buf.add_entry(fs, ".", ino);
    buf.add_entry(fs, "..", fs->get_parent(ino));

    auto children = fs->get_children(ino);

    if(!children){
        fs->log_error("failed to get children of ino {}. Likely not a directory", ino);
        fuse_reply_err(req, ENOTDIR);
        return;
    }

    for(const auto &[name, child_ino] : children.value().get()){
        buf.add_entry(fs, name.c_str(), child_ino);
    }

    buf.reply(off, size);
}

void sealfs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
	 //    * Valid replies:
	 // *   fuse_reply_open
	 // *   fuse_reply_err
	 // *
	 // * @param req request handle
	 // * @param ino the inode number
	 // * @param fi file information


}


const struct fuse_lowlevel_ops sealfs_oper = {
    .init = sealfs_init,
    .lookup = sealfs_lookup,
    .getattr = sealfs_getattr,

    // TODO: Figure out why I am warned to put this before open/readdir
    .open = sealfs_open,

    .opendir = sealfs_opendir,
    .readdir = sealfs_readdir,

    /*
     * Basics:
     *  - Path traversal: lookup, getattr, readdir
     *  - Creating/Modifying files: create/mknod, mkdir, read, write
     *  - File Lifecycle: unlink, rmdir, rename
     *  - Open/Close flow: open, release
     *  - Optional but Useful: flush, fsync, init, destroy
     */
    // .fuse_lowlevel_init = stuff
        // .open = hello_ll_open,
        // .read = hello_ll_read,
        // .setxattr = hello_ll_setxattr,
        // .getxattr = hello_ll_getxattr,
        // .removexattr = hello_ll_removexattr,
};
