//相当于合并SSTable文件
//同一个 key 的历史值不断积累，合并时只保留需要的最新版本
#pragma once
#include <map>
#include <string>
#include <filesystem>
#include <vector>
#include <queue>
#include "SSTable.hpp"

struct TableHandle{
    std::filesystem::path path;
    std::unique_ptr<SSTable> table;
};

struct HeapNode {
    Record record;
    size_t table_index;
};

struct Compare {
    bool operator()(const HeapNode& a,
                    const HeapNode& b) const {
        if (a.record.key != b.record.key)
            return a.record.key > b.record.key;

        // 同 Key：generation 越大越新
        return a.table_index < b.table_index;
    }
};


class Compaction{
    public:

    bool compacte(const std::vector<TableHandle>& sstables, SSTable& output);
    void sortDigital(std::vector<std::filesystem::path>& files);

    
    private:
    bool mergeSortedFiles(
    const std::vector<TableHandle>& sstables,
    SSTable& output);

    std::map<std::string, Entry, std::less<>> merged;
    
};