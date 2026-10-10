//相当于合并SSTable文件
//同一个 key 的历史值不断积累，合并时只保留需要的最新版本
#pragma once
#include <map>
#include <string>
#include <filesystem>
#include "SSTable.hpp"

class Compaction{
    public:

    bool compacte(const std::string& table_dir);
  

    const std::map<std::string, Entry, std::less<>>& getMerged(){
        return merged;
    }
    
    private:
    bool scanAndCombine(SSTable& table);
    std::map<std::string, Entry, std::less<>> merged;
};