/*因为 MemTable 不会每次写入都立刻生成 SSTable。在这段等待期间，
如果进程崩溃，内存数据就丢了，WAL 用来恢复这部分数据。*/

/*为什么有 WAL，还需要 SSTable？ 因为日志中可能有大量重复更新。
直接靠 WAL 查询一个 key，朴素实现需要扫描日志才能确定最新值；
恢复时也要重放越来越多的历史操作。SSTable 将数据按 key 整理，配合索引可以更高效地查询磁盘数据
*/

/*
read_buffer_pos_	readExact()、seekTo()
read_buffer_size_	readExact()、seekTo()
next_read_offset_	readExact()、seekTo()
write_buffer_size_	appendBytes()、flushWriteBuffer()
next_write_offset_	appendBytes()
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
#include <sys/stat.h>


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

    try {
        /*
        SSTable 构造函数抛出异常
    → SSTable 自身的析构函数不会执行
    → 已完成构造的成员会自动析构
        → sparse_index 释放索引项
            → 每个 key 释放字符串内存
        → footer 无额外资源需要释放
        → fd_ 只是整数，不会关闭文件*/

        if (mode == OpenMode::OpenExisting) {
            if (!readFooter()) {
                throw std::runtime_error("Invalid SSTable footer");
            }
            if (readSparseIndex()!=ReadResult::Ok) {
                throw std::runtime_error("Failed to read SSTable index");
                }
            if (!seekTo(0)) {
                    throw std::runtime_error("Failed to seek to SSTable data");
                }
            }
        } catch (...) {
            ::close(fd_);
            fd_ = -1;
            throw;  // 清理后，将原来的异常继续传给调用者
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
    LookupState look(std::string_view key, Record& record);
    LookupState fastLook(std::string_view key,Record& record);

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

    bool writeUint64LE(uint64_t value);
    ReadResult readUint64LE(std::uint64_t& value);


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
    size_t read_buffer_pos_ = 0; //读缓冲区内下一次读取的位置
    size_t read_buffer_size_ = 0;//读缓冲区内有效数据的结束位置

    //新添一个写缓冲区设计
    std::vector<char> write_buffer_ = std::vector<char>(buffer_size);
    size_t write_buffer_size_ =0;//当前buffer中已经写入的字节数

    //让两个写入接口复用编码与长度检查
    static constexpr std::size_t max_field_length = 16 * 1024 * 1024;
    bool writeEntry(
        std::string_view key,
        std::string_view value,
        bool isdelete
    );

    public:
        struct IndexEntry {
        std::string key;
        uint64_t offset;
    };

    std::vector<IndexEntry> sparse_index;
    struct Footer {
        uint64_t index_offset;
        uint64_t index_size;
        uint64_t magic;
    };

    bool finish();
    bool writeSparseIndex();
    ReadResult readSparseIndex();
    ReadResult readIndexEntry(IndexEntry& entry);
    int binarySearch(std::string_view key,int left, int right,int result =-1);
    bool writeFooter();
    bool readFooter();
    bool seekTo(uint64_t offset);

    private:
    //修改了文件协议之后，需要维护从writeRecords();到sync的完整写入流程
    //另外由于继续读取到 EOF，程序就有可能把 Sparse Index 中的数据错误地解析为 Record
    //所以需要保存uint64_t data_end_offset_;
    uint64_t next_read_offset_ = 0; // 下一次逻辑读取的位置
    uint64_t data_end_offset_ = 0;    // Record 区域结束位置

    int record_count=0;
    uint64_t next_write_offset_=0;
    Footer footer;
    static constexpr uint64_t kMagic = 0x1234567853535431ULL;
    
    std::uint64_t get_file_size(int fd) {
        struct stat st;
        if (::fstat(fd, &st) != 0) {
            throw std::system_error(errno, std::generic_category(), "fstat");
        }
        return static_cast<std::uint64_t>(st.st_size);
    }

};