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
