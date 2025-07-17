#include "common.hpp"
#include "state.hpp"

#include <sys/stat.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#include <iostream>
#include <vector>
#include <format>
#include <unordered_map>

using namespace SealFS;

fuse_ino_t SealFSData::next_dir_ino(){
    return rel_next_dir_ino;
}

fuse_ino_t SealFSData::next_file_ino(){
    return rel_next_dir_ino + LEAF_OFFSET;
}

    // Only return inode num
fuse_ino_t SealFSData::lookup(fuse_ino_t parent, const char* name){
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
const inode_entry* SealFSData::lookup_entry(fuse_ino_t parent, const char* name){
    fuse_ino_t cur_ino = lookup(parent, name);
    std::cerr << std::format("[lookup_entry] parent: {} name: {}\n", parent, name);
    if(cur_ino == static_cast<fuse_ino_t>(-1)){
        // TODO: Add logging here
        std::cerr << std::format("\tlookup(parent={}, name={}) returned -1\n", parent, name);
        return nullptr;
    }
    return lookup_entry(cur_ino);
}

const inode_entry* SealFSData::lookup_entry(fuse_ino_t cur_ino){
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
inode_entry* SealFSData::create_inode_entry(const char* name, sealfs_ino_t type, mode_t mode){
    mode_t mask;
    inode_entry* cur_entry = nullptr;

    std::cerr << std::format("[create_inode_entry] name: {}, type: {}, mode: {}\n", name, static_cast<int>(type), mode);

    if(type == sealfs_ino_t::FILE){
        file_inodes.emplace_back();
        cur_entry = &file_inodes.back();
        cur_entry->ino = next_file_ino();
        cur_entry->st.st_ino = next_file_ino();
        rel_next_file_ino++;
        cur_entry->st.st_size = 0;
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
        cur_entry->st.st_size = 4096;
        cur_entry->st.st_nlink = 2;
        mask = S_IFDIR;
    }

    cur_entry->name = strdup(name);

    time_t now = time(NULL);
    cur_entry->st.st_atime = now;
    cur_entry->st.st_mtime = now;
    cur_entry->st.st_ctime = now;

    // restrict to permission bits only
    cur_entry->st.st_mode = mask | (mode & 0777);
    // what to do about uid/gid?
    return cur_entry;
}

void SealFSData::create_parent_mapping(const char* name, fuse_ino_t child, fuse_ino_t parent){
    std::cerr << std::format("[create_parent_mapping] name: {}, child: {}, parent: {}\n", name, child, parent);
    // TODO: Add error checking if name already exists, etc.?
    dirs[parent][name] = child;
}

const std::unordered_map<std::string, fuse_ino_t>& SealFSData::get_children(fuse_ino_t parent_ino){
    std::cerr << std::format("[get_children] parent_ino: {}\n", parent_ino);
    if(parent_ino < LEAF_OFFSET){
        if(parent_ino >= dir_inodes.size()){
            // TODO: Add logging here
            std::cerr << std::format("\tFailed to find dir inode with ino {}, dir_inodes.size() = {}\n", parent_ino, dir_inodes.size());
            throw std::exception();
        }
        return dirs[parent_ino];
    }
    else{
        std::cerr << std::format("\tExpected parent ino, received file ino\n");
        // TODO: Make this better
        throw std::exception();
    }
}

DirBuf::DirBuf(fuse_req_t req): req(req), p(nullptr), size(0) {};

// Given a possibly existing buffer b of fuse_direntrys, pack in this new one with ino = ino, name = name
void DirBuf::add_entry(SealFS::SealFSData* fs, const char* name, fuse_ino_t ino){
    size_t oldsize = size;
    // Figure out size of fuse_direntry required to pack. Note that only a fixed number of bits from stat are used, so we don't actually need to pass in the stat struct to get the correct size (name is the only entry of variable length)
    size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
    p = (char*) realloc(p, size);
    const SealFS::inode_entry* ent = fs->lookup_entry(ino);

    // Actually add the fuse_direntry corresponding to this (name, ino) to buffer b
    fuse_add_direntry(req, p + oldsize, size - oldsize, name, &ent->st, size);
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

