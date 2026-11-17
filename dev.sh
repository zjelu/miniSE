#!/usr/bin/env bash


# 无论从哪个目录调用，都定位到项目根目录
#你的需求	命令
#配置 Debug 项目	./dev.sh config
#编译全部	./dev.sh build
#编译 compaction_test	./dev.sh target compaction_test
#编译 recoveryTest	./dev.sh target recoveryTest
#运行全部测试	./dev.sh test
#只运行 compaction_test	./dev.sh test-one compaction_test
#查看注册的测试	./dev.sh list
#使用 GDB 调试 compaction_test	./dev.sh debug compaction_test
#cd "$(dirname "$0")"

#!/usr/bin/env bash
set -euo pipefail

# 无论从哪个目录调用，都定位到项目根目录
cd "$(dirname "$0")"

BUILD_DIR="build"
JOBS=2
TARGET="${2:-compaction_test}"

case "${1:-}" in

    config)
        cmake -S . -B "$BUILD_DIR" \
            -DCMAKE_BUILD_TYPE=Debug
        ;;

    build)
        cmake --build "$BUILD_DIR" -j "$JOBS"
        ;;

    target)
        cmake --build "$BUILD_DIR" \
            --target "$TARGET" -j "$JOBS"
        ;;

    test)
        ctest --test-dir "$BUILD_DIR" \
            --output-on-failure
        ;;

    test-one)
        ctest --test-dir "$BUILD_DIR" \
            -R "^${TARGET}$" \
            --output-on-failure
        ;;

    list)
        ctest --test-dir "$BUILD_DIR" -N
        ;;

    debug)

        cmake --build "$BUILD_DIR" \
            --target "$TARGET" -j "$JOBS"

        mkdir -p "$BUILD_DIR/test-runs"

        test_dir=$(mktemp -d \
            "$PWD/$BUILD_DIR/test-runs/gdb-XXXXXX")

        case "$TARGET" in
            sstable_test)
                args=("$test_dir/test.sst")
                ;;
            compaction_test|restart_test)
                args=("$test_dir/data" write)
                ;;
            crash_compaction_test)
                args=("$test_dir/data" setup)
                ;;
            *)
                args=("$test_dir/data")
                ;;
        esac
        
        echo "Debug data: $test_dir"

        gdb -q \
            -ex "set debuginfod enabled off" \
            -ex "set disable-randomization off" \
            -ex "catch throw" \
           --args "./$BUILD_DIR/$TARGET" "${args[@]}"

        echo "调试目录保留在: $test_dir"
        ;;

    *)
        echo "Usage: ./dev.sh <command> [target]"
        echo "Commands:"
        echo "  config"
        echo "  build"
        echo "  target [target]"
        echo "  test"
        echo "  test-one [target]"
        echo "  list"
        echo "  debug [target]"
        exit 1
        ;;
esac