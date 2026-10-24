#include "compaction.hpp"
#include <algorithm>
#include <vector>


void Compaction::sortDigital(std::vector<std::filesystem::path>& files){
    std::sort(files.begin(), files.end(),
    [](const auto& a, const auto& b){
        auto aName = a.filename().string();
        auto bName = b.filename().string();
        auto anum = std::string_view(aName).substr(4, aName.size() - 8);
        auto bnum = std::string_view(bName).substr(4, bName.size() - 8);
        return anum<bnum;
    });
}

bool Compaction::compacte(
    const std::vector<TableHandle>& sstables,
    SSTable& output)
{
    return mergeSortedFiles(sstables, output);
}

//为什么内存需求小，因为每次加一条record,就去出一条record，所以内存需求小
bool Compaction::mergeSortedFiles(
        const std::vector<TableHandle>& sstables,
        SSTable& output){
        int id=-1;

        std::priority_queue<
            HeapNode,
            std::vector<HeapNode>,
            Compare
        > heap;
        
        //阶段一，每个sstable都加入一条record
        for(auto& sstable:sstables){
            //先把偏移量回原位
            id++;
            if (!sstable.table->rewind())
                return false;
            Record record;
            auto result = sstable.table->readRecord(record);

            if(result==ReadResult::EndOfFile){
                std::cerr <<"[SSTablelook] result==ReadResult::EndOfFile\n";
                continue;
            }
            if (result != ReadResult::Ok) {
                std::cerr << "[lookup] Error: failed to read record\n";
                return false;
            }
            heap.push({
                std::move(record),
                id
            }); 
        }

        //阶段 2：从堆中取出记录，再补充下一条
        std::optional<std::string> last_key;
        while (!heap.empty()) {
            // 1. 获取并移除堆顶
            HeapNode node = heap.top();
            heap.pop();
            // 2. 判断是否重复
            if (!last_key || *last_key != node.record.key) {
                if(!output.writeRecord(node.record)){
                    std::cerr<<"outputwriteRecord failed\n";
                    return false;
                } 
                last_key = node.record.key;
            }

        // 3. 从刚刚弹出节点所属的 SSTable 读取下一条
            size_t i = node.table_index;
            Record next{};
            auto status = sstables[i].table->readRecord(next);
            if (status == ReadResult::Ok) {
                heap.push({std::move(next), i});
            }
            else if (status != ReadResult::EndOfFile) {
                return false;
            }
        }
        return true;
}