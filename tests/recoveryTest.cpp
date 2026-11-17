#include "storeEngine.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <chrono>

namespace fs = std::filesystem;
using Entries = std::map<std::string, Entry, std::less<>>;


// 不使用 assert：Release 构建中也必须执行检查。
void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

void testRecoveryWithoutFlush(const fs::path& root){

    {
    StoreEngine engine(root);

    engine.put("a", "100");
    engine.put("b", "200");

    // 不调用 flush()
    }

    // 析构已经完成

    {
        StoreEngine engine(root);
         require(engine.get("a") == "100", "无法获取a");
         require(engine.get("b") == "200", "无法获取b");
        // 验证 a、b 是否正确恢复
    }
    std::cerr<<"测试通过\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "用法: compaction_test <独立测试目录> <write|reopen>\n";
        return 2;
    }
    try {
        const auto root = fs::absolute(argv[1]);
       
            require(!fs::exists(root) || fs::is_empty(root), "只接受全新空目录");
            fs::create_directories(root);
            testRecoveryWithoutFlush(root);
            //std::cout << "[PASS] write 完成，请用相同目录运行 reopen\n";
            std::cout << "[PASS] 未 flush 数据的恢复测试通过\n";
        
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}