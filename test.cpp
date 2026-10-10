#include "SSTable.hpp"
#include <cassert>
#include <iostream>

/*
# 创建临时目录，不创建文件
sst_test_dir=$(mktemp -d)

# 测试程序会创建目录中的 test.sst
./miniStoreEgine/build/sstable_test "$sst_test_dir/test.sst"

cmake --build miniStoreEgine/build --target sstable_test -j
*/

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "用法：ministore <新的 sst 文件路径>\n";
        return 1;
    }

    MemTable mem;
    mem.put("apple", "old");
    mem.put("apple", "red");       // 覆盖
    mem.put("banana", "yellow");
    mem.put("empty", "");          // 合法空值
    mem.put("deleted", "old");
    mem.remove("deleted");        // 删除标记，并没有实际删除，这份工作不是由他来完成的
    mem.put("text", "hello world\nnext line");
    mem.put("large", std::string(10000, 'x'));

    SSTable table(argv[1]);
    bool written = table.writeTable(mem);
    assert(written);

    auto expectValue = [&](const std::string& key,
                           const std::string& expected) {
        Record record{};
        auto status = table.look(key, record);
        assert(status == LookupState::Found);
        assert(!record.isdelete);
        assert(record.key == key);
        assert(record.value == expected);
    };

    // 故意不按顺序查询，验证每次查询都能正确定位。
    expectValue("banana", "yellow");
    expectValue("apple", "red");
    expectValue("empty", "");
    expectValue("text", "hello world\nnext line");
    expectValue("large", std::string(10000, 'x'));
    expectValue("apple", "red");   // 重复查询

    Record record{};
    auto status = table.look("deleted", record);
    assert(status == LookupState::Found);
    assert(record.isdelete);

    // 分别测试目标在所有记录之前、中间、之后的情况。
    for (const char* key : {"aaa", "carrot", "zzz"}) {
        status = table.look(key, record);
        assert(status == LookupState::NotFound);
    }



    std::cout << "基础读写测试通过\n";
}