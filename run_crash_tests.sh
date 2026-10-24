#!/usr/bin/env bash
#之所以适合用外部脚本组织，是因为测试进程已经故意崩溃了，它无法继续执行自己的后续恢复检查。
#需要一个仍然运行的外部程序负责再次启动它
set -euo pipefail

# Usage: ./run_crash_tests.sh /path/to/crash_compaction_test
BIN="${1:-/tmp/crash_compaction_test}"
POINTS=(after_write after_file_sync after_rename after_publish after_first_delete after_cleanup_sync)

for point in "${POINTS[@]}"; do
    dir=$(mktemp -d /tmp/ministore-crash.XXXXXX)
    echo "===== $point ====="
    "$BIN" "$dir" setup
    set +e
    MINISTORE_CRASH_AT="$point" "$BIN" "$dir" crash
    status=$?
    set -e
    if [[ "$status" -ne 99 ]]; then
        echo "[FAIL] expected injected exit 99, observed $status at $point"
        echo "Inspect test data: $dir"
        exit 1
    fi
    "$BIN" "$dir" verify || {
        echo "[FAIL] recovery test at $point; files kept in $dir"
        exit 1
    }
    echo "[PASS] $point"
    rm -rf -- "$dir"
done

echo "[PASS] All crash injection cases passed"
