// TODO: Double check used version
#define FUSE_USE_VERSION 34

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

#include <limits>
#include <iostream>
#include <vector>
#include <format>
#include <unordered_map>

// TODO: For now, make configurable, etc. later
const char* LOG_FILE = "~/sealfs.log";
const fuse_ino_t LEAF_OFFSET = std::numeric_limits<fuse_ino_t>::max() >> 1;

namespace SealFS{

enum class sealfs_ino_t { FILE, DIR };


struct inode_entry {
    // TODO: Probably not even needed separately if its stored in stat already...
    fuse_ino_t ino;
    struct stat st;

    char* name;
    sealfs_ino_t type;

    // Probably not needed..., don't plan on this being fully in memory
    // void *data;
    // size_t data_len;
};

struct SealFSData{
private:
    fuse_ino_t rel_next_dir_ino = 1, rel_next_file_ino = 2;
    std::vector<inode_entry> file_inodes{2}, dir_inodes{1};
    // TODO: Consider std::string_view + external std::string storage for lookups to not require std::string creation on each call
    std::vector<std::unordered_map<std::string, fuse_ino_t>> dirs{1};
public:


    inline fuse_ino_t next_dir_ino(){
        return rel_next_dir_ino;
    }

    inline fuse_ino_t next_file_ino(){
        return rel_next_dir_ino + LEAF_OFFSET;
    }

    // Only return inode num
    inline fuse_ino_t lookup(fuse_ino_t parent, const char* name){
        std::cerr << std::format("[lookup] parent: {} name: {}\n", parent, name);
        if(dir_inodes.size() <= parent){
            // TODO: Add logging here
            std::cerr << std::format("\tCould not find parent: {}, dir_inodes.size(): {}\n", parent, dir_inodes.size());
            return -1;
        }
        const auto& cdir = dirs[parent];
        auto it = cdir.find(name);
        if(it == cdir.end()){
            // TODO: Add logging here
            std::cerr << std::format("\tCould not find name: {} under parent: {}\n", name, parent);
            return -1;
        }
        return it->second;
    }

    // Return inode entry
    inline const inode_entry* lookup_entry(fuse_ino_t parent, const char* name){
        fuse_ino_t cur_ino = lookup(parent, name);
        std::cerr << std::format("[lookup_entry] parent: {} name: {}\n", parent, name);
        if(cur_ino == static_cast<fuse_ino_t>(-1)){
            // TODO: Add logging here
            std::cerr << std::format("\tlookup(parent={}, name={}) returned -1\n", parent, name);
            return nullptr;
        }
        return lookup_entry(cur_ino);
    }

    inline const inode_entry* lookup_entry(fuse_ino_t cur_ino){
        std::cerr << std::format("[lookup_entry] cur_ino: {}\n", cur_ino);
        if(cur_ino < LEAF_OFFSET){
            if(cur_ino >= dir_inodes.size()){
                // TODO: Add logging here
                std::cerr << std::format("\tFailed to find dir inode with ino {}, dir_inodes.size() = {}\n", cur_ino, dir_inodes.size());
                return nullptr;
            }
            return &dir_inodes[cur_ino];
        }
        else{
            cur_ino -= LEAF_OFFSET;
            if(cur_ino >= file_inodes.size()){
                // TODO: Add logging here
                std::cerr << std::format("\tFailed to find file inode with ino {}, file_inodes.size() = {}\n", cur_ino, file_inodes.size());
                return nullptr;
            }
            return &file_inodes[cur_ino];
        }
    }

    // Create file/dir in appropriate list and return ptr to inode_entry
    inline inode_entry* create_inode_entry(const char* name, sealfs_ino_t type, mode_t mode){
        mode_t mask;
        inode_entry* cur_entry = nullptr;

        std::cerr << std::format("[create_inode_entry] name: {}, type: {}, mode: {}\n", name, static_cast<int>(type), mode);

        if(type == sealfs_ino_t::FILE){
            file_inodes.emplace_back();
            cur_entry = &file_inodes.back();
            cur_entry->ino = next_file_ino();
            cur_entry->st.st_ino = next_file_ino();
            rel_next_file_ino++;
            cur_entry->st.st_nlink = 1;
            mask = S_IFREG;
        }
        else{
            dirs.emplace_back();
            dir_inodes.emplace_back();
            cur_entry = &dir_inodes.back();
            cur_entry->ino = next_dir_ino();
            cur_entry->st.st_ino = next_dir_ino();
            rel_next_dir_ino++;
            cur_entry->st.st_nlink = 2;
            mask = S_IFDIR;
        }

        cur_entry->name = strdup(name);

        time_t now = time(NULL);
        cur_entry->st.st_atime = now;
        cur_entry->st.st_mtime = now;
        cur_entry->st.st_ctime = now;

        cur_entry->st.st_size = 0;
        // restrict to permission bits only
        cur_entry->st.st_mode = mask | (mode & 0777);
        // what to do about uid/gid?
        return cur_entry;
    }

    inline void create_parent_mapping(const char* name, fuse_ino_t child, fuse_ino_t parent){
        std::cerr << std::format("[create_parent_mapping] name: {}, child: {}, parent: {}\n", name, child, parent);
        // TODO: Add error checking if name already exists, etc.?
        dirs[parent][name] = child;
    }

};

} // namespace SealFS

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
        std::cerr << ret->st.st_ino << '\n';
        std::cerr << ret->name << '\n';
        std::cerr << ret->ino << '\n';

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

    // TODO: Maybe do something else for timeouts?
    const double attr_timeout = 1.0;

    std::cerr << ret->st.st_ino << '\n';
    std::cerr << ret->name << '\n';
    std::cerr << ret->ino << '\n';
    fuse_reply_attr(req, &ret->st, attr_timeout);
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
