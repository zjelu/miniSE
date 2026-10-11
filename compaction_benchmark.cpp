// Linux / C++17. See compaction_benchmark.md for Release build and usage.
#include "storeEngine.hpp"
#include <sys/resource.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Entries = std::map<std::string, Entry, std::less<>>;
struct Config { std::size_t keys, bytes, tables; std::string mode; };
void check(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}
std::size_t number(const std::string& text) {
    check(!text.empty() && text.find_first_not_of("0123456789") == std::string::npos,
          "Expected positive integer: " + text);
    const auto n = std::stoull(text);
    check(n > 0 && n <= std::numeric_limits<std::size_t>::max(), "Integer out of range");
    return static_cast<std::size_t>(n);
}
void validate(const Config& c) {
    check(c.keys > 0 && c.bytes > 0 && c.tables >= 2, "Require keys > 0, bytes > 0, tables >= 2");
    check(c.tables < 10000000000ULL, "Table ID exceeds filename width");
    check(c.bytes <= std::numeric_limits<std::uint32_t>::max(), "Value exceeds SSTable format");
    check(c.keys <= std::numeric_limits<std::size_t>::max() / c.tables, "Record count overflow");
    check(c.mode == "disjoint" || c.mode == "overwrite", "Mode must be disjoint or overwrite");
}
std::string padded(std::size_t n, int width) {
    std::ostringstream out;
    out << std::setw(width) << std::setfill('0') << n;
    return out.str();
}
std::string key(std::size_t n) { return "key_" + padded(n, 20); }
std::string value(std::size_t table, std::size_t bytes) {
    return std::string(bytes, static_cast<char>('A' + table % 26));
}
long rssKiB() {
    std::ifstream input("/proc/self/status");
    std::string line;
    while (std::getline(input, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream in(line.substr(6));
            long rss = -1;
            in >> rss;
            return rss;
        }
    }
    return -1;
}
long peakKiB() {
    rusage usage{};
    check(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage failed");
    return usage.ru_maxrss; // Linux: KiB, process lifetime peak.
}
void report(const std::string& stage, Clock::time_point start) {
    const double seconds = std::chrono::duration<double>(Clock::now() - start).count();
    std::cout << stage << ',' << std::fixed << std::setprecision(6) << seconds
              << ',' << rssKiB() << ',' << peakKiB() << '\n' << std::flush;
}
std::vector<fs::path> files(const fs::path& dir) {
    std::vector<fs::path> result;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ".sst") result.push_back(e.path());
    return result;
}
std::uintmax_t diskBytes(const std::vector<fs::path>& paths) {
    std::uintmax_t size = 0;
    for (const auto& path : paths) size += fs::file_size(path);
    return size;
}
void prepare(const fs::path& root, const Config& c) {
    validate(c);
    check(!fs::exists(root) || fs::is_empty(root), "prepare requires a new empty directory");
    fs::create_directories(root / "engine" / "sstables");
    const auto start = Clock::now();
    // Bound the map to approximately 1 MiB of payload or 1024 records.
    // A single record can exceed this budget if value_bytes itself is large.
    const auto batchLimit = std::max<std::size_t>(1,
        std::min<std::size_t>(1024, 1048576ULL / (static_cast<std::uint64_t>(c.bytes) + 128)));
    for (std::size_t t = 0; t < c.tables; ++t) {
        const auto path = root / "engine" / "sstables" / ("sst_" + padded(t, 10) + ".sst");
        SSTable table(path.string(), OpenMode::CreateNew);
        Entries batch;
        const auto payload = value(t, c.bytes);
        for (std::size_t i = 0; i < c.keys; ++i) {
            const auto id = c.mode == "disjoint" ? t * c.keys + i : i;
            batch.emplace(key(id), Entry{payload, false});
            if (batch.size() == batchLimit || i + 1 == c.keys) {
                check(table.writeEntries(batch), "Input SSTable write failed");
                batch.clear();
            }
        }
        check(table.sync(), "Input SSTable sync failed");
        std::cerr << "prepared_table=" << t + 1 << '/' << c.tables << '\n';
    }
    // Write metadata last so incomplete setup is not considered runnable.
    std::ofstream meta(root / "benchmark.txt");
    meta << c.keys << ' ' << c.bytes << ' ' << c.tables << ' ' << c.mode << '\n';
    meta.close();
    check(bool(meta), "Cannot write benchmark metadata");
    report("prepare", start);
}
Config readConfig(const fs::path& root) {
    Config c{};
    std::ifstream in(root / "benchmark.txt");
    check(bool(in >> c.keys >> c.bytes >> c.tables >> c.mode), "Missing/invalid benchmark metadata");
    validate(c);
    return c;
}
void run(const fs::path& root) {
    const auto c = readConfig(root);
    const auto dir = root / "engine" / "sstables";
    const auto before = files(dir);
    check(before.size() == c.tables, "Expected unmerged input; prepare a fresh directory per run");
    const auto wal = root / "engine" / "active.wal";
    check(!fs::exists(wal) || fs::file_size(wal) == 0, "Benchmark requires empty WAL");
    const auto unique = c.mode == "disjoint" ? c.keys * c.tables : c.keys;
    std::cout << "# mode=" << c.mode << " input_records=" << c.keys * c.tables
              << " unique_keys=" << unique << " value_bytes=" << c.bytes
              << " input_file_bytes=" << diskBytes(before) << '\n' << std::flush;
    auto start = Clock::now();
    {
        StoreEngine engine(root / "engine");
        report("open", start);
        start = Clock::now();
        std::cout << "# starting compaction\n" << std::flush;
        check(engine.compacte(), "Compaction returned false");
        report("compact", start); // Includes write, fsync, rename and old-file cleanup.
        const auto after = files(dir);
        check(after.size() == 1, "Expected one merged SSTable");
        for (const auto& path : before) check(!fs::exists(path), "Old SSTable survived");
        std::cout << "# output_file_bytes=" << diskBytes(after) << '\n';
        // Verify outside compaction timing, streaming one record at a time.
        start = Clock::now();
        SSTable output(after.front().string(), OpenMode::OpenExisting);
        for (std::size_t i = 0; i < unique; ++i) {
            Record record{};
            check(output.readRecord(record) == ReadResult::Ok, "Missing/corrupt output record");
            check(record.key == key(i) && !record.isdelete, "Wrong key/order/tombstone");
            const auto source = c.mode == "disjoint" ? i / c.keys : c.tables - 1;
            check(record.value.size() == c.bytes &&
                  record.value.find_first_not_of(static_cast<char>('A' + source % 26)) == std::string::npos,
                  "Wrong output value");
        }
        Record extra{};
        check(output.readRecord(extra) == ReadResult::EndOfFile, "Unexpected extra output");
        report("verify", start);
        start = Clock::now();
    }
    report("destroy_engine", start);
    std::cout << "# PASS\n";
}
int main(int argc, char** argv) {
    try {
        check(argc >= 3,
              "Usage: compaction_benchmark prepare DIR KEYS_PER_TABLE VALUE_BYTES TABLES disjoint|overwrite\n"
              "       compaction_benchmark run DIR");
#ifndef NDEBUG
        throw std::runtime_error("Use a Release build for this benchmark");
#endif
#ifdef MINISTORE_CRASH_TEST
        throw std::runtime_error("Crash injection must be disabled for benchmark");
#endif
        std::cout << "stage,seconds,rss_kib,process_peak_rss_kib\n";
        const std::string mode = argv[1];
        const auto root = fs::absolute(argv[2]);
        if (mode == "prepare") {
            check(argc == 7, "prepare requires DIR KEYS_PER_TABLE VALUE_BYTES TABLES MODE");
            prepare(root, Config{number(argv[3]), number(argv[4]), number(argv[5]), argv[6]});
        } else {
            check(mode == "run" && argc == 3, "Expected run DIR");
            run(root);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[FAIL] " << e.what() << '\n';
        return 1;
    }
}
