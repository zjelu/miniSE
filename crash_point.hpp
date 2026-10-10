#pragma once

#ifdef MINISTORE_CRASH_TEST

#include <cstdlib>
#include <string_view>
#include <unistd.h>

//条件编译，如无特殊要求，一般这个函数被替换成空函数
inline void crashPoint(std::string_view name) {
    const char* target = std::getenv("MINISTORE_CRASH_AT");

    if (target && name == target) {
        ::_exit(99);
    }
}

#else

inline void crashPoint(std::string_view) noexcept {}

#endif