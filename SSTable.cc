#include "SSTable.hpp"
#include <iomanip>
#include <iostream>


bool SSTable::writeTable(const MemTable&memtable){
   return writeEntries(memtable.table());
}

bool SSTable::writeEntries(const EntryMap& entries) {
    // 复用原 writeTable 中遍历、写长度、标记和内容的逻辑。
    for (const auto& [key, entry] :entries) {
    uint32_t keyLength = key.size();
    uint32_t valueLength = entry.value.size();

    if(!writeUint32LE(keyLength)) return false;
    if(!writeUint32LE(valueLength)) return false;
    if(!writeDeleteFlag(entry.isdelete)) return false;
    if(!writeString(key)) return false;
    if(!writeString(entry.value)) return false;
    }

    return true;
}


/*STable::look() 返回包含删除标记的记录是正确的，
不应该把它过滤成 NotFound。 否则引擎会继续查询旧表，让被删除的值重新出现*/
LookupState SSTable::look(std::string_view key,Record& record){
    Record temp{};
    if (::lseek(fd_, 0, SEEK_SET) == -1) {
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

    std::size_t written = 0;
    while(written < sizeof(bytes)){
        ssize_t n = write(
            fd_,
            bytes + written,
            sizeof(bytes) - written
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
            return false;
        }
        // write() 返回 0：没有取得进展，避免死循环
        return false;
    }
    return true;
}

//写字符串
bool SSTable::writeString( std::string_view value){
    std::size_t written = 0;
    while(written < value.size()){
        ssize_t n = write(
            fd_,
            value.data() + written,
            value.size() - written
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
            return false;
        }
        // write() 返回 0：没有取得进展，避免死循环
        return false;
    }
    return true;
}

bool SSTable::writeDeleteFlag(bool isdelete){
   
    while(true){
        unsigned char flag = isdelete ? 1 : 0;
        ssize_t n = ::write(fd_, &flag, sizeof(flag));

        if(n>0) return true;

        if (n == -1 && errno == EINTR) {
            continue;
        }
        if (n == -1) {
            perror("write");
            return false;
        }
        // write() 返回 0：没有取得进展，避免死循环
        return false;
    }
    return true;
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

ReadResult SSTable::readExact( void* buffer, std::size_t size){
    auto* dst = static_cast<unsigned char*>(buffer);
    std::size_t total = 0;

    while (total < size) {
        ssize_t n = ::read(fd_, dst + total, size - total);

        if (n > 0) {
            total += static_cast<std::size_t>(n);
        } else if (n == 0) {
            return total == 0
                ? ReadResult::EndOfFile
                : ReadResult::Truncated;
        } else if (errno != EINTR) {
            return ReadResult::Error;
        }
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
    while (::fsync(fd_) == -1) {
        if (errno == EINTR) continue;

        perror("fsync SSTable");
        return false;
    }
    return true;
}




