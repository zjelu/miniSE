#include "memTable.hpp"

void MemTable::put(std::string key, std::string value){
    auto it = table_.find(key);

    if (it == table_.end()) {
        memory_usage_ += key.size();
        memory_usage_ += value.size();

        table_.emplace(std::move(key), Entry{std::move(value), false});
    } else {
        memory_usage_ -= it->second.value.size();
        memory_usage_ += value.size();

        it->second.value = std::move(value);
        it->second.isdelete = false;
    }
}
void MemTable::clear(){
    table_.clear();
    memory_usage_ = 0;
}

LookupResult MemTable::lookup(std::string_view key) const {
    auto it = table_.find(key);

    if (it == table_.end()) {
    LOG_DEBUG("[remove] key=%s state=NotFound",
            std::string(key).c_str());

        return {LookupState::NotFound, {}};
    }

    if (it->second.isdelete) {
    LOG_DEBUG("[remove] key=%s state=Deleted",
            std::string(key).c_str());

        return {LookupState::Deleted, {}};
    }

    return {LookupState::Found, it->second.value};
}

//这个逻辑需要放在storeegine中
/*std::optional<std::string> MemTable::get(std::string_view key){
    auto result = memtable.lookup(key);
    switch (result.state) {
    case LookupState::Found:
        return result.value;
    case LookupState::Deleted:
        return std::nullopt;
    case LookupState::NotFound:
        // 继续查询旧 MemTable 或 SSTable
        break;
    }
}*/

void MemTable::remove(std::string_view key){
    auto it = table_.find(key);
        
        if (it != table_.end()) {
            // 找到了数据，隐式构造 std::optional<std::string> 返回
            memory_usage_ -= it->second.value.size();
            it->second.value.clear();
            it->second.isdelete = true;
    LOG_DEBUG("[remove] key=%s state=mark_deleted",
            std::string(key).c_str());
        }
        else{
            //没有找到，穿创建一个新的key,然后设置为tombstone
             table_.emplace(std::string(key), Entry{"", true});
            memory_usage_ += key.size();
   LOG_DEBUG("[remove] key=%s state=mark_deleted",
          std::string(key).c_str());
        }
}

size_t MemTable::approximateMemoryUsage() const{
    int count = table_.size();
    size_t overheadMemory = count* kEntryOverhead;
    return  memory_usage_+overheadMemory;
}

