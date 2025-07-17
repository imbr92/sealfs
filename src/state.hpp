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

    // Require parent_ino to be a dir
    const std::unordered_map<std::string, fuse_ino_t>& get_children(fuse_ino_t parent_ino);

    // Create file/dir in appropriate list and return ptr to inode_entry
    inode_entry* create_inode_entry(const char* name, sealfs_ino_t type, mode_t mode);

    void create_parent_mapping(const char* name, fuse_ino_t child, fuse_ino_t parent);
};

// Wrapper for buffer containing all fuse_direntrys (not a real struct, conceptual notion)
class DirBuf{
private:
    fuse_req_t req;
    char *p; // Each fuse_direntry is packed into p
    size_t size; // Size of p (all packed fuse_direntrys so far)

public:
    DirBuf(fuse_req_t req);

    void add_entry(SealFS::SealFSData* fs, const char* name, fuse_ino_t ino);

    int reply(off_t off, size_t maxsize);

    ~DirBuf();
};

// Want backing structure to be fairly modular/easy to plug and play
//  - Everything should just be in SealFSData...
//  - For now:
//      - In-memory directory structure, implement import/export via protobuf to metadata file? (or nlohmann::json for easier debugging for now?)
//          - cons(path):
//              - Set dir_path and dir_name based on this
//              - Warn that it is UB to mount this fs in 2 spots at once
//                  - Maybe lockfile or something?
//              - Read from given path to get in-memory directory structure
//              - Check validity of directory
//                  - All inodes mentioned in directory structure should exist, or we create, mark empty and log warning/error
//                  - Log warning/error for inodes that have data but are not present in the in-memory fs
//          - cons():
//              - Set dir_path, dir_name manually, use uuid to generate fs name
//          - dest():
//              - Export in-memory directory structure to metadata file
//      - Flat data in filesystem <inode number>.data -> data for file corresponding to this inode
//      - get_parent(fuse_ino_t)
//          - Figure out what to do when we are at root of fs
//      - const std::unordered_map<std::string, fuse_ino_t> get_children(fuse_ino_t dir)
//      - const inode_entry* lookup_entry(fuse_ino_t parent, char* name)
//      - const inode_entry* lookup_entry(fuse_ino_t ino)
//      - fuse_ino_t lookup(fuse_ino_t parent, char* name)
//      - inode_entry* create_inode_entry(fuse_ino_t parent, const char* name, sealfs_ino_t type, mode_t mode);
//          - TODO: figure out return type
//          - Combine create_inode_entry and create_parent_mapping into single function

//
// Structure:
//  - dir_path = Path to backing dir
//  - dir_name = Name of backing dir (fs_<uuid of this mount>)
//  - inode_no -> inode_entry object map, for now just do everything in map (no vector/whatever I was doing before)
//      - inode_entry contains
//          - FILE vs. DIR
//              // name to fuse_ino_t mapping for children
//          - std::unordered_map<std::string, fuse_ino_t> children
//          - stat info
//          - fuse_ino_t parent
//          - std::string name
// class FSBacking{
// private:
//     std::string dir_path;
//     std::string dir_name;
//
//     // Names of files to 
//     std::unordered_map<fuse_ino_t, std::string> filenames;
//
// public:
//     void get_path();
//
// };

} // namespace SealFS
