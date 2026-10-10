#include "storeEngine.hpp"
#include "compaction.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace fs = std::filesystem;

// 同时兼容 test-data/*.sst 和 test-data/sstables/*.sst。
std::set<fs::path> listTables(const fs::path& dir) {
    std::set<fs::path> result;
    for (const auto& entry : fs::recursive_directory_iterator(dir)) {
        if (entry.is_regular_file() &&
            entry.path().extension() == ".sst") {
            result.insert(entry.path().lexically_relative(dir));
        }
    }
    return result;
}

void expectValue(StoreEngine& engine,
                 const char* key, const char* expected) {
    auto value = engine.get(key);
    assert(value.has_value());
    assert(*value == expected);
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cerr << "用法:restart_test <数据目录> <write|reopen>\n";
        return 1;
    }

    const fs::path dir = fs::absolute(argv[1]);
    const std::string mode = argv[2];

    if (mode != "write" && mode != "reopen") return 1;

    // write 阶段必须使用新的、空的数据目录。
    //第一次模拟，使用全新的空目录
    if (mode == "write") {
        if (fs::exists(dir) && !fs::is_empty(dir)) {
            std::cerr << "write 阶段需要空目录，拒绝使用已有数据\n";
            return 1;
        }
        fs::create_directories(dir);
    }

    //这行代码会执行两次，但是一次是write，一次是reopen
    StoreEngine engine(dir);

    if (mode == "write") {
        bool ok = engine.put("a", "1");
        assert(ok);
        ok = engine.put("deleted", "old");
        assert(ok);
        ok = engine.flush();
        assert(ok);

        assert(!listTables(dir).empty());

        // 这些操作暂不 flush，留给 WAL 恢复。
        ok = engine.put("a", "2");
        assert(ok);
        ok = engine.put("b", "3");
        assert(ok);
        ok = engine.remove("deleted");
        assert(ok);

        std::cout << "[PASS] 写入完成，现在退出进程\n";
        return 0;
    }

    // reopen：新的进程必须恢复全部操作。
    //第二次测试，reopen模式，打开上一次初始化的目录，然后阐述读取
    expectValue(engine, "a", "2");
    expectValue(engine, "b", "3");
    assert(!engine.get("deleted").has_value());
    std::cout << "[PASS] 重启恢复覆盖写入、新增和删除\n";

    const auto before = listTables(dir);
    assert(!before.empty());

    bool ok = engine.put("c", "4");
    assert(ok);
    ok = engine.flush();
    assert(ok);

    const auto after = listTables(dir);
    assert(after.size() == before.size() + 1);
    for (const auto& path : before) {
        assert(after.count(path) == 1);
    }

    expectValue(engine, "a", "2");
    expectValue(engine, "b", "3");
    expectValue(engine, "c", "4");
    assert(!engine.get("deleted").has_value());

    std::cout << "[PASS] 重启后 flush 生成新文件，旧文件仍保留\n";
}