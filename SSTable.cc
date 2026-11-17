#include "SSTable.hpp"
#include <iomanip>
#include <iostream>
#include <limits>


bool SSTable::writeTable(const MemTable& memtable){
   return writeEntries(memtable.table());
}
bool SSTable::writeEntries(const EntryMap& entries) {
    for (const auto& [key, entry] : entries) {
        if (!writeEntry(key, entry.value, entry.isdelete)) {
            return false;
        }
    }
    if(!finish()) return false;
    return true;
}
/*STable::look() 返回包含删除标记的记录是正确的，
不应该把它过滤成 NotFound。 否则引擎会继续查询旧表，让被删除的值重新出现*/
LookupState SSTable::fastLook(std::string_view key,Record& record){
    //先看内存稀疏索引，找到最后一个 key <= 目标 key 的索引项。
    int SparseIndexSize=sparse_index.size();
    int left=0;
    int right = SparseIndexSize-1;

    int index=binarySearch(key,left,right);
    if(index==-1){
         return LookupState::NotFound;
    }

    const auto& entry = sparse_index[index];
    if (!seekTo(entry.offset)) {
        return LookupState::Error;
    }

    Record temp{};
    const auto next = static_cast<std::size_t>(index) + 1;
    const uint64_t end_offset =
        next < sparse_index.size()
            ? sparse_index[next].offset
            : data_end_offset_;

    while(next_read_offset_<=end_offset){
        auto result = readRecord(temp);
        if(result==ReadResult::EndOfFile){
        LOG_DEBUG("[SSTablelook] result==ReadResult::EndOfFile");
            return LookupState::NotFound;
        }

        if (result != ReadResult::Ok) {
        std::cerr << "[lookup] Error: failed to read record\n";
        return LookupState::Error;
        }
        if (temp.key == key) {
            std::cerr << "[lookup] Found\n";
            record = std::move(temp);
            return LookupState::Found;
        }
    }
    return LookupState::NotFound;

}

int SSTable::binarySearch(std::string_view key,int left, int right, int result){
    if(left>right) return result;
     int middle = (left+right)/2;
    
     if(sparse_index[middle].key==key){
        return middle;
     }
     else if(sparse_index[middle].key<=key){
        left = middle;
        result = middle;
        return binarySearch(key,left+1, right,result);
     }
     else {
        right = middle;
        return binarySearch(key,left, right-1,result);
     }

}


LookupState SSTable::look(std::string_view key,Record& record){
    Record temp{};
    if (!rewind()) {
    return LookupState::Error;
    }

   while(true){
    const off_t before = ::lseek(fd_, 0, SEEK_CUR);

    auto result = readRecord(temp);

    const off_t after = ::lseek(fd_, 0, SEEK_CUR);

   std::cerr << "[lookup] target=" << std::quoted(key)
              << " before=" << before
              << " after=" << after
              << " result=" << static_cast<int>(result)
              << '\n';

    if(result==ReadResult::EndOfFile){
    LOG_DEBUG("[SSTablelook] result==ReadResult::EndOfFile");
        return LookupState::NotFound;
    }

    if (result != ReadResult::Ok) {
        std::cerr << "[lookup] Error: failed to read record\n";
        return LookupState::Error;
    }
    // 只有读取成功后，才打印和比较记录。
    std::cerr << "[record] key=" << std::quoted(temp.key)
              << " key_len=" << temp.lenKey
              << " value_len=" << temp.lenValue
              << " deleted=" << temp.isdelete
              << '\n';

    if (temp.key == key) {
        std::cerr << "[lookup] Found\n";
        record = std::move(temp);
        return LookupState::Found;
    }
    if (temp.key > key) {
        std::cerr << "[lookup] NotFound: passed target key\n";
        return LookupState::NotFound;
    }
}
}
//写长度
bool SSTable::writeUint32LE( std::uint32_t length){
     unsigned char bytes[4] = {
    static_cast<unsigned char>(length),
    static_cast<unsigned char>(length >> 8),
    static_cast<unsigned char>(length >> 16),
    static_cast<unsigned char>(length >> 24)
    };

    return appendBytes(bytes, sizeof(bytes));
}

bool SSTable::writeUint64LE(uint64_t value){
    unsigned char bytes[8];
    for(int i=0;i<8;i++){
        bytes[i] = static_cast<unsigned char>(
            value>>(8*i) //每次取出一个整数中的八位，将它转换为一个字节
        );
    }
    return appendBytes(bytes,sizeof(bytes));
}

ReadResult SSTable::readUint64LE( std::uint64_t& length){
    unsigned char bytes[8];

    auto result = readExact(bytes, sizeof(bytes));
    if (result != ReadResult::Ok) {
        return result;
    }

    length = 0;
    for (int i = 0; i < 8; ++i) {
        length |= static_cast<std::uint64_t>(bytes[i]) << (8 * i);
    }

    return ReadResult::Ok;
}

bool SSTable::writeString(std::string_view value)
{
    return appendBytes(value.data(), value.size());
}

bool SSTable::writeDeleteFlag(bool isdelete){
    unsigned char flag = isdelete ? 1 : 0;
    return appendBytes(&flag, 1);
}
//读取4字节长度，统一返回状态，通过引用参数输出结果
ReadResult SSTable::readUint32LE( std::uint32_t& length){
    unsigned char bytes[4];

    auto result = readExact(bytes, sizeof(bytes));
    if (result != ReadResult::Ok) {
        return result;
    }
    length =
          static_cast<std::uint32_t>(bytes[0])
        | (static_cast<std::uint32_t>(bytes[1]) << 8)
        | (static_cast<std::uint32_t>(bytes[2]) << 16)
        | (static_cast<std::uint32_t>(bytes[3]) << 24);

    return ReadResult::Ok;
}

//读取length长度， 统一返回状态，通过引用参数输出结果
//将 readString() 改为接收已知长度，不再自己读取长度
ReadResult SSTable::readString( std::uint32_t length, std::string& value){

    // 示例限制，需要与你的写入限制保持一致。
    constexpr std::uint32_t maxLength = 16 * 1024 * 1024;
    if (length > maxLength) {
        return ReadResult::Error;
    }

    std::string buffer(length, '\0');
    auto result = readExact(buffer.data(), buffer.size());

    // 已读到长度，但字符串内容缺失，属于记录不完整。
    if (result == ReadResult::EndOfFile) {
        return ReadResult::Truncated;
    }

    if (result != ReadResult::Ok) {
        return result;
    }

    value.swap(buffer);
    return ReadResult::Ok;

}

ReadResult SSTable::readDeleteFlag(bool& isdelete) {
    unsigned char flag = 0;
    auto result = readExact(&flag, sizeof(flag));

    if (result != ReadResult::Ok) {
        return result;
    }
    if (flag > 1) {
        return ReadResult::Error;
    }
    isdelete = (flag == 1);
    return ReadResult::Ok;
}

bool SSTable::appendBytes(const void* data, size_t size){
    const char* src = static_cast<const char*>(data);

    //已经从data中拷贝了多少字节
    size_t offset =0;

    while(offset<size){
        size_t remaining = size - offset;
        size_t space =buffer_size - write_buffer_size_;
        size_t to_copy = std::min(space,remaining);

        std::memcpy(write_buffer_.data()+write_buffer_size_,src+offset,to_copy);
        write_buffer_size_+=to_copy;
        offset +=to_copy;

        next_write_offset_+=to_copy;
        if (write_buffer_size_ == buffer_size) {
        // 此时 buffer 中有 1024 字节有效数据
            if(!flushWriteBuffer()) return false;         
            write_buffer_size_ = 0;           // 重置写入位置，下次从头覆盖
        }
    }
    return true;   
}

ReadResult SSTable::readExact( void* dst, std::size_t size){
    auto* out = static_cast<unsigned char*>(dst);
    std::size_t total = 0;

    while (total < size) {
        if(read_buffer_pos_ == read_buffer_size_){
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
            read_buffer_pos_ = 0;
            read_buffer_size_ = static_cast<size_t>(n);
        }

        //这段的功能是把内容从读缓冲区的内容传输到输出缓冲区中
        size_t available = read_buffer_size_ - read_buffer_pos_;
        size_t needed = size - total;
        size_t count = std::min(available, needed);

        std::memcpy(
            out + total,
            read_buffer_.data() + read_buffer_pos_,
            count
        );
        read_buffer_pos_ += count;
        total += count;
        next_read_offset_+=count;
    }
    return ReadResult::Ok;
}

//把读取到的record通过参数来返回
ReadResult SSTable::readRecord(Record& record){
    std::cerr << "[readRecord] pos=" << next_read_offset_
          << " data_end=" << data_end_offset_
          << '\n';
    //parse string 的功能，创建一个record,然后swap

    /*没读到任何字节就 EOF	正常结束
    已读到部分记录，再遇到 EOF	记录不完整*/
    Record tmp{};
    if (next_read_offset_ == data_end_offset_) {
    return ReadResult::EndOfFile;
    }

    if (next_read_offset_ > data_end_offset_) {

        return ReadResult::Error;
    }
    
    auto status = readUint32LE(tmp.lenKey);
    if(status != ReadResult::Ok) return status;
    
    status = readUint32LE(tmp.lenValue);
    if(status != ReadResult::Ok ) return requireField(status);

    status = readDeleteFlag(tmp.isdelete);
    if(status != ReadResult::Ok ) return requireField(status);

    status = readString(tmp.lenKey, tmp.key);
    if(status != ReadResult::Ok ) return requireField(status);

    status = readString(tmp.lenValue, tmp.value);
    if(status != ReadResult::Ok ) return requireField(status);

    

    record = std::move(tmp);
    return ReadResult::Ok;
}

ReadResult SSTable::requireField(ReadResult status){
    if (status == ReadResult::EndOfFile) {
    return ReadResult::Truncated;
    }
    if (status != ReadResult::Ok) {
        return status;
    }
        return status;
}

bool SSTable::sync(){
    if (!flushWriteBuffer())
        return false;
    while (::fsync(fd_) == -1) {
        if (errno == EINTR) continue;
        perror("fsync SSTable");
        return false;
    }
    return true;
}


bool SSTable::rewind()
{
    if (!flushWriteBuffer()) return false;

    if (::lseek(fd_, 0, SEEK_SET) == -1)
        return false;

    read_buffer_pos_ = 0;
    read_buffer_size_ = 0;
     return seekTo(0);
}

//它的任务只需要把所有传入缓冲区的内容写入fd就行了，至于大小则是由append来控制的
//需要深入理解
bool SSTable::flushWriteBuffer()
{
    std::size_t written = 0;
    while(written <write_buffer_size_){
        ssize_t n = write(
            fd_,
            write_buffer_.data() + written,
            write_buffer_size_ - written
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
        const auto remaining = write_buffer_size_ - written;
        std::memmove(
            write_buffer_.data(),
            write_buffer_.data() + written,
            remaining
        );
        write_buffer_size_ = remaining;
        return false;
    }
    write_buffer_size_=0;
    return true;
}


bool SSTable::writeRecord(const Record& record) {
    return writeEntry(
        record.key,
        record.value,
        record.isdelete
    );
}

bool SSTable::writeEntry(
    std::string_view key,
    std::string_view value,
    bool isdelete
) {
    // 在写任何字节之前检查。
    if (key.size() > max_field_length ||
        value.size() > max_field_length) {
        return false;
    }

    const uint64_t offset = next_write_offset_;

    if (!writeUint32LE(static_cast<std::uint32_t>(key.size())))
        return false;
    if (!writeUint32LE(static_cast<std::uint32_t>(value.size())))
        return false;
    if (!writeDeleteFlag(isdelete)) return false;
    if (!writeString(key)) return false;
    if (!writeString(value)) return false;

    // Record 成功写入缓冲系统之后再建立索引
    if (record_count % 128 == 0) {
        sparse_index.push_back({
            std::string(key),
            offset
        });
    }

    ++record_count;
    return true;
}

bool SSTable::writeSparseIndex(){
 
     for (const auto& entry : sparse_index) {
        if (!writeUint32LE(static_cast<uint32_t>(entry.key.size()))) return false;
        if (!writeString(entry.key)) return false;
        if (!writeUint64LE(entry.offset)) return false;
    }

    return true;
}

bool SSTable::writeFooter(){
   if (!writeUint64LE(footer.index_offset))
        return false;

    if (!writeUint64LE(footer.index_size))
        return false;

    if (!writeUint64LE(footer.magic))
        return false;
    return true;

}

//require:是把磁盘中的稀疏索引加载到内存的 sparse_index 中
ReadResult SSTable::readSparseIndex(){
    if(!seekTo(footer.index_offset)) return ReadResult::Error;
    while(next_read_offset_< 
        footer.index_offset + footer.index_size){
        auto entry = IndexEntry{};
        auto status = readIndexEntry(entry);
        if(status != ReadResult::Ok) return(status);
        sparse_index.push_back(entry);
    }

    return ReadResult::Ok; 
}

//require:读取一行行的索引
ReadResult SSTable::readIndexEntry(IndexEntry& entry){
    std::uint32_t key_length = 0;
    auto status = readUint32LE(key_length);
    if (status != ReadResult::Ok) {
        return status;
    }

    IndexEntry temp{};

    status = readString(key_length, temp.key);
    if (status != ReadResult::Ok) {
        return requireField(status);
    }

    status = readUint64LE(temp.offset);
    if (status != ReadResult::Ok) {
        return requireField(status);
    }

    entry = std::move(temp);
    return ReadResult::Ok;
}

/*
[有序 Record 数据][稀疏索引][固定 24 字节 Footer]
                   ↑
              index_offset
每个索引项：
[key长度：4字节][key内容：长度由前面决定][offset：固定8字节]

Footer：
  index_offset  8 字节 
  index_size    8 字节
  magic         8 字节
  大小为8，不是值为8
*/

//require:把 index_offset 等信息保存在成员变量 footer
bool SSTable::readFooter(){
    //先定位到末尾的footer
    /*读入临时 Footer，验证：
    - magic == kMagic
    - index_offset <= 文件大小 - 24
    - index_size == 文件大小 - 24 - index_offset */
    
    std::uint64_t file_size = get_file_size(fd_);
    if(!seekTo(file_size-24)) return false;
    if (readUint64LE(footer.index_offset) != ReadResult::Ok)
        return false;

    if (readUint64LE(footer.index_size)!= ReadResult::Ok)
        return false;

    if (readUint64LE(footer.magic)!= ReadResult::Ok)
        return false;

    if(footer.magic == kMagic 
        && footer.index_offset <= file_size - 24 
        && footer.index_size == file_size - 24 - footer.index_offset)
        {
            data_end_offset_ = footer.index_offset;

            return true;
        }
    std::cerr<<"footer parameters incorrect\n";
    return false; 
}

bool SSTable::seekTo(uint64_t offset) {
    if (offset > static_cast<uint64_t>(
            std::numeric_limits<off_t>::max())) {
        return false;
    }

    if (::lseek(fd_, static_cast<off_t>(offset), SEEK_SET) == -1){
        std::cerr<<"fail to seek offset\n";
        return false;
    }
        
    read_buffer_pos_ = 0;
    read_buffer_size_ = 0;
    next_read_offset_ = offset;

    return true;
}

bool SSTable::finish(){
    data_end_offset_ = next_write_offset_;
    footer.index_offset = data_end_offset_;
    
    if(!writeSparseIndex()) return false;

    footer.index_size = next_write_offset_ - footer.index_offset;
    footer.magic = kMagic;

    if (!writeFooter())
        return false;

    return sync();
}