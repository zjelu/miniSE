#include "storeEngine.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <utility>


bool StoreEngine::put(std::string key, std::string value){
    std::ostringstream out;
    out << "PUT " << key << ' ' << value << '\n';
    std::string input = out.str();

    try{
       if (!wal_->append(input)) return false;
       if (!wal_->sync()) return false;

        memtable_.put(key,value);

        //如果memtable_太大，就自动flush
        if(memtable_.approximateMemoryUsage()>16*1024*1024)
        {
            if(flush()) return true;
        }
        LOG_DEBUG("StoreEngine::put sucessful");
        return true;

    }catch (const std::exception& e) {
    std::cerr << "[put] exception: " << e.what() << '\n';
    return false;
    }
    catch (...) {
        std::cerr << "[put] unknown exception\n";
        return false;
    }

}

bool StoreEngine::remove(std::string_view key){
    
    std::ostringstream out;
    out << "DEL " << key << '\n';
    std::string input = out.str();
   try{
   if (!wal_->append(input)) return false;
   if (!wal_->sync())  return false;

    memtable_.remove(key);
    return true;
   }catch(...){
    LOG_DEBUG("StoreEngine::remove failed");

    return false;
   }
}

std::optional<std::string> StoreEngine::
get(std::string_view key){
    //先查memtable，这一步会过滤掉deleted

    auto result = memtable_.lookup(key);
    switch (result.state) {
    case LookupState::Found:
        return result.value;
    case LookupState::Deleted:
        return std::nullopt;
    case LookupState::NotFound:
        break;
    }

    for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
        Record record{};
        auto status = it->table->look(key, record);

        switch (status) {
        case LookupState::Found:
        //这里的逻辑与memtable不一样，将delete归入了Found
            if (record.isdelete) {
                return std::nullopt;  // 删除标记屏蔽所有更旧的记录
            }
            return record.value;

        case LookupState::NotFound:
            continue;  // 这张表没有，继续查旧表

        case LookupState::Error:
            throw std::runtime_error("SSTable lookup failed");
        }
    }

     return std::nullopt;
}

bool StoreEngine::recover(){
    if (!wal_->rewindForReplay()) {
        return false;
    }

    std::string line;
    while(true){
        ReadResult status = wal_->readLine(line);

        //完整记录 → 解析 → 直接修改 memtable_ → 下一轮
       if(status == ReadResult::Line){

           LogRecord record = wal_->parse(line);
           
           switch(record.type){
               case(OpType::PUT):
               memtable_.put(record.key,record.value);
               break;
               
               case(OpType::DEL):
               memtable_.remove(record.key);
               break;
               
               case(OpType::INVALID):
            LOG_DEBUG("[OpType] type=INVALID");
               return false;
            }
        }

        //读取失败或记录不完整 → 返回失败
        if(status == ReadResult::EndOfFile) return true;
        if (status != ReadResult::Line) return false;
        
    }
    return false;
}

//开始把sstable中的数据都写入sstables_，writeTable实现写入文件
bool StoreEngine::flush() {
    auto pending = createNewTable();

    //把mentable转换成SSTable
    //写完SSTable，也要同步
    if (!pending.table->writeTable(memtable_)) {
        return false;
    }

    // 先确认文件内容持久化。
    if (!pending.table->sync()) {
        return false;
    }
    std::error_code ec;
    //不太理解，这里是修改了内存中的名字吗？rename如何影响到磁盘的呢？
    std::filesystem::rename(
        pending.temporary_path,
        pending.final_path,
        ec
    );

    if (ec) {
        std::cerr << "[ rename failed: "
                  << ec.message() << '\n';
        return false;
    }
    // 临时文件和正式文件都位于 table_dir_。
    // 确认改名这一目录变更持久化。
    if (!syncDirectory(table_dir_)) {
        return false;
    }

    sstables_.push_back(TableHandle{
        pending.final_path,
        std::move(pending.table)
    });

    memtable_.clear();
    return true;
}

PendingTable StoreEngine::createNewTable() {
    const auto name =
        "sst_" + formatId(next_file_id_++) + ".sst";

    const auto finalPath = table_dir_ / name;

    auto temporaryPath = finalPath;
    temporaryPath += ".tmp";

    //将table与一个实际文件相互绑定
    auto table = std::make_unique<SSTable>(
        temporaryPath.string(), OpenMode::CreateNew
    );

    return {
        std::move(table),
        temporaryPath,
        finalPath
    };
}


//补零函数
 std::string StoreEngine::formatId(uint64_t id) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%010llu", static_cast<unsigned long long>(id));
    return std::string(buf);
}


bool StoreEngine::open(const std::string& dir){

    namespace fs = std::filesystem;
    data_dir_ = fs::absolute(dir);
    table_dir_ = data_dir_ / "sstables";

    fs::create_directories(table_dir_);

    //这是为了同步新目录，但是有时候不能够保证它在父目录中的名字已经持久化
    syncDirectory(table_dir_);


    //首次创建wal，这里也要同步WAL 文件，以及所在目录
    wal_ = std::make_unique<WALLog>(
        (data_dir_ / "active.wal").string()
    );

    // 确认 WAL 文件持久化。
    if (!wal_->sync()) {
        
    std::cerr << "wal syncronise failed" <<'\n';
        return false;
    }

    // 确认 active.wal 的文件名持久化。
    if (!syncDirectory(data_dir_)) {
        return false;
    }

    std::cerr << "[open] data_dir=" << data_dir_ << '\n';

    std::vector<fs::path> files;
    next_file_id_ = 0;

    //现在为了实现只扫描一遍，把wal,sst文件的处理逻辑新放在了一个循环中
    for (const auto& entry : fs::directory_iterator(table_dir_)) {
        if (!entry.is_regular_file()) continue;
        
        if (entry.path().extension() != ".sst") continue;

        //处理id递增的逻辑
        const auto name = entry.path().filename().string();
        const auto digits = name.substr(4, 10);
        const auto id = static_cast<std::uint64_t>(std::stoull(digits));
        next_file_id_ = std::max(next_file_id_, id + 1);

        //把合适的文件放入files
        files.push_back(entry.path());

    }
    std::sort(files.begin(), files.end());


    for(auto& file_path : files){
    
        TableHandle table= TableHandle{
        file_path.string(),
        std::make_unique<SSTable>(
        file_path.string(),OpenMode::OpenExisting)
    };

        sstables_.push_back(std::move(table));
    }

   if (!recover()) {
    std::cerr << "MemTable recovery failed\n";
    return false;
    }

    return true;

}

//同步目录关系
bool StoreEngine::syncDirectory(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd == -1) return false;

    int result;
    do {
        result = ::fsync(fd);
    } while (result == -1 && errno == EINTR);

    ::close(fd);
    return result == 0;
}

//只负责合并
/*bool StoreEngine::merge(
    const std::vector<std::unique_ptr<SSTable>>& tables,
    EntryMap& output
    ){
        //重置偏移量，方便读取
        if(::lseek(fd_, 0, SEEK_SET)==-1) return false;

        for(auto& table :tables){
            while (true) {
                Record record{};
                auto result = table->readRecord(record);

                if (result == ReadResult::EndOfFile) {
                   break;
                }

                if (result != ReadResult::Ok) {
                    return false;
                }
                output.insert_or_assign(
                    record.key,
                    Entry{record.value, record.isdelete}
                );
            }
        }
    }*/



bool StoreEngine::compacte() {
    if (sstables_.size() < 2) return true;

    std::vector<std::filesystem::path> oldPaths;
    oldPaths.reserve(sstables_.size());

    for (const auto& tablehandle : sstables_) {
        oldPaths.push_back(tablehandle.path); // 复制路径
    }

    // 合并这些输入表,于是我们获得了一个merged
    if(!compaction.compacte(table_dir_)){
        std::cerr<<"compaction compacte failed\n";
        return false;
    } 

    // 写入并可靠发布新表。
    if(!writeFile(compaction.getMerged())) return false;
   
    // 切换查询列表、关闭旧表
    // 只要之后不再使用这些迭代器，就没有问题
    sstables_.erase(sstables_.begin(), sstables_.end() - 1);

    // 最后按 oldPaths 删除旧文件。
    bool cleanupOk = true;

    for (const auto& path : oldPaths) {
        std::error_code ec;
        std::filesystem::remove(path, ec);

        if (ec) {
            std::cerr << "[compaction] remove failed: "
                    << path << ": " << ec.message() << '\n';
            cleanupOk = false;
        }
    }

    if (!syncDirectory(table_dir_)) {
        cleanupOk = false;
    }

    return cleanupOk;
}


bool StoreEngine::writeFile(const EntryMap& output){
    auto pending = createNewTable();
    //尝试把merged
    if (!pending.table->writeEntries(output)) {
        return false;
    }
    // 先确认文件内容持久化。
    if (!pending.table->sync()) {
         std::cerr << "[ pending.table->sync failed\n";
        return false;
    }
    std::error_code ec;
    //不太理解，这里是修改了内存中的名字吗？rename如何影响到磁盘的呢？
    std::filesystem::rename(
        pending.temporary_path,
        pending.final_path,
        ec
    );

    if (ec) {
        std::cerr << "[ rename failed: "
                  << ec.message() << '\n';
        return false;
    }

    // 临时文件和正式文件都位于 table_dir_。
    // 确认改名这一目录变更持久化。
    if (!syncDirectory(table_dir_)) {
        std::cerr << "[ syncDirectory failed: "
                  << ec.message() << '\n';
        return false;
    }

    TableHandle table{pending.final_path, std::move(pending.table)};
    sstables_.push_back(std::move(table));
    return true;
}
