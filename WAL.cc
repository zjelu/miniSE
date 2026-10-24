#include "WAL.hpp"

WALLog::WALLog(const std::string& path){
    fd_ = open(
        path.c_str(),
         O_RDWR | O_CREAT | O_APPEND,
        0644
    );

    if (fd_ == -1) {
        perror("open AOF");
    }
}
//单纯的把string写到文件中
bool WALLog::append(const std::string& record){
       std::string data = record;

    if (data.empty() || data.back() != '\n') {
        data.push_back('\n');
    }

    std::size_t written = 0;
    while (written < data.size()) {
        ssize_t n = write(
            fd_,
            data.data() + written,
            data.size() - written
        );

        if (n > 0) {
            written += static_cast<std::size_t>(n);
            continue;
        }

        if (n == -1 && errno == EINTR) {
            continue;
        }

        if (n == -1) {
            perror("write");
            return false;
        }

        // write() 返回 0：没有取得进展，避免死循环
        return false;
    }
    return true;
}

//parse函数无法处理空格与空值
//暂时只能够接受不含换行与空格的非空字符串
LogRecord WALLog::parse(std::string_view input){
//比如 put a 10
//Del a

 std::string_view command =
    next_token(input);

    OpType type = to_type(command);

    // SET key value
    if (type == OpType::PUT) {
       const std::string_view key =
            next_token(input);

        const std::string_view value =
            next_token(input);

        if (key.empty() || value.empty()) {
            return LogRecord{};
        }

        if (!next_token(input).empty()) {
            return LogRecord{};
        }

        return LogRecord{
            OpType::PUT,
            std::string(key),
            std::string(value)
        };
    }

      // 一个 key 的命令
    if (type == OpType::DEL )
    {
       std::string_view key =
        next_token(input);

        if (key.empty()) {
            return LogRecord{};
        }

        if (!next_token(input).empty()) {
            return  LogRecord{};
        }

        return LogRecord{type, std::string(key),{}};
    }
    return LogRecord{};
}

WALLog::~WALLog()
{
    if (fd_ != -1) {
        close(fd_);
    }
}

std::string_view WALLog::next_token(
    std::string_view& input
)
{
    // 跳过开头的空格
    while (!input.empty() &&
           input.front() == ' ')
    {
        input.remove_prefix(1);
    }

    if (input.empty()) {
        return {};
    }

    const std::size_t space =
        input.find(' ');

    if (space == std::string_view::npos)
    {
        std::string_view token = input;

        input = {};
        return token;
    }

    std::string_view token =
        input.substr(0, space);

    input.remove_prefix(space + 1);

    return token;
}

OpType WALLog::to_type(
    std::string_view name
)
{
    if (name == "PUT") {
        return OpType::PUT;
    }
    if (name == "DEL") {
        return OpType::DEL;
    }
    return OpType::INVALID;
}

ReadResult WALLog::readLine(std::string& line){
    line.clear();
    while(true){
        auto pos =pending_.find('\n');

        if(pos != std::string::npos){
            line.assign(pending_,0,pos);
            pending_.erase(0,pos+1);//性能存疑
            return ReadResult::Line;
        }

        if(reached_eof_){
            return pending_.empty()
            ?ReadResult::EndOfFile
            :ReadResult::Truncated;
        }

        //把文件中的内容读取到pending中
        char buffer[4096];
        ssize_t n = read(fd_,buffer,sizeof(buffer));

        if(n>0){
            pending_.append(buffer,static_cast<std::size_t>(n));
        }else if (n == 0) {
            reached_eof_ = true;
        } else if (errno != EINTR) {
            return ReadResult::Error;
        }
    }
}

bool WALLog::rewindForReplay() {
    if (lseek(fd_, 0, SEEK_SET) == -1) {
        return false;
    }

    pending_.clear();
    reached_eof_ = false;
    return true;
}

// WAL.cc
bool WALLog::resetAfterFlush() {
    // 清空文件内容；处理信号中断。
    while (::ftruncate(fd_, 0) == -1) {
        if (errno == EINTR) continue;

        perror("truncate WAL");
        return false;
    }

    // ftruncate 不会重置文件偏移，需要单独处理。
    if (::lseek(fd_, 0, SEEK_SET) == -1) {
        perror("seek WAL");
        return false;
    }

    // 丢弃读取旧日志时留下的缓冲状态。
    pending_.clear();
    reached_eof_ = false;

    // 确认文件长度变为 0 的修改已经持久化。
    return sync();
}

bool WALLog::sync() {
    while (::fsync(fd_) == -1) {
        if (errno == EINTR) {
            continue;
        }

        perror("fsync WAL");
        return false;
    }

    return true;
}