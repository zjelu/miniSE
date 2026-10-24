cmake_minimum_required(VERSION 3.16)

project(MiniStoreEngine LANGUAGES CXX)

# ---------- 基础配置 ----------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# INTERFACE 库只用于传递配置，不生成实际库文件。
add_library(ministore_options INTERFACE)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ministore_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

if(ENABLE_TSAN)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "TSan requires GCC or Clang")
    endif()

    target_compile_options(ministore_options INTERFACE
        -fsanitize=thread
        -g
        -O1
        -fno-omit-frame-pointer
    )

    target_link_options(ministore_options INTERFACE
        -fsanitize=thread
    )
endif()

# ---------- 引擎库 ----------

set(MINISTORE_SOURCES
    storeEngine.cc
    compaction.cc
    SSTable.cc
    memTable.cc
    WAL.cc
)

function(add_ministore_library target)
    add_library(${target} STATIC ${MINISTORE_SOURCES})

    target_include_directories(${target} PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )

    target_link_libraries(${target} PUBLIC
        ministore_options
        Threads::Threads
    )
endfunction()

# 普通版本
add_ministore_library(ministore_core)

# 故障注入版本：必须带宏重新编译引擎源码。
add_ministore_library(ministore_core_crash)

target_compile_definitions(ministore_core_crash PUBLIC
    MINISTORE_CRASH_TEST
)

# ---------- 创建可执行程序 ----------

function(add_ministore_program target source library)
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE ${library})
endfunction()

add_ministore_program(
    compaction_test compaction_test.cpp ministore_core
)

add_ministore_program(
    restart_test restart_test.cpp ministore_core
)

add_ministore_program(
    store_engine_test storeEngine_test.cpp ministore_core
)

add_ministore_program(
    sstable_test test.cpp ministore_core
)

add_ministore_program(
    crash_compaction_test
    crash_compaction_test.cpp
    ministore_core_crash
)

add_ministore_program(
    compaction_benchmark
    compaction_benchmark.cpp
    ministore_core
)

# ---------- 注册自动测试 ----------

enable_testing()

# kind 决定如何给测试程序传参数：
# restart：先 write，再 reopen
# engine ：传入一个新的数据目录
# sstable：传入一个尚不存在的 SSTable 文件路径
function(register_ministore_test target kind)
    add_test(
        NAME ${target}
        COMMAND "${CMAKE_COMMAND}"
            "-DTEST_PROGRAM=$<TARGET_FILE:${target}>"
            "-DTEST_NAME=${target}"
            "-DTEST_KIND=${kind}"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/test-runs"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunTest.cmake"
    )
endfunction()

register_ministore_test(compaction_test restart)
register_ministore_test(restart_test restart)
register_ministore_test(store_engine_test engine)
register_ministore_test(sstable_test sstable)

# crash_compaction_test 仍由原来的崩溃测试脚本驱动。
# compaction_benchmark 是性能测试，不注册进普通 CTest。