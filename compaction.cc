#include "compaction.hpp"
#include <algorithm>
#include <vector>



bool Compaction::compacte(const std::string& table_dir) {
    merged.clear();

    namespace fs = std::filesystem;
    std::vector<fs::path> files;

    // 1. 收集 SSTable 文件。
    for (const auto& entry : fs::directory_iterator(table_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".sst") continue;

        files.push_back(entry.path());
    }

    // 2. 按文件名从旧到新排序。
    std::sort(files.begin(), files.end());

    // 3. 新表后读，让新记录覆盖旧记录。
    for (const auto& path : files) {
        SSTable sstable(path.string(), OpenMode::OpenExisting);

        if (!scanAndCombine(sstable)) {
            std::cerr << "scan and combine failed\n";
            return false;
        }
    }

    return true;
}

bool Compaction::scanAndCombine(SSTable& table){
        
        while (true) {
        Record record{};
        auto result = table.readRecord(record);

        if (result == ReadResult::EndOfFile) {
            return true;
        }

        if (result != ReadResult::Ok) {
            return false;
        }

        merged.insert_or_assign(
            record.key,
            Entry{record.value, record.isdelete}
        );
    }
}