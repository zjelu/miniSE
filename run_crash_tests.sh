#!/usr/bin/env bash
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
