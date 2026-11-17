#pragma once

#include <cstdio>

enum class LogLevel {
    Error = 0,
    Warn  = 1,
    Info  = 2,
    Debug = 3
};

// C++17 inline 变量
inline LogLevel CURRENT_LOG_LEVEL = LogLevel::Debug;

// 使用 __VA_ARGS__ 接收完整 printf 参数
#define LOG_INFO(...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LogLevel::Info) { \
            std::printf("[INFO ] "); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
        } \
    } while (0)

#define LOG_DEBUG(...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LogLevel::Debug) { \
            std::printf("[DEBUG] "); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
        } \
    } while (0)

#define LOG_ERROR(...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LogLevel::Error) { \
            std::printf("[ERROR] "); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
        } \
    } while (0)

#define LOG_WARN(...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LogLevel::Warn) { \
            std::printf("[WARN ] "); \
            std::printf(__VA_ARGS__); \
            std::printf("\n"); \
        } \
    } while (0)
