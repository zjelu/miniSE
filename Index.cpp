#include "Index.hpp"
#include <algorithm>


bool SSTableMetass:: saveMetadata(
    const std::vector<SSTableMeta>& metas){

    // 1. 创建临时文件

    // 2. 写入文件头
    // magic, version, count

    // 3. 逐条序列化每个 meta

    // 4. 刷新用户态缓冲区

    // 5. fsync 临时文件

    // 6. close 临时文件，检查错误

    // 7. rename 临时文件替换正式文件

    // 8. fsync 所在目录

    }

//从磁盘恢复索引与文件元数据
bool SSTableMetas:: loadMetadata(
    std::vector<SSTableMeta>& metas){

        // 1. 读取并验证 Header

    // 2. 读取 Meta Count

    // 3. 循环读取每个 Meta

    // 4. 检查每个字段及索引顺序是否合法

    // 5. 检查文件是否有意外多余数据

    // 6. 完全成功后提交结果
    
    }
    //元数据缺失或不可用时，通过扫描 SSTable 重建

bool SSTableMeta:: rebuildMetadata(
    const std::filesystem::path& sstable_dir,
    std::vector<SSTableMeta>& metas){

    }
        //遍历 SSTable，读取 Record，每隔 128 条生成一个索引项。  
        

bool SSTableMetas::write_u32(uint32_t v) { char b[4]; put_u32(b,v); return write_all(b,4); }
bool SSTableMetas::write_u64(uint64_t v) { char b[8]; put_u64(b,v); return write_all(b,8); }
bool SSTableMetas::write_str(const std::string& s) {
    if (s.empty()) return true;              // 避免 write_all(nullptr,0)
    return write_all(s.data(), s.size());
}

void SSTableMetas:: put_u32(char* p, uint32_t v){
    p[0] = char(v      );
    p[1] = char(v >>  8);
    p[2] = char(v >> 16);
    p[3] = char(v >> 24);
}

// 读
bool SSTableMetas:: read_u32(uint32_t& v) {
    char b[4];
    if (read_all(b,4) != ReadResult::Ok) return false;
    v = get_u32(b); return true;
}

uint32_t SSTableMetas:: get_u32(const char* p){
    return  uint32_t(uint8_t(p[0]))
         | (uint32_t(uint8_t(p[1])) <<  8)
         | (uint32_t(uint8_t(p[2])) << 16)
         | (uint32_t(uint8_t(p[3])) << 24);
}

bool SSTableMetas:: read_str(std::string& s, uint32_t len) {
    if (len == 0) { s.clear(); return true; }
    s.resize(len);
    return read_all(&s[0], len) == ReadResult::Ok;
}

//这里的绝大多数代码都属于reuse，不过考虑代码量比较小，所以没有抽象出来，而是每个类都有
bool SSTableMetas:: write_all( const void* data, size_t size){
    const char* src = static_cast<const char*>(data);

    //已经从data中拷贝了多少字节
    size_t offset =0;

    while(offset<size){
        size_t remaining = size - offset;
        size_t space =buffer_size - write_pos;
        size_t to_copy = std::min(space,remaining);

        std::memcpy(write_buffer_.data()+write_pos,src+offset,to_copy);
        write_pos+=to_copy;
        offset +=to_copy;

        if (write_pos == buffer_size) {
        // 此时 buffer 中有 1024 字节有效数据
            if(!flushWriteBuffer()) return false;         
            //write_pos = 0;    自动重置写入位置，下次从头覆盖
        }
    }
    return true;  
}

ReadResult SSTableMetas:: read_all(void* data, size_t size){
     auto* out = static_cast<unsigned char*>(data);
    std::size_t total = 0;

    while (total < size) {
        if(read_pos_ == read_end_){
            ssize_t n;
            do{
                n=::read(fd_, 
                    read_buffer_.data(), 
                    read_buffer_.size()
                );
            }while(n<0 && errno ==EINTR);
            
            if(n==0){
                return total == 0
                ? ReadResult::EndOfFile
                : ReadResult::Truncated;
            }
            if(n<0){
                return ReadResult::Error;
            }
            read_pos_ = 0;
            read_end_ = static_cast<size_t>(n);
        }

        //这段的功能是把内容从读缓冲区的内容传输到输出缓冲区中
        size_t available = read_end_ - read_pos_;
        size_t needed = size - total;
        size_t count = std::min(available, needed);

        std::memcpy(
            out + total,
            read_buffer_.data() + read_pos_,
            count
        );
        read_pos_ += count;
        total += count;
    }
    return ReadResult::Ok;
}

bool SSTableMetas::flushWriteBuffer()
{
    std::size_t written = 0;
    while(written <write_pos){
        ssize_t n = write(
            fd_,
            write_buffer_.data() + written,
            write_pos - written
        );
        if (n > 0) {
            written += static_cast<std::size_t>(n);
            continue;
        }
        if (n == -1 && errno == EINTR) {
            continue;
        }
        if (n == -1) {
            perror("write");
        }

       // 不论返回 -1 还是 0，都保留未写出的后缀。
        const auto remaining = write_pos - written;
        std::memmove(
            write_buffer_.data(),
            write_buffer_.data() + written,
            remaining
        );
        write_pos = remaining;
        return false;
    }
    write_pos=0;
    return true;
}