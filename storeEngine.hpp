#pragma once
#include "log.hpp"
#include "memTable.hpp"
#include "WAL.hpp"
#include "SSTable.hpp"
#include "compaction.hpp"


#include <sstream>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>

/*
程序内存
    ↓ write()
操作系统的文件缓存（仍在内存中）
    ↓ 系统稍后写回，或主动 fsync()
持久化存储

所以，“数据已经写到 SSTable 或 WAL”可能只是两份数据都进入了操作系统缓存。
掉电时，这两份缓存可以一起丢失。

关于 sync()，这里的“同步”指的是把文件在操作系统缓存中的修改同步到持久化存储
*/
struct PendingTable {
    std::unique_ptr<SSTable> table;
    std::filesystem::path temporary_path;
    std::filesystem::path final_path;
};

struct TableHandle{
    std::filesystem::path path;
    std::unique_ptr<SSTable> table;
};

class StoreEngine{
using EntryMap = std::map<std::string, Entry, std::less<>>;

    public:
    explicit StoreEngine(const std::filesystem::path& dataDir){
       if (!open(dataDir.string())) {
       throw std::runtime_error("StoreEngine initialization failed");
       }
    }

    bool put(std::string key, std::string value);
    std::optional<std::string> get(std::string_view key);
    bool remove(std::string_view key);
    PendingTable createNewTable();
    bool recover();

    //flush是把memtable写入磁盘
    bool flush();
    //writeFile是把任意SSTable一个文件写入磁盘
    bool writeFile(const EntryMap& output);


    std::vector<std::filesystem::path> loadTables(std::string& dir);
     std::string formatId(uint64_t id);

    bool open(const std::string& dir);


    //compaction合并函数
    bool merge(
    const std::vector<std::unique_ptr<SSTable>>& tables,
    EntryMap& output
    );

    bool compacte();

    private:
    MemTable memtable_;
    std::unique_ptr<WALLog> wal_;
    std::vector<TableHandle> sstables_;
    Compaction compaction;
    
    
    std::filesystem::path data_dir_;//类似test-data
    std::filesystem::path table_dir_;//类似test-data/sstables
    std::uint64_t next_file_id_ = 0;
    
    //目录：保存了“文件名 → 文件对象”的关联，这用来持久化同步目录还有对应的文件名关系
    bool syncDirectory(const std::filesystem::path& path);

    //存储没有compaction处理的旧目录
    std::vector<std::filesystem::path> oldpaths;

    /*
    <data_dir>/
       ├── active.wal
       └── sstables/
       ├── sst_0000000000.sst
       └── sst_0000000001.sst.tmp
    */



};