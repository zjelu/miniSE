#!/usr/bin/env bash
set -euo pipefail

BIN="miniStoreEgine/build-release/compaction_benchmark"
RUNS=5

# 永久保存小体积结果
RESULT_DIR="./benchmark-results"
mkdir -p "$RESULT_DIR"

for i in $(seq 1 "$RUNS"); do
    bench_dir=$(mktemp -d /tmp/ministore-bench.XXXXXX)

    echo "Running benchmark $i/$RUNS"

    if "$BIN" prepare "$bench_dir" 300000 256 4 disjoint \
            > "$RESULT_DIR/prepare_$i.log" 2>&1 &&
       "$BIN" run "$bench_dir" \
            > "$RESULT_DIR/run_$i.csv" \
            2> "$RESULT_DIR/run_$i.log"; then

        # 成功：清理本次生成的大文件
        rm -rf -- "$bench_dir"
    else
        echo "Benchmark failed. Data preserved: $bench_dir" >&2
        exit 1
    fi
done

echo "All benchmarks completed."
echo "Results saved in $RESULT_DIR"