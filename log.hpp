#pragma once

#include <cstdio>

enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3
};
static LogLevel CURRENT_LOG_LEVEL = LOG_DEBUG;


#define LOG_INFO(fmt, ...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LOG_INFO) { \
            printf("[INFO ] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_DEBUG(fmt, ...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LOG_DEBUG) { \
            printf("[DEBUG] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)

#define LOG_ERROR(fmt, ...) \
    do { \
        if (CURRENT_LOG_LEVEL >= LOG_ERROR) { \
            printf("[ERROR] " fmt "\n", ##__VA_ARGS__); \
        } \
    } while (0)
    
#define LOG_WARN(fmt, ...) \
        do { \
            if (CURRENT_LOG_LEVEL >= LOG_WARN) { \
                printf("[WARN] " fmt "\n", ##__VA_ARGS__); \
            } \
        } while (0)