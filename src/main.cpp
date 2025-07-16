// TODO: Double check used version
#define FUSE_USE_VERSION 34

#include "state.hpp"

#include <fuse3/fuse_lowlevel.h>
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

static void sealfs_init(void* userdata, struct fuse_conn_info *conn){
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

static void sealfs_lookup(fuse_req_t req, fuse_ino_t parent, const char* name){
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

static void sealfs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
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


	/**
	 * Look up a directory entry by name and get its attributes.
	 *
	 * Valid replies:
	 *   fuse_reply_entry
	 *   fuse_reply_err
	 *
	 * @param req request handle
	 * @param parent inode number of the parent directory
	 * @param name the name to look up
	 */

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

int main(int argc, char* argv[]){
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    struct fuse_session *se;
    struct fuse_cmdline_opts opts;
    struct fuse_loop_config config;
    int ret = -1;

    if(fuse_parse_cmdline(&args, &opts) != 0){
        return 1;
    }

    SealFS::SealFSData* fs = new SealFS::SealFSData();

    if(opts.show_help){
        printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
        fuse_cmdline_help();
        fuse_lowlevel_help();
        ret = 0;
        goto err_out1;
    }
    else if(opts.show_version){
        printf("FUSE library version %s\n", fuse_pkgversion());
        fuse_lowlevel_version();
        ret = 0;
        goto err_out1;
    }

    if(opts.mountpoint == NULL) {
        printf("usage: %s [options] <mountpoint>\n", argv[0]);
        printf("       %s --help\n", argv[0]);
        ret = 1;
        goto err_out1;
    }


    se = fuse_session_new(&args, &sealfs_oper, sizeof(sealfs_oper), fs);
    if(se == NULL){
        goto err_out1;
    }
    if(fuse_set_signal_handlers(se) != 0){
        goto err_out2;
    }
    if(fuse_session_mount(se, opts.mountpoint) != 0){
        goto err_out3;
    }

    fuse_daemonize(opts.foreground);

    /* Block until ctrl+c or fusermount -u */
    if(opts.singlethread){
        ret = fuse_session_loop(se);
    }
    else{
        config.clone_fd = opts.clone_fd;
        config.max_idle_threads = opts.max_idle_threads;
        ret = fuse_session_loop_mt(se, &config);
    }

        fuse_session_unmount(se);
err_out3:
        fuse_remove_signal_handlers(se);
err_out2:
        fuse_session_destroy(se);
err_out1:
        free(opts.mountpoint);
        fuse_opt_free_args(&args);
        delete fs;

        return ret ? 1 : 0;
}
