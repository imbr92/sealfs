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

#include <filesystem>
#include <regex>
#include <iostream>
#include <fstream>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace SealFS{

static inline const std::filesystem::path expand_user_path(const std::string& path){
    if(!path.empty() && path[0] == '~'){
        const char* home = std::getenv("HOME");
        if(home){
            return std::filesystem::path(home) / path.substr(2);
        }
    }
    return std::filesystem::path(path);
}

inline const std::filesystem::path& get_default_persistence_root() {
    // TODO: For now, make configurable, etc. later
    static const std::filesystem::path root = expand_user_path("~/sealfs");
    return root;
}

static constexpr fuse_ino_t INVALID_INODE = static_cast<fuse_ino_t>(-1);

// RAII-style persistence root lock to ensure that a fs is not mounted in multiple places at once
class SealFSLock{
private:
    std::filesystem::path lock_path_;
    int fd_ = -1;
public:

    SealFSLock();
    SealFSLock(SealFSLock&& oth);
    SealFSLock& operator=(SealFSLock&& oth);
    SealFSLock(const std::filesystem::path& persistence_root);
    ~SealFSLock();

    SealFSLock(const SealFSLock&) = delete;
    SealFSLock& operator=(const SealFSLock&) = delete;
};

enum class sealfs_ino_t { FILE, DIR };

struct inode_entry{
    // TODO: Probably not even needed separately if its stored in stat already...
    fuse_ino_t ino;
    fuse_ino_t parent;
    // TODO: Maybe remove
    std::string name;

    // TODO: Maybe remove, S_ISDIR(st.st_mode) and S_ISREG(st.st_mode) can do the same thing
    sealfs_ino_t type;
    struct stat st;

    std::optional<std::unordered_map<std::string, fuse_ino_t>> children;
};

void to_json(json& j, const inode_entry& inode);
void from_json(const json& j, inode_entry& inode);


class SealFSData{
private:
    fuse_ino_t next_ino = 1;
    SealFSLock plock;
    std::filesystem::path persistence_root;
    std::shared_ptr<spdlog::logger> logger;
    std::unordered_map<fuse_ino_t, inode_entry> inodes;

    inline std::filesystem::path get_log_path(){
        return persistence_root / "sealfs.log";
    }

    inline std::filesystem::path get_structure_path(){
        return persistence_root / "structure.json";
    }

    inline std::filesystem::path get_data_path(){
        return persistence_root / "data";
    }

    void validate_persistence_root();
    // TODO: At some point switch over to an atomic write
    bool write_structure_to_disk();
    bool read_structure_from_disk();

public:
    SealFSData();
    SealFSData(const std::filesystem::path& path);
    ~SealFSData();


    fuse_ino_t get_parent(fuse_ino_t node);

    // std::optional<std::reference_wrapper<T>> since non-owning nullable reference + don't want to pass around raw ptrs
    const std::optional<std::reference_wrapper<std::unordered_map<std::string, fuse_ino_t>>> get_children(fuse_ino_t node);

    // TODO: Maybe string_view this?
    fuse_ino_t lookup(fuse_ino_t parent, const char* name);
    const std::optional<std::reference_wrapper<inode_entry>> lookup_entry(fuse_ino_t parent, const char* name);
    const std::optional<std::reference_wrapper<inode_entry>> lookup_entry(fuse_ino_t cur_ino);
    // Return nullopt iff parent has a child with same name already
    std::optional<std::reference_wrapper<inode_entry>> create_inode_entry(fuse_ino_t parent, const char* name, sealfs_ino_t type, mode_t mode);

    // TODO: Replace all internal logger-> calls with calls to these
    template<typename... Args>
    void log_info(fmt::format_string<Args...> fmt, Args&&... args){
        logger->info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_warn(fmt::format_string<Args...> fmt, Args&&... args){
        logger->warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_debug(fmt::format_string<Args...> fmt, Args&&... args){
        logger->debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void log_error(fmt::format_string<Args...> fmt, Args&&... args){
        logger->error(fmt, std::forward<Args>(args)...);
    }

};

// Wrapper for buffer containing all fuse_direntrys (not a real struct, conceptual notion)
class DirBuf{
private:
    fuse_req_t req;
    char *p; // Each fuse_direntry is packed into p
    size_t size; // Size of p (all packed fuse_direntrys so far)

public:
    DirBuf(fuse_req_t req);
    ~DirBuf();

    void add_entry(SealFS::SealFSData* fs, const char* name, fuse_ino_t ino);
    int reply(off_t off, size_t maxsize);
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
} // namespace SealFS
