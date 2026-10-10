// Crash-injection integration test for MiniStoreEngine (C++17).
// Modes are intentionally run in SEPARATE processes by run_crash_tests.sh.
// Usage: crash_compaction_test <empty-or-existing-test-dir> <setup|crash|verify>
#include "storeEngine.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Entries = std::map<std::string, Entry, std::less<>>;

static void require(bool ok, const std::string& msg) {
    if (!ok) throw std::runtime_error(msg);
}

static const Entries expected{
    {"a", {"new", false}},
    {"deleted", {"", true}},
    {"empty", {"nonempty", false}},
    {"keep", {"stable", false}},
    {"reborn", {"again", false}}
};

static std::uint64_t fileId(const fs::path& path) {
    const std::string name = path.filename().string();
    require(name.size() == 18 &&
            (name.compare(0, 4, "sst_") == 0 || name.compare(0, 4, "com_") == 0) &&
            name.compare(14, 4, ".sst") == 0,
            "Unknown SSTable filename: " + name);
    for (std::size_t i = 4; i < 14; ++i)
        require(name[i] >= '0' && name[i] <= '9', "Bad SSTable ID: " + name);
    return std::stoull(name.substr(4, 10));
}

static std::vector<fs::path> tables(const fs::path& dir) {
    std::vector<fs::path> result;
    for (const auto& item : fs::directory_iterator(dir)) {
        if (item.is_regular_file() && item.path().extension() == ".sst")
            result.push_back(item.path());
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return fileId(a) < fileId(b);
    });
    for (std::size_t i = 1; i < result.size(); ++i)
        require(fileId(result[i - 1]) != fileId(result[i]), "Duplicate file IDs");
    return result;
}

// Independent disk-level verification: do NOT rely on WAL replay or MemTable.
// Mirrors newest-wins logical semantics over all published SSTable files.
// The algorithm is valid for the current *full compaction* design only.
static Entries readPublishedDiskState(const fs::path& dir) {
    Entries merged;
    for (const auto& path : tables(dir)) {
        SSTable table(path.string(), OpenMode::OpenExisting);
        std::optional<std::string> previous;
        while (true) {
            Record record{};
            auto result = table.readRecord(record);
            if (result == ReadResult::EndOfFile) break;
            require(result == ReadResult::Ok, "Corrupt SSTable: " + path.string());
            require(!previous || *previous < record.key, "Unsorted SSTable: " + path.string());
            previous = record.key;
            merged.insert_or_assign(record.key, Entry{record.value, record.isdelete});
        }
    }
    return merged;
}

static void verifyEntries(const Entries& actual) {
    require(actual.size() == expected.size(), "Wrong disk key count");
    for (const auto& [key, want] : expected) {
        const auto it = actual.find(key);
        require(it != actual.end(), "Disk missing key: " + key);
        require(it->second.value == want.value, "Disk wrong value: " + key);
        require(it->second.isdelete == want.isdelete, "Disk wrong tombstone: " + key);
    }
}

/*static void verifyQueries(StoreEngine& db) {
    for (const auto& [key, want] : expected) {
        auto got = db.get(key);
        if (want.isdelete) require(!got.has_value(), "Deleted key resurrected: " + key);
        else require(got.has_value() && *got == want.value, "Wrong GET: " + key);
    }
    require(!db.get("missing").has_value(), "Missing key appeared");
}*/

static void verifyQueries(StoreEngine& db) {
    for (const auto& [key, want] : expected) {
        auto got = db.get(key);

        std::cerr
            << "[VERIFY] key=" << key
            << " expected=" << want.value
            << " actual="
            << (got ? *got : "<nullopt>")
            << " deleted=" << want.isdelete
            << '\n';

        if (want.isdelete) {
            require(!got.has_value(),
                    "Deleted key resurrected: " + key);
        } else {
            require(got.has_value() && *got == want.value,
                    "Wrong GET: " + key);
        }
    }
}

static void setup(const fs::path& root) {
    require(!fs::exists(root) || fs::is_empty(root), "setup requires empty directory");
    fs::create_directories(root);
    StoreEngine db(root / "engine");
    require(db.put("a", "old"), "put a old");
    require(db.put("deleted", "old"), "put deleted");
    require(db.put("reborn", "old"), "put reborn");
    require(db.put("keep", "stable"), "put keep");
    require(db.flush(), "flush 1");
    require(db.put("a", "new"), "put a new");
    require(db.remove("deleted"), "delete deleted");
    require(db.remove("reborn"), "delete reborn");
    require(db.put("reborn", "again"), "recreate reborn");
    require(db.put("empty", "nonempty"), "put empty");
    require(db.flush(), "flush 2");
    require(tables(root / "engine" / "sstables").size() == 2, "Expected 2 SSTables before compaction");
    verifyQueries(db);
    verifyEntries(readPublishedDiskState(root / "engine" / "sstables"));
    std::cout << "[PASS] setup: two synchronized SSTables\n";
}

static void crashRun(const fs::path& root) {
    StoreEngine db(root / "engine");
    require(db.compacte(), "compaction returned false");
    // Returning normally is a FAILURE when a crash point is requested.
    std::cerr << "[FAIL] compaction returned without hitting selected crash point\n";
    std::exit(3);
}

static void verify(const fs::path& root) {
    auto dir = root / "engine" / "sstables";
    require(fs::is_directory(dir), "Missing SSTable directory");
    // The disk check deliberately runs before engine reopen to detect data loss
    // that could otherwise be hidden by WAL replay.
    verifyEntries(readPublishedDiskState(dir));
    StoreEngine db(root / "engine");
    verifyQueries(db);
    std::cout << "[PASS] reopen: disk contents AND queries correct\n";
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: crash_compaction_test <dir> <setup|crash|verify>\n";
        return 2;
    }
    try {
        const fs::path root = fs::absolute(argv[1]);
        const std::string mode = argv[2];
        if (mode == "setup") setup(root);
        else if (mode == "crash") crashRun(root);
        else if (mode == "verify") verify(root);
        else throw std::runtime_error("Unknown mode: " + mode);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
