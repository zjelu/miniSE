#pragma once
#include "log.hpp"
#include "memTable.hpp"
#include "WAL.hpp"
#include "SSTable.hpp"
#include "compaction.hpp"
#include "crash_point.hpp"


#include <sstream>
#include <vector>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <thread>
#include <condition_variable>

//实现并发需要一个后台线程后台 compaction 则适合由引擎自己管理。 因为它的触发条件、输入文件、发布和清理流程都是引擎内部的职责。
struct PendingTable {
    std::unique_ptr<SSTable> table;
    std::filesystem::path temporary_path;
    std::filesystem::path final_path;
};

struct CompactionPlan {
    std::vector<std::filesystem::path> input_paths;
    PendingTable output;
};

class StoreEngine{
using EntryMap = std::map<std::string, Entry, std::less<>>;

    public:
    explicit StoreEngine(const std::filesystem::path& dataDir){
       if (!open(dataDir.string())) {
       throw std::runtime_error("StoreEngine initialization failed");
       }
       compaction_thread_ = std::thread(
        &StoreEngine::CompacteWorkloop, this);
    }

    ~StoreEngine() {
    {
        std::lock_guard<std::mutex> lock(state_mu_);
        stopping_ = true;
    }

    compaction_cv_.notify_one();

    // 不能持 state_mu_ 等待，否则后台可能拿不到锁而死锁。
    if (compaction_thread_.joinable()) {
        compaction_thread_.join();
    }
}

    bool put(std::string key, std::string value);
    std::optional<std::string> get(std::string_view key);
    bool remove(std::string_view key);
    PendingTable createNewTable(bool isCompacte = false);
    bool recover();

    //flush是把memtable写入磁盘
    bool flush();
    

    //writeFile是把合并后的SSTable一个文件写入磁盘
    bool writeCompacteFile(const std::vector<TableHandle>& sstables);


    std::vector<std::filesystem::path> loadTables(std::string& dir);
     std::string formatId(uint64_t id);

    bool open(const std::string& dir);
    void sortDigital(std::vector<std::filesystem::path>& files);


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

    //flush是把memtable写入磁盘
   
    bool flushLocked();
    
    //目录：保存了“文件名 → 文件对象”的关联，这用来持久化同步目录还有对应的文件名关系
    bool syncDirectory(const std::filesystem::path& path);
    bool removeOldFiles(std::vector<std::filesystem::path> oldPaths);

    //存储没有compaction处理的旧目录
    std::vector<std::filesystem::path> oldpaths;

    //并发设计
    std::mutex state_mu_; //保护memtable_、wal_、sstables_、next_file_id_，前台并行
    std::mutex compaction_mu_; // 保证同时只有一个 compaction

    std::thread compaction_thread_;//专门归属于engine的后台线程
    std::condition_variable compaction_cv_;

    bool compaction_requested_ = false;
    bool stopping_ = false;

    CompactionPlan prepareCompactionLocked();

    bool buildCompactionOutput(CompactionPlan& plan);
    bool publishCompactionOutput(PendingTable& pending);
    bool installCompactionLocked(CompactionPlan& plan);

    bool cleanupCompactionInputs(
        const std::vector<std::filesystem::path>& paths);
    void CompacteWorkloop();
    void requestCompactionLocked();


};

