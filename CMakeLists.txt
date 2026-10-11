cmake_minimum_required(VERSION 3.16)

project(MiniStoreEngine LANGUAGES CXX)



add_executable(crash_compaction_test
    crash_compaction_test.cpp
    storeEngine.cc
    compaction.cc
    SSTable.cc
    memTable.cc
    WAL.cc
)

set_target_properties(crash_compaction_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(crash_compaction_test PRIVATE
    -Wall -Wextra -Wpedantic
)
target_compile_definitions(crash_compaction_test PRIVATE
    MINISTORE_CRASH_TEST
)


add_executable(compaction_test
    compaction_test.cpp
    storeEngine.cc
    compaction.cc
    SSTable.cc
    memTable.cc
    WAL.cc
)

set_target_properties(compaction_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(compaction_test PRIVATE
    -Wall -Wextra -Wpedantic
)


add_executable(sstable_test
    test.cpp
    SSTable.cc
    memTable.cc
)

set_target_properties(sstable_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(sstable_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)


add_executable(restart_test
    restart_test.cpp
    storeEngine.cc
    memTable.cc
    WAL.cc
    SSTable.cc
    compaction.cc
)

set_target_properties(restart_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(restart_test PRIVATE
    -Wall -Wextra -Wpedantic
)


add_executable(store_engine_test
    storeEngine_test.cpp
    storeEngine.cc
    memTable.cc
    WAL.cc
    SSTable.cc
)

set_target_properties(store_engine_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(store_engine_test PRIVATE
    -Wall -Wextra -Wpedantic
)


add_executable(ministore
    log.hpp
    memTable.cc
    storeEngine.cc
    WAL.cc
    SSTable.cc
)

set_target_properties(ministore PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_include_directories(ministore PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(ministore PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Build in a separate Release directory; crash injection is intentionally disabled.
add_executable(compaction_benchmark
    compaction_benchmark.cpp
    storeEngine.cc
    compaction.cc
    SSTable.cc
    memTable.cc
    WAL.cc
)
set_target_properties(compaction_benchmark PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_compile_options(compaction_benchmark PRIVATE -Wall -Wextra -Wpedantic)
