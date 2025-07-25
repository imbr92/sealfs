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

void sealfs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);

void sealfs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* fi);

void sealfs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

void sealfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);

void sealfs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);

void sealfs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi);




extern const struct fuse_lowlevel_ops sealfs_oper;
