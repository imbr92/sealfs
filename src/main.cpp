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
    // fs->set_initialized(false);
    fs->set_initialized(true);

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
