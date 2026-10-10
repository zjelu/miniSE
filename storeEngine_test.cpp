#include "storeEngine.hpp"
#include <cassert>
#include <iostream>

/*
cmake -S miniStoreEgine -B miniStoreEgine/build -DCMAKE_BUILD_TYPE=Debug
cmake --build miniStoreEgine/build --target store_engine_test -j

engine_test_dir=$(mktemp -d)

(
    cd "$engine_test_dir" &&
    /workspace/IO/miniStoreEgine/build/store_engine_test ./test.wal
)

cmake -S miniStoreEgine -B miniStoreEgine/build -DCMAKE_BUILD_TYPE=Debug
cmake --build miniStoreEgine/build --target crash_compaction_test -j

engine_test_dir=$(mktemp -d)

(
    cd "$engine_test_dir" &&
    /workspace/IO/miniStoreEgine/build/crash_compaction_test 
)
*/

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "用法：store_engine_test <WAL路径>\n";
        return 1;
    }
    
    std::cerr << "[test] before constructor" << std::endl;
    StoreEngine engine(argv[1]);

    auto expectValue = [&](const char* expected) {
       std::cerr << "[test] before get" << std::endl;
    auto result = engine.get("a");
    std::cerr << "[test] after get, has_value="
              << result.has_value() << std::endl;

        assert(result.has_value());
        assert(*result == expected);
    };

    // 1. MemTable 清空后，应从 SSTable 读取。
    std::cerr << "[test] before put" << std::endl;
    bool ok = engine.put("a", "1");
    assert(ok);

    std::cerr << "[test] before flush" << std::endl;
    ok = engine.flush();
    std::cerr << "[test] after flush" << std::endl;

    assert(ok);

    std::cerr << "[test] before epect1" << std::endl;
    expectValue("1");
    std::cerr << "[test] after epect1" << std::endl;

    std::cout << "[PASS] 首次落盘后可以查询\n";

    // 2. MemTable 中的新值应覆盖旧 SSTable。
    ok = engine.put("a", "2");
    assert(ok);
    expectValue("2");
    std::cout << "[PASS] MemTable 新值覆盖磁盘旧值\n";

    // 3. 最新 SSTable 应优先于旧 SSTable。
    ok = engine.flush();
    assert(ok);
    expectValue("2");
    std::cout << "[PASS] 新 SSTable 覆盖旧 SSTable\n";

    // 4. 最新 SSTable 的删除标记应屏蔽旧值。
    ok = engine.remove("a");
    assert(ok);

    ok = engine.flush();
    assert(ok);

    auto result = engine.get("a");
    assert(!result.has_value());
    std::cout << "[PASS] 删除标记屏蔽旧 SSTable 中的值\n";
}