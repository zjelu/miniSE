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
#include <algorithm>
#include <cstring>
#include <vector>


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

    //流式合并函数之一，用于读取
    ReadResult readRecord(Record& record);
    bool writeRecord(const Record& record);
    //将文件偏移量还有offset都归零
    bool rewind();


    private:
    bool writeUint32LE(std::uint32_t value);
    ReadResult readUint32LE(std::uint32_t& value);

    bool writeString(std::string_view value);
    ReadResult readString(std::uint32_t length, std::string& value);

    bool writeDeleteFlag(bool isdelete);
    ReadResult readDeleteFlag(bool& isdelete);

    ReadResult readExact(void* dst, std::size_t size);
    ReadResult requireField(ReadResult status);

    bool appendBytes(const void* data, size_t size);//向内存缓冲区追加字节；必要时触发写出
    bool flushWriteBuffer();//使用 write() 将缓冲区剩余字节全部写入 fd，处理短写及 EINTR

    int fd_=-1;
    //新添的读缓冲区的设计，这样能够避免直接调用IO读取
    static constexpr size_t buffer_size = 16*1024;
    std::vector<char> read_buffer_ = std::vector<char>(buffer_size);
    size_t read_pos_ = 0;
    size_t read_end_ = 0;

    //新添一个写缓冲区设计
    std::vector<char> write_buffer_ = std::vector<char>(buffer_size);
    size_t write_pos =0;//当前buffer中已经写入的字节数

    //让两个写入接口复用编码与长度检查
    static constexpr std::size_t max_field_length = 16 * 1024 * 1024;
    bool writeEntry(
        std::string_view key,
        std::string_view value,
        bool isdelete
    );


};