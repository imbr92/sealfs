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


    // TODO: delete at some point...
    if(!fs->is_initialized()){
        std::cerr << "Not initialized\n";
        const auto root = fs->create_inode_entry(SealFS::INVALID_INODE, "", SealFS::sealfs_ino_t::DIR, 0777);
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
    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_open] ino: {}", ino);

    const auto c_ent = fs->lookup_entry(ino);

    // TODO: Maybe refactor/split out safe unwrap elsewhere?
    if(!c_ent){
        fs->log_error("Could not find corresponding inode_entry for ino: {}", ino);
        fuse_reply_err(req, EINVAL);
        return;
    }
    const auto& unwrapped_ent = c_ent.value().get();

    // Contains user uid and gid
    // We only check primary gid, secondary gid is not easily accessible via ctx
    const struct fuse_ctx *ctx = fuse_req_ctx(req);

    // TODO: refactor
    auto check_read_perms = [](uid_t ctx_uid, gid_t ctx_gid, uid_t file_uid, gid_t file_gid, mode_t file_perms) -> bool{
        if (ctx_uid == 0) return true;
        bool allowed = false;
        if(ctx_uid == file_uid){
            allowed = allowed || (file_perms & S_IRUSR);
        }
        if(ctx_gid == file_gid){
            allowed = allowed || (file_perms & S_IRGRP);
        }
        allowed = allowed || (file_perms & S_IROTH);
        return allowed;
    };

    auto check_write_perms = [](uid_t ctx_uid, gid_t ctx_gid, uid_t file_uid, gid_t file_gid, mode_t file_perms) -> bool{
        if (ctx_uid == 0) return true;
        bool allowed = false;
        if(ctx_uid == file_uid){
            allowed = allowed || (file_perms & S_IWUSR);
        }
        if(ctx_gid == file_gid){
            allowed = allowed || (file_perms & S_IWGRP);
        }
        allowed = allowed || (file_perms & S_IWOTH);
        return allowed;
    };

    bool read_allowed = check_read_perms(
        ctx->uid,
        ctx->gid,
        unwrapped_ent.st.st_uid,
        unwrapped_ent.st.st_gid,
        unwrapped_ent.st.st_mode
    );

    bool write_allowed = check_write_perms(
        ctx->uid,
        ctx->gid,
        unwrapped_ent.st.st_uid,
        unwrapped_ent.st.st_gid,
        unwrapped_ent.st.st_mode
    );

    auto reply = [&](bool access){
        if(access){
            auto filepath = fs->get_data_ent_path(unwrapped_ent.data_id);
            int fd = open(filepath.c_str(), fi->flags);
            if(fd == -1){
                fs->log_error("Failed to get fd for file {} with ino {}", filepath.c_str(), ino);
                fuse_reply_err(req, errno);
                return;
            }
            // TODO: make sure to delete and call close on fd when calling release()
            SealFS::FileHandle* h = new SealFS::FileHandle{fd};
            fi->fh = reinterpret_cast<uint64_t>(h);
            fs->log_info("Successfully opened file {} with ino: {}", filepath.c_str(), ino);
            fuse_reply_open(req, fi);
        }
        else{
            fs->log_error("User does not have sufficient access to open ino {}", ino);
            fuse_reply_err(req, EACCES);
        }
    };

    int32_t accmode = fi->flags & O_ACCMODE;
    if(accmode == O_WRONLY){
        reply(write_allowed);
    }
    else if(accmode == O_RDONLY){
        reply(read_allowed);
    }
    else if(accmode == O_RDWR){
        reply(read_allowed && write_allowed);
    }
    else{
        fs->log_error("User is requesting unknown access type");
        fuse_reply_err(req, EINVAL);
    }
}


// Currently does not support direct_io
void sealfs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi){
    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_read] ino: {} size: {} off: {}", ino, size, off);

    SealFS::FileHandle *f = reinterpret_cast<SealFS::FileHandle*>(fi->fh);

    // TODO: Maybe cap size to MAX_READ_SIZE

    std::unique_ptr<char[]> buf = std::make_unique<char[]>(size);
    ssize_t bytes = pread(f->fd, buf.get(), size, off);
    if(bytes == -1){
        fuse_reply_err(req, errno);
        return;
    }

    fuse_reply_buf(req, buf.get(), bytes);
}

void sealfs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi){
    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_release] ino: {}", ino);

    SealFS::FileHandle* hptr = reinterpret_cast<SealFS::FileHandle*>(fi->fh);
    delete hptr;

    fuse_reply_err(req, 0);
}

void sealfs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi){
    SealFS::SealFSData* fs = static_cast<SealFS::SealFSData*>(fuse_req_userdata(req));
    fs->log_info("[sealfs_write] ino: {} size: {} off: {}", ino, size, off);

    SealFS::FileHandle *f = reinterpret_cast<SealFS::FileHandle*>(fi->fh);

    ssize_t bytes = pwrite(f->fd, buf, size, off);
    if(bytes == -1){
        fuse_reply_err(req, errno);
        return;
    }
    const auto& cur_ent = fs->lookup_entry(ino);

    if(!cur_ent){
        fs->log_info("Could not find inode_entry corresponding to ino: {}", ino);
        fuse_reply_err(req, errno);
        return;
    }

    auto& unwrapped_ent = cur_ent.value().get();
    unwrapped_ent.st.st_size += bytes;

    fuse_reply_write(req, bytes);
}





const struct fuse_lowlevel_ops sealfs_oper = {
    .init = sealfs_init,
    .lookup = sealfs_lookup,
    .getattr = sealfs_getattr,

    // TODO: Figure out why I am warned to put this before open/readdir
    .open = sealfs_open,
    .read = sealfs_read,

    .write = sealfs_write,

    .release = sealfs_release,

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
};
