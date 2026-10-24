#include "SSTable.hpp"
#include <iomanip>
#include <iostream>


bool SSTable::writeTable(const MemTable&memtable){
   return writeEntries(memtable.table());
}
bool SSTable::writeEntries(const EntryMap& entries) {
    for (const auto& [key, entry] : entries) {
        if (!writeEntry(key, entry.value, entry.isdelete)) {
            return false;
        }
    }
    return true;
}
/*STable::look() 返回包含删除标记的记录是正确的，
不应该把它过滤成 NotFound。 否则引擎会继续查询旧表，让被删除的值重新出现*/
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
        size_t space =buffer_size - write_pos;
        size_t to_copy = std::min(space,remaining);

        std::memcpy(write_buffer_.data()+write_pos,src+offset,to_copy);
        write_pos+=to_copy;
        offset +=to_copy;

        if (write_pos == buffer_size) {
        // 此时 buffer 中有 1024 字节有效数据
            if(!flushWriteBuffer()) return false;         
            write_pos = 0;           // 重置写入位置，下次从头覆盖
        }
    }
    return true;   
}

ReadResult SSTable::readExact( void* dst, std::size_t size){
    auto* out = static_cast<unsigned char*>(dst);
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

//把读取到的record通过参数来返回
ReadResult SSTable::readRecord(Record& record){
    //parse string 的功能，创建一个record,然后swap

    /*没读到任何字节就 EOF	正常结束
    已读到部分记录，再遇到 EOF	记录不完整*/
    Record tmp{};
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

    read_pos_ = 0;
    read_end_ = 0;
    return true;
}

//它的任务只需要把所有传入缓冲区的内容写入fd就行了，至于大小则是由append来控制的
//需要深入理解
bool SSTable::flushWriteBuffer()
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


bool SSTable::writeRecord(const Record& record) {
    return writeEntry(record.key, record.value, record.isdelete);
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

    if (!writeUint32LE(static_cast<std::uint32_t>(key.size())))
        return false;
    if (!writeUint32LE(static_cast<std::uint32_t>(value.size())))
        return false;
    if (!writeDeleteFlag(isdelete)) return false;
    if (!writeString(key)) return false;
    if (!writeString(value)) return false;

    return true;
}


