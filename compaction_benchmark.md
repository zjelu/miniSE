# Compaction benchmark（Linux，Release）

此 benchmark 测量真实的 `StoreEngine::compacte()`，包含扫描、合并、写文件、同步、发布和删除旧表。
它直接分批生成 SSTable，**不测量 put、WAL 或端到端写入吞吐量**；准备数据的 map 大致限制在 1 MiB payload 或 1024 条记录（单条 value 超大时除外）。不修改引擎实现。

## 构建

从 `/workspace/IO` 执行：

```bash
cmake -S miniStoreEgine -B miniStoreEgine/build-release -DCMAKE_BUILD_TYPE=Release
cmake --build miniStoreEgine/build-release --target compaction_benchmark -j 1
```

保留原有 Debug 构建。benchmark 在未定义 NDEBUG 时拒绝运行；目标没有启用 crash 注入。

## 一次测量

```bash
bench_dir=$(mktemp -d /tmp/ministore-bench.XXXXXX)
miniStoreEgine/build-release/compaction_benchmark prepare "$bench_dir" 10000 256 4 disjoint
miniStoreEgine/build-release/compaction_benchmark run "$bench_dir" > "$bench_dir/result.csv" 2> "$bench_dir/run.log"
cat "$bench_dir/result.csv"
```

prepare 参数依次是：目录、每张表记录数、value 字节数、表数量、分布模式。目录必须为空。
上述例子输入 4 张表，每张 10000 条、value 256 B；最终 40000 个不同 key。

- `disjoint`：各表 key 不重复，合并结果大小随总记录数增长，适合找内存边界。
- `overwrite`：各表使用相同 key，不同表写入不同内容，最终只保留每张表记录数那么多个 key，适合比较历史覆盖更新。
- key 为固定 24 字节；无空值、空格、换行或删除，删除正确性由 compaction_test 单独覆盖。
- 每次 run 会合并并删除输入旧表，因此每轮都要在新目录 prepare；不要重复 run 同一个已合并目录。
- 数据和结果会保留，不自动删除。大规模多轮测量也要注意磁盘空间。

## 指标

输出含 `#` 注释；作为 CSV 导入时跳过注释行。

- `open`：打开引擎耗时。WAL 为空，避免重放生成一个额外的大 MemTable。
- `compact`：完整合并耗时，以及合并返回后的 RSS。
- `verify`：逐条读取输出，校验数量、顺序、key、value、删除标记；不计入 compact 时间，也不保存完整结果集。
- `destroy_engine`：销毁引擎的耗时和之后的 RSS。
- `rss_kib`：阶段结束时的当前驻留内存。
- `process_peak_rss_kib`：该进程截至此刻的累计 RSS 峰值（Linux getrusage），不是阶段峰值增量。compact 行包含 open 和 compact 两阶段，排除了 prepare 和后续 verify。
- KiB / 1024 = MiB。RSS 不等于容器总占用，不包括所有文件缓存，也不包含被换出的页面。交换压力下不能仅靠 RSS 判断实际内存需求。
- 进程退出或清空 map 才释放对象；分配器可能缓存已释放空间，因此 destroy_engine 后 RSS 不一定立刻下降。

## 找边界

先固定 4 张表、256 B value，把每表记录数从 10000 提升到 25000、50000、100000，逐轮手动运行并记录 compact 时间和峰值，不自动运行到 OOM。
随后可以固定记录数，比较 256 B 和 1024 B value。每次只改变一个参数。
同时查看 `free -h`、Swap 和 `memory.events`。可用内存持续下降或 Swap 明显增加时，已接近当前环境的实用边界；不必等编辑器再次被杀。
若被 SIGKILL，退出码通常为 137，但单凭此不能断言 OOM，要结合内核日志或 oom_kill 计数变化。
真正强制探测 OOM 应使用与编辑器隔离、具有内存限制的容器或 cgroup，避免系统选择杀掉扩展宿主。

输入文件刚生成后可能在页缓存中，属于偏热缓存测量。记录表数、唯一 key 数、value 长度、文件大小、后台负载和 Swap 使用量，后续流式版本使用相同参数比较。
