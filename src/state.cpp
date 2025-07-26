#include "state.hpp"

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
using namespace SealFS;

SealFSLock::SealFSLock(){}

SealFSLock::SealFSLock(const std::filesystem::path& persistence_root)
    : lock_path_(persistence_root / ".sealfs.lock"){
    // TODO: See if I can move this elsewhere? Not a fan of this being done in the SealFSLock constructor...
    if(!std::filesystem::exists(persistence_root) || !std::filesystem::is_directory(persistence_root)){
        throw std::runtime_error(std::format("Path {} is not a valid directory", persistence_root.string()));
    }

    fd_ = open(lock_path_.c_str(), O_RDWR | O_CREAT, 0666);
    if(fd_ == -1){
        throw std::runtime_error("Failed to open lockfile: " + lock_path_.string());
    }

    if(lockf(fd_, F_TLOCK, 0) == -1){
        close(fd_);
        throw std::runtime_error("SealFS already mounted at: " + persistence_root.string());
    }
}

SealFSLock::~SealFSLock(){
    if(fd_ != -1){
        lockf(fd_, F_ULOCK, 0);
        close(fd_);
    }
}

SealFSLock::SealFSLock(SealFSLock&& oth){
    lock_path_ = std::move(oth.lock_path_);
    fd_ = std::move(oth.fd_);
    oth.lock_path_.clear();
    oth.fd_ = -1;
}

SealFSLock& SealFSLock::operator=(SealFSLock&& oth){
    if(this != &oth){
        lock_path_ = std::move(oth.lock_path_);
        fd_ = std::move(oth.fd_);
        oth.lock_path_.clear();
        oth.fd_ = -1;
    }
    return *this;
}

void SealFS::to_json(json& j, const inode_entry& inode){
    j = json{
        {"ino", inode.ino},
        {"parent", inode.parent},
        {"name", inode.name},
        {"type", inode.type},
        {"data_id", inode.data_id},
        {"st", {
            {"ino", inode.st.st_ino},
            {"nlink", inode.st.st_nlink},
            {"mode", inode.st.st_mode},
            {"uid", inode.st.st_uid},
            {"gid", inode.st.st_gid},
            {"size", inode.st.st_size},
            {"atime", inode.st.st_atime},
            {"mtime", inode.st.st_mtime},
            {"ctime", inode.st.st_ctime}
        }},
        {"children", inode.children}
    };
}

void SealFS::from_json(const json& j, inode_entry& inode){
    inode.ino = j.at("ino").get<fuse_ino_t>();
    inode.parent = j.at("parent").get<fuse_ino_t>();
    inode.name = j.at("name").get<std::string>();
    inode.type = j.at("type").get<sealfs_ino_t>();
    inode.data_id = j.at("data_id").get<uint32_t>();

    inode.st = {};
    inode.st.st_ino = j.at("st").at("ino").get<ino_t>();
    inode.st.st_nlink = j.at("st").at("nlink").get<nlink_t>();
    inode.st.st_mode = j.at("st").at("mode").get<mode_t>();
    inode.st.st_uid  = j.at("st").at("uid").get<uid_t>();
    inode.st.st_gid  = j.at("st").at("gid").get<gid_t>();
    inode.st.st_size = j.at("st").at("size").get<off_t>();
    inode.st.st_atime = j.at("st").at("atime").get<time_t>();
    inode.st.st_mtime = j.at("st").at("mtime").get<time_t>();
    inode.st.st_ctime = j.at("st").at("ctime").get<time_t>();

    if(j.contains("children") && !j.at("children").is_null()){
        inode.children = j.at("children").get<std::unordered_map<std::string, fuse_ino_t>>();
    }
    else{
        inode.children = std::nullopt;
    }
}


void SealFSData::validate_persistence_root(){
    std::filesystem::path structure_file = get_structure_path();
    std::filesystem::path data_dir = get_data_path();


    if(!std::filesystem::exists(structure_file)){
        std::ofstream(structure_file);
    }
    else if(!std::filesystem::is_regular_file(structure_file)){
        throw std::runtime_error(std::format("Structure file {} exists but is not a regular file", structure_file.string()));
    }

    if(!std::filesystem::exists(data_dir)){
        if(!std::filesystem::create_directory(data_dir)){
            throw std::runtime_error(std::format("Could not find or create data directory {}", data_dir.string()));
        }
    }
    else if(!std::filesystem::is_directory(data_dir)){
        throw std::runtime_error(std::format("Data directory {} exists but is not a directory", data_dir.string()));
    }

    std::regex valid_filename(R"(^\d+\.data$)");
    for(const auto& entry : std::filesystem::directory_iterator(data_dir)){
        std::string fname = entry.path().filename().string();
        if(!std::filesystem::is_regular_file(entry)){
            logger->warn("Non-file entry in data/: {}", fname);
        }
        if(!std::regex_match(fname, valid_filename)){
            logger->warn("Unexpected file in data/: {}", fname);
        }
    }
}

// TODO: At some point switch over to an atomic write
bool SealFSData::write_structure_to_disk(){
    try{
        nlohmann::json j = inodes;
        std::ofstream out(get_structure_path());
        out << j.dump(4);
        return true;
    }
    catch(const std::exception& e){
        logger->error("Failed to write structure.json: {}", e.what());
        return false;
    }
}

bool SealFSData::read_structure_from_disk(){
    try{
        std::ifstream in(get_structure_path());
        if(!in.is_open() || in.peek() == std::ifstream::traits_type::eof()){
            logger->warn("structure.json does not exist or is empty, initializing empty inodes");

            const auto root = create_inode_entry(SealFS::INVALID_INODE, "", SealFS::sealfs_ino_t::DIR, 0777);
            if(!root){
                logger->error("Failed to create root dir");
                return false;
            }
            return true;
        }
        nlohmann::json j;
        in >> j;
        inodes = j.get<std::unordered_map<fuse_ino_t, inode_entry>>();
        fuse_ino_t max_ino = 0;
        uint32_t max_data_id = 0;
        for(const auto &[ino, ent] : inodes){
            max_ino = std::max(max_ino, ino);
            max_data_id = std::max(max_data_id, ent.data_id);
        }
        next_ino = max_ino + 1;
        next_data_id = max_data_id + 1;
        return true;
    }
    catch(const std::exception& e){
        logger->error("Failed to read structure.json: {}", e.what());
        return false;
    }
}

SealFSData::SealFSData(const std::filesystem::path& path): persistence_root(path), plock(persistence_root){
    // TODO: Check whether this can take a std::filesystem::path directly?
    std::filesystem::path log_file = get_log_path();
    logger = spdlog::basic_logger_mt("SealFS Logger", log_file);
    logger->flush_on(spdlog::level::err);

    logger->info("Acquired lock on persistence root {}", persistence_root.string());

    validate_persistence_root();

    read_structure_from_disk();
}

SealFSData::SealFSData(){
    persistence_root = get_default_persistence_root();
    plock = SealFSLock(persistence_root);

    // TODO: Check whether this can take a std::filesystem::path directly?
    std::filesystem::path log_file = get_log_path();
    logger = spdlog::basic_logger_mt("SealFS Logger", log_file);
    logger->flush_on(spdlog::level::err);

    logger->info("Acquired lock on persistence root {}", persistence_root.string());

    validate_persistence_root();

    read_structure_from_disk();
}

SealFSData::~SealFSData(){
    write_structure_to_disk();

    logger->info("Releasing lock on persistence root {}", persistence_root.string());
    logger->flush();
}

fuse_ino_t SealFSData::get_parent(fuse_ino_t node){
    auto it = inodes.find(node);
    if(it == inodes.end()){
        logger->error("Failed to find inode {}", node);
        return -1;
    }
    return it->second.parent;
}

// Remove all references to this node, if data has 0 other refs, also delete corresponding data.
bool SealFSData::remove(fuse_ino_t node, sealfs_ino_t expected_type){
    auto entry = lookup_entry(node);
    if(!entry){
        return false;
    }

    auto& unwrapped_ent = entry.value().get();

    if(unwrapped_ent.type != expected_type){
        return false;
    }

    fuse_ino_t parent_ino = unwrapped_ent.parent;
    auto it = inodes.find(parent_ino);

    if(expected_type == sealfs_ino_t::DIR && unwrapped_ent.children.value().size() != 0){
        logger->warn("Failed to delete dir {} since it is nonempty", node);
        return false;
    }

    if(it != inodes.end()){
        auto& parent_map = it->second.children;
        if(parent_map){
            auto& unwrapped_pmap = parent_map.value();
            auto ino_it = unwrapped_pmap.find(unwrapped_ent.name);
            if(ino_it != unwrapped_pmap.end()){
                unwrapped_pmap.erase(ino_it);
            }
        }
    }

    if(unwrapped_ent.type == sealfs_ino_t::FILE){
        auto data_ent_path = get_data_ent_path(unwrapped_ent.data_id);
        bool wks = std::filesystem::remove(data_ent_path);
        logger->info("Status of removing data ent path for ino {} is {}", node, wks);
        inodes.erase(inodes.find(node));
        return wks;
    }
    else{
        inodes.erase(inodes.find(node));
        return true;
    }
}


const std::optional<std::reference_wrapper<std::unordered_map<std::string, fuse_ino_t>>> SealFSData::get_children(fuse_ino_t node){
    auto inode_entry = lookup_entry(node);
    if(!inode_entry){
        return std::nullopt;
    }

    auto& children = inode_entry.value().get().children;
    if(!children) return std::nullopt;
    else return children.value();
}

fuse_ino_t SealFSData::lookup(fuse_ino_t parent, const char* name){
    logger->info("[lookup] parent: {} name: {}", parent, name);

    if(parent == INVALID_INODE && strcmp(name, "")){
        if(strcmp(name, "")){
            // TODO: Do something better than hardcoding this
            return 1;
        }
        else return INVALID_INODE;
    }

    auto children = get_children(parent);
    if(!children.has_value()){
        logger->error("Inode {} has no children", parent);
        return INVALID_INODE;
    }
    auto unwrapped_children = children.value().get();
    auto it = unwrapped_children.find(name);
    if(it == unwrapped_children.end()){
        logger->error("Could not find child with name {} under inode {}", name, parent);
        return INVALID_INODE;
    }
    else return it->second;
}


const std::optional<std::reference_wrapper<inode_entry>> SealFSData::lookup_entry(fuse_ino_t parent, const char* name){
    logger->info("[lookup_entry] parent: {} name: {}", parent, name);
    fuse_ino_t cur_ino = lookup(parent, name);
    if(cur_ino == INVALID_INODE){
        logger->error("lookup(parent={}, name={}) returned INVALID_INODE", parent, name);
        return std::nullopt;
    }
    return lookup_entry(cur_ino);
}

const std::optional<std::reference_wrapper<inode_entry>> SealFSData::lookup_entry(fuse_ino_t cur_ino){
    logger->info("[lookup_entry] cur_ino: {}", cur_ino);

    auto it = inodes.find(cur_ino);
    if(it == inodes.end()){
        logger->error("Failed to find inode {}", cur_ino);
        return std::nullopt;
    }
    return it->second;
}

// TODO: Accept uid, gid
// Return nullopt iff parent has a child with same name already
std::optional<std::reference_wrapper<inode_entry>> SealFSData::create_inode_entry(fuse_ino_t parent, const char* name, sealfs_ino_t type, mode_t mode){

    logger->info("[create_inode_entry] parent: {} name: {} type: {} mode: {}", parent, name, static_cast<int>(type), mode);

    mode_t mask;
    const auto cur_ino = next_ino++;
    auto& cur_entry = inodes[cur_ino];

    if(parent != INVALID_INODE){
        auto children = get_children(parent);

        if(!children){
            logger->error("parent {} passed in is not directory", parent);
            return std::nullopt;
        }

        auto& cref = children.value().get();
        cref[name] = cur_ino;
    }

    cur_entry.ino = cur_entry.st.st_ino = cur_ino;
    cur_entry.parent = parent;
    cur_entry.type = type;

    if(type == sealfs_ino_t::FILE){
        cur_entry.st.st_size = 0;
        cur_entry.st.st_nlink = 1;
        cur_entry.children = std::nullopt;
        cur_entry.data_id = next_data_id++;
        mask = S_IFREG;

        auto filepath = get_data_ent_path(cur_entry.data_id);
        int fd = open(filepath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if(fd == -1){
            log_error("Failed to touch {}.data file on inode_entry creation", cur_ino);
        }
        else close(fd);
    }
    else{
        cur_entry.st.st_size = 4096;
        cur_entry.st.st_nlink = 2;
        cur_entry.children.emplace();
        mask = S_IFDIR;
    }

    cur_entry.name = strdup(name);

    time_t now = time(NULL);
    cur_entry.st.st_atime = now;
    cur_entry.st.st_mtime = now;
    cur_entry.st.st_ctime = now;

    // restrict to permission bits only
    cur_entry.st.st_mode = mask | (mode & 0777);

    logger->info("Successfully created inode {} with name {} and parent {}", cur_ino, name, parent);

    // what to do about uid/gid?
    return cur_entry;

}

// only possible on files
std::optional<std::reference_wrapper<inode_entry>> SealFSData::cow_inode_entry(fuse_ino_t parent, const char* name, mode_t mode, fuse_ino_t to_copy){
    logger->info("[cow_inode_entry] parent: {} name: {} mode: {} to_copy: {}", parent, name, mode, to_copy);

    mode_t mask;
    const auto cur_ino = next_ino++;
    auto& cur_entry = inodes[cur_ino];

    auto children = get_children(parent);

    if(!children){
        logger->error("parent {} passed in is not directory", parent);
        return std::nullopt;
    }

    auto& cref = children.value().get();
    cref[name] = cur_ino;

    const auto& copy_entry = inodes[to_copy];

    if(copy_entry.type != sealfs_ino_t::FILE){
        logger->error("ino to_copy {} passed in is not file", to_copy);
        return std::nullopt;
    }

    cur_entry.ino = cur_entry.st.st_ino = cur_ino;
    cur_entry.parent = parent;
    cur_entry.type = sealfs_ino_t::FILE;
    cur_entry.data_id = copy_entry.data_id;

    cur_entry.st.st_size = copy_entry.st.st_size;
    cur_entry.st.st_nlink = 1;
    cur_entry.children = std::nullopt;
    mask = S_IFREG;

    cur_entry.name = strdup(name);

    time_t now = time(NULL);
    cur_entry.st.st_atime = now;
    cur_entry.st.st_mtime = copy_entry.st.st_mtime;
    cur_entry.st.st_ctime = now;

    // restrict to permission bits only
    cur_entry.st.st_mode = mask | (mode & 0777);

    logger->info("Successfully copy-on-write of inode {} with name {} and parent {} copying to_copy {}", cur_ino, name, parent, to_copy);

    // what to do about uid/gid?
    return cur_entry;
}


// TODO: Maybe add check that it is not directory?
std::filesystem::path SealFSData::get_data_ent_path(uint32_t data_id){
    std::string filename = std::to_string(data_id) + ".data";
    return get_data_path() / filename;
}


DirBuf::DirBuf(fuse_req_t req): req(req), p(nullptr), size(0) {};

// Given a possibly existing buffer b of fuse_direntrys, pack in this new one with ino = ino, name = name
void DirBuf::add_entry(SealFS::SealFSData* fs, const char* name, fuse_ino_t ino){
    fs->log_info("Adding entry name: {} ino: {} to dirbuf", name, ino);
    size_t oldsize = size;
    // Figure out size of fuse_direntry required to pack. Note that only a fixed number of bits from stat are used, so we don't actually need to pass in the stat struct to get the correct size (name is the only entry of variable length)
    size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
    p = (char*) realloc(p, size);
    struct stat st;
    memset(&st, 0, sizeof(st));

    if(ino != INVALID_INODE){
        const auto& ent = fs->lookup_entry(ino);
        // TODO: add logging functionality directly into fs
        if(!ent){
            throw std::runtime_error("Could not find entry corresponding to ino");
        }
        memcpy(&st, &ent.value().get().st, sizeof(struct stat));
    }

    // Actually add the fuse_direntry corresponding to this (name, ino) to buffer b
    fuse_add_direntry(req, p + oldsize, size - oldsize, name, &st, size);
}

// Add buffer b to reply, respect offset (off) and maxsize for kernel pagination
int DirBuf::reply(off_t off, size_t maxsize){
    if(off < size){
        return fuse_reply_buf(req, p + off, std::min(size - off, maxsize));
    }
    else{
        return fuse_reply_buf(req, NULL, 0);
    }
}

DirBuf::~DirBuf(){
    free(p);
}
