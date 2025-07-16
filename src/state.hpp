#pragma once

#include "common.hpp"

#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <limits>
#include <vector>
#include <string>
#include <unordered_map>


namespace SealFS{
    // TODO: For now, make configurable, etc. later
    static const char* LOG_FILE = "~/sealfs.log";
    static const fuse_ino_t LEAF_OFFSET = std::numeric_limits<fuse_ino_t>::max() >> 1;

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


    fuse_ino_t next_dir_ino();

    fuse_ino_t next_file_ino();

    // Only return inode num
    fuse_ino_t lookup(fuse_ino_t parent, const char* name);

    // Return inode entry
    const inode_entry* lookup_entry(fuse_ino_t parent, const char* name);

    const inode_entry* lookup_entry(fuse_ino_t cur_ino);

    // Create file/dir in appropriate list and return ptr to inode_entry
    inode_entry* create_inode_entry(const char* name, sealfs_ino_t type, mode_t mode);

    void create_parent_mapping(const char* name, fuse_ino_t child, fuse_ino_t parent);

};

} // namespace SealFS
