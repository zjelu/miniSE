
//sstable_meta协议设计：
//

//不需要创建一个新类
//去对应的文件，对应的偏移量来读取内容
#pragma once;
#include <cstdint>
#include <cstddef>
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>

class SSTableMetas{
    public:

    struct IndexEntry {
        std::string key;
        uint64_t offset;
    };

//一个SSTableMeta对应一个文件
    struct SSTableMeta {
        uint64_t generation;
        std::string min_key;
        std::string max_key;
        std::filesystem::path;
        std::vector<IndexEntry> sparse_index;
    };

    //尝试把恢复数据写入saveMetadata
    //这里的path对应的是sstable_meta文件
    bool saveMetadata(
        const std::vector<SSTableMeta>& metas,
        const std::filesystem::path& path);

    //从磁盘恢复索引与文件元数据
    bool loadMetadata(
        const std::filesystem::path& path,
        std::vector<SSTableMeta>& metas);
        //元数据缺失或不可用时，通过扫描 SSTable 重建

    bool rebuildMetadata(
        const std::filesystem::path& sstable_dir,
        std::vector<SSTableMeta>& metas);
            //遍历 SSTable，读取 Record，每隔 128 条生成一个索引项。  
            
    private:
    int fd_;
    const std::filesystem::path& path;

    static constexpr size_t buffer_size = 16*1024;
    std::vector<char> read_buffer_ = std::vector<char>(buffer_size);
    size_t read_pos_ = 0;
    size_t read_end_ = 0;

    //新添一个写缓冲区设计
    std::vector<char> write_buffer_ = std::vector<char>(buffer_size);
    size_t write_pos =0;//当前buffer中已经写入的字节数
    
    void put_u32(char* p, uint32_t v);
    uint32_t get_u32(const char* p);
    bool write_all(int fd, const void* data, size_t n);
    bool read_all(int fd, void* data, size_t n);

};

//std::vector<SSTableMeta>& metas是查找文件的时候借助的数据结构
//由于放在内存中，所以需要考虑持久性存储

/*
启动时可以：
1. 先按照现有规则恢复有效 SSTable。
2. 尝试加载 sstable_meta.bin。sstable_meta.bin 保存每张 SSTable 的元数据。
3. 核对元数据中的文件编号、大小及其他必要身份信息，确认与实际有效 SSTable 匹配。
4. 匹配成功则直接使用；否则扫描有效 SSTable 重建索引。
5. 将重新构建的元数据保存到文件，供下次启动使用
*/
//每次 SSTable 集合发生变化，就将当前有效的全部 SSTableMeta 重新写入一个临时文件
//写入成功并同步后，再通过 rename() 替换旧文件。