#pragma once
#include <string>
#include <sstream>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include "log.hpp"
//logrecord
//对应一个WAL文件

//目前问题，WAL无法支持空值还有空格
enum class OpType {
    PUT,
    DEL,
    INVALID
};

enum class ReadResult{
    Line,
    EndOfFile,
    Truncated,
    Error,
    Ok
};

struct LogRecord {
    OpType type = OpType::INVALID;
    std::string key;
    std::string value;  // Delete 不使用
};

class WALLog{
    public:
    // WAL：不存在就创建，存在就打开并追加
    explicit WALLog(const std::string& path);
    WALLog() = default;

    WALLog(const WALLog&) = delete;
    WALLog& operator=(const WALLog&) = delete;
    ~WALLog();

    //给文件中加文本
    bool append(const std::string& record);

    //重置
    bool rewindForReplay();//保留文件内容
    bool resetAfterFlush();//清空文件内容
    //从文件中读取原始文本
    ReadResult readLine(std::string& line);
    LogRecord parse( std::string_view input);

    //把文件在操作系统缓存中的修改同步到持久化存储
    bool sync();

    private:
    OpType to_type(std::string_view name);
    std::string_view next_token(std::string_view& input);



    std::string pending_;//存储未处理的内容
    bool reached_eof_ = false;
    int fd_ =-1 ;
};