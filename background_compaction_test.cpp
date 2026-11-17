#include "storeEngine.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>

using namespace std::chrono_literals;

// 不使用 assert，避免 Release 构建关闭检查。
// 失败时直接结束测试进程，避免暂停中的后台线程使析构卡住。
void require(bool ok, const std::string& message) {
    if (!ok) {
        std::cerr << "[FAIL] " << message << '\n';
        std::abort();
    }
}

void expectValue(StoreEngine& engine, const std::string& expected) {
    const auto value = engine.get("key");
    require(value.has_value(), "key is missing");
    require(*value == expected, "unexpected value: " + *value);
}

int main(int argc, char* argv[]) {
    require(argc == 2,
            "usage: background_compaction_test <data-directory>");

    // 同步对象必须比 engine 活得更久。
    std::promise<void> snapshotReady;
    auto snapshotFuture = snapshotReady.get_future();

    std::promise<void> resume;
    auto resumeFuture = resume.get_future().share();

    std::promise<bool> completed;
    auto completedFuture = completed.get_future();

    std::atomic<bool> firstSnapshot{true};
    std::atomic<bool> firstCompletion{true};

    EngineTestHooks hooks;

    hooks.after_compaction_snapshot = [&] {
        if (firstSnapshot.exchange(false)) {
            snapshotReady.set_value();
            resumeFuture.wait();
        }
    };

    hooks.after_background_compaction = [&](bool ok) {
        if (firstCompletion.exchange(false)) {
            completed.set_value(ok);
        }
    };

    {
        StoreEngine engine(argv[1], std::move(hooks));

       for (int i = 1; i <= 8; ++i) {
    const std::string value = "seed" + std::to_string(i);
    require(engine.put("key", value), "seed put failed");
    require(engine.flush(), "seed flush failed");
}


        // 确认后台确实已经取得输入快照，而不是仍在等待。
        require(
            snapshotFuture.wait_for(30s) == std::future_status::ready,
            "background compaction did not reach snapshot point"
        );

        // 此时后台暂停，输入快照中最新版本是 v2。
       expectValue(engine, "seed8");

        // 前台继续更新并 flush，生成不属于旧快照的新 SSTable。
        require(engine.put("key", "v3"), "put v3 failed");
        expectValue(engine, "v3");

        require(engine.flush(), "flush v3 failed");
        expectValue(engine, "v3");

        // 再保留一个仅在 memtable 和 WAL 中的新版本。
        require(engine.put("key", "v4"), "put v4 failed");
        expectValue(engine, "v4");

        // 允许后台发布旧快照的合并结果。
        resume.set_value();

        require(
            completedFuture.wait_for(30s) == std::future_status::ready,
            "background compaction did not finish"
        );
        require(completedFuture.get(), "background compaction failed");

        // 旧快照发布后，不能覆盖前台的新值。
        expectValue(engine, "v4");

        // 离开作用域，析构函数停止并 join 后台线程。
    }

    {
        // 使用同一数据目录重新打开。
        StoreEngine reopened(argv[1]);
        expectValue(reopened, "v4");
    }

    std::cout << "[PASS] background compaction and reopen\n";
}