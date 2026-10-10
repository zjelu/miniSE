/*因为 MemTable 不会每次写入都立刻生成 SSTable。在这段等待期间，
如果进程崩溃，内存数据就丢了，WAL 用来恢复这部分数据。*/

/*为什么有 WAL，还需要 SSTable？ 因为日志中可能有大量重复更新。
直接靠 WAL 查询一个 key，朴素实现需要扫描日志才能确定最新值；
恢复时也要重放越来越多的历史操作。SSTable 将数据按 key 整理，配合索引可以更高效地查询磁盘数据
*/
#pragma once
#include "memTable.hpp"
#include "WAL.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

enum class OpenMode {
    CreateNew,
    OpenExisting
};

struct Record{
    std::uint32_t lenKey = 0;
    std::uint32_t lenValue = 0;
    bool isdelete;
    std::string key;
    std::string value;
};


class SSTable{
using EntryMap = std::map<std::string, Entry, std::less<>>;

    public:
    // 新 SSTable：要求文件不存在，否则报 File exists
    explicit SSTable(const std::string& path, OpenMode mode){
    int flags = mode == OpenMode::CreateNew
    ? O_RDWR | O_CREAT | O_EXCL
    : O_RDONLY;

   fd_ = ::open(path.c_str(), flags, 0644);

    if (fd_ == -1) {
            perror("open SSTable");
        }
    }

    ~SSTable(){
        if(fd_>=0){
            close(fd_);
        }
    }

    

    SSTable(const SSTable&) = delete;
    SSTable& operator=(const SSTable&) = delete;

    bool writeTable(const MemTable&memtable);
    bool writeEntries(const EntryMap& entries);
    LookupState look(std::string_view key,Record& record);

    bool sync();//首次写完临时的SSTable文件时候选择同步
    ReadResult readRecord(Record& record);


    private:
    bool writeUint32LE(std::uint32_t value);
    ReadResult readUint32LE(std::uint32_t& value);

    bool writeString(std::string_view value);
    ReadResult readString(std::uint32_t length, std::string& value);

    bool writeDeleteFlag(bool isdelete);
    ReadResult readDeleteFlag(bool& isdelete);

    ReadResult readExact(void* buffer, std::size_t size);
    ReadResult requireField(ReadResult status);

    


    int fd_=-1;
};