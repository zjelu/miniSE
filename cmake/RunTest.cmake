cmake_minimum_required(VERSION 3.16)

# 检查 CMakeLists.txt 传进来的参数。
foreach(required IN ITEMS TEST_PROGRAM TEST_NAME TEST_KIND TEST_ROOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing argument: ${required}")
    endif()
endforeach()

if(NOT EXISTS "${TEST_PROGRAM}")
    message(FATAL_ERROR
        "Test executable not found: ${TEST_PROGRAM}\n"
        "Build the project before running CTest."
    )
endif()

# 每次执行都生成独立目录。
# 保留测试数据，失败后可以检查。
file(MAKE_DIRECTORY "${TEST_ROOT}")

string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef suffix)
set(run_dir "${TEST_ROOT}/${TEST_NAME}-${suffix}")

if(EXISTS "${run_dir}")
    message(FATAL_ERROR "Test directory already exists: ${run_dir}")
endif()

file(MAKE_DIRECTORY "${run_dir}")

message(STATUS "Test directory: ${run_dir}")

# 执行测试程序；非零退出码或进程异常都视为失败。
function(run_checked)
    execute_process(
        COMMAND "${TEST_PROGRAM}" ${ARGV}
        WORKING_DIRECTORY "${run_dir}"
        RESULT_VARIABLE result
    )

    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR
            "${TEST_NAME} failed: ${result}\n"
            "Test data retained at: ${run_dir}"
        )
    endif()
endfunction()

if(TEST_KIND STREQUAL "restart")
    # 两个独立进程，共用同一个数据目录。
    # write 失败时会立即停止，不执行 reopen。
    run_checked("${run_dir}/data" write)
    run_checked("${run_dir}/data" reopen)

elseif(TEST_KIND STREQUAL "engine")
    run_checked("${run_dir}/data")

elseif(TEST_KIND STREQUAL "sstable")
    # 只创建父目录，文件由 OpenMode::CreateNew 创建。
    run_checked("${run_dir}/test.sst")

else()
    message(FATAL_ERROR "Unknown test kind: ${TEST_KIND}")
endif()