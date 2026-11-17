// 在 /workspace/IO 下编译（无需修改 CMakeLists.txt）：
// g++ -std=c++17 -g -O0 -I miniStoreEgine miniStoreEgine/compaction_test.cpp miniStoreEgine/storeEngine.cc miniStoreEgine/compaction.cc miniStoreEgine/SSTable.cc miniStoreEgine/memTable.cc miniStoreEgine/WAL.cc -o /tmp/compaction_test
// 使用全新目录，分别启动两个进程：
// test_dir=$(mktemp -d /tmp/compaction-test.XXXXXX)
// /tmp/compaction_test "$test_dir" write
// /tmp/compaction_test "$test_dir" reopen

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

std::vector<fs::path> tables(const fs::path& dir) {
    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sst")
            result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::stoull(a.filename().string().substr(4, 10)) <
               std::stoull(b.filename().string().substr(4, 10));
    });
    //std::sort(result.begin(), result.end());
    return result;
}

void expectEntries(const Entries& actual, const Entries& expected) {
    require(actual.size() == expected.size(), "记录数量不一致");
    for (const auto& [key, entry] : expected) {
        const auto it = actual.find(key);
        require(it != actual.end(), "缺少 key: " + key);
        require(it->second.isdelete == entry.isdelete, "删除标记错误: " + key);
        require(it->second.value == entry.value, "value 错误: " + key);
    }
}

void writeTable(const fs::path& path, const Entries& entries) {
    SSTable table(path.string(), OpenMode::CreateNew);
    require(table.writeEntries(entries), "创建测试表失败");
    require(table.sync(), "同步测试表失败");
}

// 直接读取磁盘文件，避免 MemTable 或完整 WAL 重放掩盖合并错误。
void expectDisk(const fs::path& dir, const Entries& expected) {
    const auto files = tables(dir);
    std::cerr << "[DEBUG] SSTable directory: "
              << dir << '\n';

    std::cerr << "[DEBUG] SSTable count: "
              << files.size() << '\n';

    for (const auto& file : files) {
        std::cerr << "[DEBUG] " << file.filename() << '\n';
    }

    require(files.size() == 1, "合并后应当恰好有一张 SSTable");
    SSTable table(files.front().string(), OpenMode::OpenExisting);
    Entries actual;
    std::string previous;
    bool first = true;
    while (true) {
        Record record{};
        const auto status = table.readRecord(record);
        if (status == ReadResult::EndOfFile) break;
        require(status == ReadResult::Ok, "合并文件存在无法读取的记录");
        require(first || previous < record.key, "输出 key 不严格递增或出现重复");
        first = false;
        previous = record.key;
        actual.emplace(record.key, Entry{record.value, record.isdelete});
    }
    expectEntries(actual, expected);
}

void expectQueries(StoreEngine& engine, const Entries& expected) {
    for (const auto& [key, entry] : expected) {
        const auto value = engine.get(key);
        if (entry.isdelete) {
            require(!value.has_value(), "已删除的 key 复活: " + key);
        } else {
            require(value.has_value() && *value == entry.value, "查询错误: " + key);
        }
    }
    require(!engine.get("missing-key").has_value(), "不存在的 key 应返回空");
}

const Entries finalEntries{
    {"a", {"v4", false}},
    {"deleted", {"", true}},
    {"empty", {"nonempty", false}},
    {"keep", {"stable", false}},
    {"reborn", {"again", false}}
};

void testOrder(const fs::path& root) {
    const auto dir = root / "order";
    fs::create_directories(dir);
    // 故意以 新、旧、中 的次序创建文件。目录实际枚举顺序由文件系统决定。
    writeTable(dir / "sst_0000000010.sst", {{"a", {"latest", false}}, {"gone", {"", true}}});
    writeTable(dir / "sst_0000000002.sst", {{"a", {"old", false}}, {"gone", {"old", false}}});
    writeTable(dir / "sst_0000000009.sst", {{"a", {"middle", false}}});
    // 未发布的 .tmp 文件不应参与合并。
    writeTable(dir / "sst_0000000011.sst.tmp", {{"a", {"wrong", false}}});
    Compaction compaction;
    // 适配流式接口：输入按编号排序，输出放到输入目录之外。
    auto mergeAndRead = [&](const fs::path& input, const fs::path& outputPath) {
        std::vector<TableHandle> inputs;
        for (const auto& path : tables(input)) {
            inputs.push_back(TableHandle{
                path, std::make_unique<SSTable>(path.string(), OpenMode::OpenExisting)
            });
        }
        SSTable output(outputPath.string(), OpenMode::CreateNew);
        require(compaction.compacte(inputs, output), "流式合并失败");
        require(output.sync(), "同步合并结果失败");
        require(output.rewind(), "重置输出读取位置失败");
        Entries actual;
        std::optional<std::string> previous;
        while (true) {
            Record record{};
            const auto status = output.readRecord(record);
            if (status == ReadResult::EndOfFile) break;
            require(status == ReadResult::Ok, "读取合并结果失败");
            require(!previous || *previous < record.key, "输出乱序或 key 重复");
            previous = record.key;
            actual.emplace(record.key, Entry{record.value, record.isdelete});
        }
        return actual;
    };
    expectEntries(mergeAndRead(dir, root / "order-result.sst"),
                  {{"a", {"latest", false}}, {"gone", {"", true}}});
    const auto empty = root / "empty-input";
    fs::create_directories(empty);
    require(mergeAndRead(empty, root / "empty-result.sst").empty(),
            "空输入不应残留上次合并结果");
    std::cout << "[PASS] 文件编号顺序、删除标记、输入过滤 tmp、重复调用空输入\n";
}

bool waitForSingleTable(
    const fs::path& dir,
    std::chrono::milliseconds timeout
) {
    const auto deadline =
        std::chrono::steady_clock::now() + timeout;

    while (std::chrono::steady_clock::now() < deadline) {
        if (tables(dir).size() == 1) {
            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10)
        );
    }

    return false;
}

void testBackgroundCompaction(const fs::path& root) {
    PausePoint point;
   const auto data = root / "background-engine";
    StoreEngine engine(data);
    engine.setPausePoint(&point);
    const auto dir = data / "sstables";

    // 1. 生成足够多的 SSTable，达到触发阈值
    for (int i = 0; i < 8; ++i) {
        require(engine.put("key" + std::to_string(i), "value"),
                "put failed");
        require(engine.flush(), "flush failed");
    }

    // 2. 等待后台合并完成
    require(
        waitForSingleTable(dir, std::chrono::seconds(5)),
        "后台 Compaction 未完成"
    );
    // 3. 检查 SSTable 数量
    require(engine.sstablesSize()==1,"没有把八个文件压缩成一个" );
    // 4. 验证所有 key 都能够正确查询
    for (int i = 0; i < 8; ++i) {
        require(engine.get("key"+std::to_string(i))=="value",std::to_string(i)+"查询错误");
    }
    // 5. 验证磁盘上的合并结果
}

void testBackgroundReopen(const fs::path& root) {
    StoreEngine engine(root / "background-engine");

    for (int i = 0; i < 8; ++i) {
        auto key = "key" + std::to_string(i);
        auto value = engine.get(key);

        require(
            value.has_value() && *value == "value",
            "后台合并后重启恢复失败: " + key
        );
    }
}

void testFlushDuringCompaction(
    const fs::path& root
){
   PausePoint point;  // 放在 engine 前面，保证比 engine 更晚析构
    const auto data = root / "background-engine";
    StoreEngine engine(data);
    engine.setPausePoint(&point); 

    const auto dir = data / "sstables";

    // 1. 生成足够多的 SSTable，达到触发阈值
    for (int i = 0; i < 8; ++i) {
        require(engine.put("key" + std::to_string(i), "value"),
                "put failed");
        require(engine.flush(), "flush failed");
    }
    //在后台完成 prepareCompactionLocked()、释放 state_mu_ 后，让后台线程暂停。
    // 主线程：
    engine.test_pause_point_->waitUntilReached();//等待后台线程到达暂停点,然后再次执行之后的release

    engine.put("a", "newest");    //然后 flush()，生成第 9 张表。
    engine.flush();
    engine.test_pause_point_->release();    //然后 flush()，生成第 9 张表。
    
    std::cerr<<engine.sstablesSize()<<std::endl;
    
}
//void testShutdownRecovery(const fs::path& root){}


void testShutDown(const fs::path& root){
    PausePoint point;  // 放在 engine 前面，保证比 engine 更晚析构
    const auto data = root / "background-engine";
    std::cerr << "[open] data_dir=" << data << '\n';
    auto engine = std::make_unique<StoreEngine>(data);
  
    engine->setPausePoint(&point); 

    const auto dir = data / "sstables";

    // 1. 生成足够多的 SSTable，达到触发阈值
    for (int i = 0; i < 8; ++i) {
        require(engine->put("key" + std::to_string(i), "value"),
                "put failed");
        require(engine->flush(), "flush failed");
    }

    point.waitUntilReached();//等待后台线程到达暂停点,然后再次执行之后的release
    std::thread closer([&]{
        engine.reset();
    });

    point.release();
    closer.join();

    std::cerr << "[open] data_dir=" << data << '\n';
    StoreEngine engine1(data);
    std::cerr<<engine1.sstablesSize()<<std::endl;

    for (int i = 0; i < 8; ++i) {
         require(engine1.get("key" + std::to_string(i))=="value","第"+std::to_string(i)+"失败");
    }

}


void testEngine(const fs::path& root) {
    const auto data = root / "engine";
    StoreEngine engine(data);
    const auto dir = data / "sstables";
    require(engine.compacte(), "空库合并失败");
    require(tables(dir).empty(), "空库合并不应生成文件");

    require(engine.put("a", "v1"), "put a 失败");
    require(engine.put("keep", "stable"), "put keep 失败");
    require(engine.put("deleted", "old"), "put deleted 失败");
    require(engine.put("reborn", "old"), "put reborn 失败");
    require(engine.flush(), "第一次 flush 失败");
    const auto single = tables(dir);
    require(single.size() == 1, "第一次 flush 应生成一张表");
    require(engine.compacte(), "单表合并失败");
    
    require(tables(dir) == single, "单表合并不应替换文件");

    require(engine.put("a", "v2"), "更新 a 失败");
    require(engine.remove("deleted"), "删除 deleted 失败");
    require(engine.remove("reborn"), "删除 reborn 失败");
    require(engine.flush(), "第二次 flush 失败");
    require(engine.put("a", "v3"), "再次更新 a 失败");
    require(engine.put("reborn", "again"), "删除后重建失败");
    require(engine.put("empty", "nonempty"), "写入空字符串失败");
    require(engine.flush(), "第三次 flush 失败");

    Entries expected = finalEntries;
    expected.at("a").value = "v3";
    expectQueries(engine, expected);
    const auto old = tables(dir);
    require(old.size() == 3, "第一次多表合并前应有三张表");
    std::cerr << "[TEST] Before manual compaction\n";
    require(engine.compacte(), "第一次多表合并失败");

    std::cerr << "[TEST] After manual compaction\n";

    expectQueries(engine, expected);
    expectDisk(dir, expected);

    std::cerr << "[DEBUG] First manual compaction completed\n";

    expectQueries(engine, expected);
    expectDisk(dir, expected);

    for (const auto& path : old)
        require(!fs::exists(path), "旧表未被删除: " + path.string());
    std::cout << "[PASS] 多版本覆盖、删除、删除后重建、空值、旧表清理、磁盘内容\n";

    const auto compacted = tables(dir);
    require(engine.compacte(), "重复合并失败");
    require(tables(dir) == compacted, "重复合并不应替换唯一的表");
    require(engine.put("a", "v4"), "合并后写入失败");
    require(engine.flush(), "合并后 flush 失败");
    require(tables(dir).size() == 2, "合并后 flush 应新增一张表");
    require(engine.compacte(), "第二轮多表合并失败");
    std::cerr << "[DEBUG] Second manual compaction completed\n";
expectQueries(engine, finalEntries);
expectDisk(dir, finalEntries);

    std::cout << "[PASS] 空库、单表、重复合并、合并后继续写入并再次合并\n";
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "用法: compaction_test <独立测试目录> <write|reopen>\n";
        return 2;
    }
    try {
        const auto root = fs::absolute(argv[1]);
        const std::string mode = argv[2];
        if (mode == "write") {
            require(!fs::exists(root) || fs::is_empty(root), "write 模式只接受全新空目录");
            fs::create_directories(root);
            testOrder(root);
            testEngine(root);//
            //testBackgroundCompaction(root);
            //testFlushDuringCompaction(root);
            testShutDown(root);
            std::cout << "[PASS] write 完成，请用相同目录运行 reopen\n";
        } else if (mode == "reopen") {
            require(fs::is_directory(root / "engine" / "sstables"), "缺少 write 阶段数据");
            expectDisk(root / "engine" / "sstables", finalEntries);
            StoreEngine engine(root / "engine");
            expectQueries(engine, finalEntries);

            testBackgroundReopen(root);
            std::cout << "[PASS] 独立进程重启后，磁盘内容和查询结果均正确\n";
        } else {
            throw std::runtime_error("模式必须是 write 或 reopen");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << '\n';
        return 1;
    }
}
