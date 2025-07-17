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
    std::cerr << std::format("[sealfs_init]\n");

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(userdata);

    std::cerr << std::format("\tuserdata successfully casted to SealFSData with size: {}\n", sizeof(*fs));

    SealFS:: inode_entry* root = fs->create_inode_entry("", SealFS::sealfs_ino_t::DIR, 0777);

    std::cerr << std::format("\tRoot dir sucessfully added\n");

    {
        char* name = "hello.txt";
        SealFS::inode_entry* ret = fs->create_inode_entry(name, SealFS::sealfs_ino_t::FILE, 0777);
        fs->create_parent_mapping(name, ret->ino, root->ino);
    }

    std::cerr << std::format("\thello.txt file sucessfully added\n");

    {
        char* name = "hello2.txt";
        SealFS::inode_entry* ret = fs->create_inode_entry(name, SealFS::sealfs_ino_t::FILE, 0777);
        fs->create_parent_mapping(name, ret->ino, root->ino);
    }

    std::cerr << std::format("\thello2.txt file sucessfully added\n");
}

void sealfs_lookup(fuse_req_t req, fuse_ino_t parent, const char* name){
    std::cerr << std::format("[sealfs_lookup] parent: {}, name: {}\n", parent, name);
    struct fuse_entry_param e;

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));

    // std::cerr << "Next Dir Ino: " << fs->next_dir_ino() << '\n';

    memset(&e, 0, sizeof(e));

    const SealFS::inode_entry* ret = fs->lookup_entry(parent, name);

    if(ret == nullptr){
        std::cerr << std::format("\tret is nullptr\n");
        fuse_reply_err(req, ENOENT);
    }
    else{
        e.ino = ret->ino;
        // TODO: Maybe do something else for timeouts?
        e.attr_timeout = 1.0;
        e.entry_timeout = 1.0;

        e.attr = ret->st;
        std::cerr << std::format("\tret fields are name: {} ino: {} st_ino: {}\n", ret->name, ret->ino, ret->st.st_ino);

        fuse_reply_entry(req, &e);
    }
}

void sealfs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
    (void) fi;
    std::cerr << std::format("[sealfs_getattr] ino: {}\n", ino);

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    const SealFS::inode_entry* ret = fs->lookup_entry(ino);
    if(ret == nullptr){
        std::cerr << std::format("\tret is nullptr\n");
        fuse_reply_err(req, ENOENT);
    }
    else{
        // TODO: Maybe do something else for timeouts?
        const double attr_timeout = 1.0;
        std::cerr << std::format("\tret fields are name: {} ino: {} st_ino: {}\n", ret->name, ret->ino, ret->st.st_ino);

        fuse_reply_attr(req, &ret->st, attr_timeout);
    }
}

void sealfs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi){
    std::cerr << std::format("[sealfs_opendir] ino: {}\n", ino);

    // Optionally store a file handle here if you want to track dir state
    fi->fh = 0;

    fuse_reply_open(req, fi);
}

// Wrapper for buffer containing all fuse_direntrys (not a real struct, conceptual notion)
struct dirbuf {
    char *p; // Each fuse_direntry is packed into p
    size_t size; // Size of p (all packed fuse_direntrys so far)
};

// Given a possibly existing buffer b of fuse_direntrys, pack in this new one with ino = ino, name = name
void dirbuf_add(SealFS::SealFSData* fs, fuse_req_t req, struct dirbuf* b, const char* name, fuse_ino_t ino){
    size_t oldsize = b->size;
    // Figure out size of fuse_direntry required to pack. Note that only a fixed number of bits from stat are used, so we don't actually need to pass in the stat struct to get the correct size (name is the only entry of variable length)
    b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
    b->p = (char*) realloc(b->p, b->size);
    const SealFS::inode_entry* ent = fs->lookup_entry(ino);

    // Actually add the fuse_direntry corresponding to this (name, ino) to buffer b
    fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &ent->st, b->size);
}

// Add buffer b to reply, respect offset (off) and maxsize for kernel pagination
int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize, off_t off, size_t maxsize){
    if(off < bufsize){
        return fuse_reply_buf(req, buf + off, std::min(bufsize - off, maxsize));
    }
    else{
        return fuse_reply_buf(req, NULL, 0);
    }
}

void sealfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
    // TODO: Maybe use? If needed might need to implement opendir
    (void) fi;

    std::cerr << std::format("[sealfs_readdir] ino: {} size: {} off: {}\n", ino, size, off);

    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));

    SealFS::DirBuf buf(req);
    buf.add_entry(fs, ".", ino);
    // TODO: use parent's ino instead of ino here
    buf.add_entry(fs, "..", ino);

    auto children = fs->get_children(ino);
    for(const auto &[name, child_ino] : children){
        buf.add_entry(fs, name.c_str(), child_ino);
    }

    buf.reply(off, size);
}

const struct fuse_lowlevel_ops sealfs_oper = {
    .init = sealfs_init,
    .lookup = sealfs_lookup,
    .getattr = sealfs_getattr,

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

