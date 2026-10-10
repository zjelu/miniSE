#pragma once
#include "log.hpp"
#include <string>
#include <optional>
#include <map>

struct Entry{
    std::string value;
    bool isdelete = false;
};

enum class LookupState{
    NotFound,
    Found,
    Deleted,
    Error
};

struct LookupResult{
    LookupState state;
    std::string value;
};



class MemTable{
    public:
    //因为无论如何memtable内部都有着一份数据，所以使用值传递未尝不可
    void put(std::string key, std::string value);

    LookupResult lookup(std::string_view key) const;
    void remove(std::string_view key);
   // std::optional<std::string>get(std::string_view key);

    void clear();
    size_t approximateMemoryUsage() const;
    
    const auto& table() const {
        return table_;
    }

    private:
    size_t kEntryOverhead = 64;
    std::map<std::string, Entry, std::less<>> table_;
    size_t memory_usage_ = 0;
};