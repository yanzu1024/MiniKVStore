# MiniKV Store

MiniKV Store 是一个基于 **C++17 和 CMake** 的本地持久化键值存储引擎。它使用追加日志保存写操作，以内存哈希索引定位最新记录，并通过容量受限的 LRU 缓存保存热点值。

项目不依赖 Qt 或第三方库，重点展示标准 C++ 容器、模板、RAII、智能指针、文件序列化、多线程同步、错误处理和自动化测试。

> MiniKV Store 是学习型嵌入式存储引擎，不是 Redis 的替代品，也不宣称达到生产数据库的崩溃一致性和分布式能力。

## 核心功能

- `SET`、`GET`、`DEL`、`EXISTS` 和前缀 `KEYS`；
- 支持引号字符串和转义字符；
- 支持基于毫秒时间戳的 TTL；
- 二进制追加日志（AOF）持久化；
- 启动时重放记录并重建哈希索引；
- 检测并移除未写完整的文件尾部；
- FNV-1a 校验值检测记录损坏；
- `unordered_map + list` 实现的泛型 LRU 缓存；
- 后台线程定期清理过期键；
- `COMPACT` 将有效记录写入临时文件并替换旧文件；
- 运行统计、缓存命中率和文件大小统计；
- 单元测试、恢复测试、TTL 测试和并发压力测试；
- 可选 ASan/UBSan 与 ThreadSanitizer；
- 独立基准测试目标。

## 项目结构

```text
MiniKVStore/
├── CMakeLists.txt
├── README.md
├── app/main.cpp
├── include/minikv/
│   ├── AppendOnlyStore.h
│   ├── CommandParser.h
│   ├── KeyValueStore.h
│   ├── LruCache.h
│   ├── Record.h
│   ├── Result.h
│   └── Status.h
├── src/
│   ├── AppendOnlyStore.cpp
│   ├── CommandParser.cpp
│   ├── KeyValueStore.cpp
│   ├── Record.cpp
│   └── Status.cpp
├── tests/
├── benchmarks/benchmark_store.cpp
├── examples/demo_commands.txt
└── docs/
    ├── PROJECT_WALKTHROUGH.md
    └── QUICK_START_CN.md
```

## 构建要求

- CMake 3.21+
- 支持 C++17 的 GCC、Clang 或 MSVC
- 线程库（由 CMake 的 `Threads` 包自动处理）

主要开发和验证环境为 Ubuntu 24.04 + GCC 13。

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

运行：

```bash
./build/minikv
```

指定数据文件和缓存容量：

```bash
./build/minikv \
  --data data/demo.aof \
  --cache 128 \
  --cleanup-ms 1000
```

也可以直接执行示例命令：

```bash
./build/minikv \
  --data data/demo.aof \
  < examples/demo_commands.txt
```

## 命令

| 命令 | 说明 | 示例 |
|---|---|---|
| `SET key value` | 写入或更新键值 | `SET language cpp` |
| `SET key value TTL seconds` | 写入带过期时间的值 | `SET code 123456 TTL 60` |
| `GET key` | 获取值 | `GET language` |
| `DEL key` | 删除键 | `DEL language` |
| `EXISTS key` | 判断键是否存在 | `EXISTS language` |
| `KEYS [prefix]` | 列出全部键或指定前缀的键 | `KEYS user:` |
| `STATS` | 显示引擎和缓存统计 | `STATS` |
| `COMPACT` | 压缩追加日志 | `COMPACT` |
| `HELP` | 查看帮助 | `HELP` |
| `EXIT` | 正常关闭程序 | `EXIT` |

含空格的值需要使用双引号：

```text
SET greeting "hello world"
SET quote "say \"hello\""
```

## 数据流

```text
SET
 ├─ 追加二进制记录到 AOF
 ├─ 更新 unordered_map 索引
 └─ 更新 LRU 缓存

GET
 ├─ 通过 unordered_map 找到 RecordMetadata
 ├─ 检查 TTL
 ├─ 查询 LRU
 └─ 未命中时按 valueOffset 从 AOF 读取

启动
 └─ 顺序扫描 AOF → 校验记录 → 重建最新索引
```

索引只保存文件偏移量、值长度和过期时间，不保存全部值；因此 LRU 缓存具有真实用途，而不是对内存字典再套一层重复缓存。

## 二进制记录格式

每条记录使用固定 28 字节头部，再跟随键和值：

| 字段 | 大小 |
|---|---:|
| Magic `MKV1` | 4 字节 |
| Version | 2 字节 |
| Operation | 1 字节 |
| Reserved | 1 字节 |
| Key size | 4 字节 |
| Value size | 4 字节 |
| Expire-at milliseconds | 8 字节 |
| Checksum | 4 字节 |
| Key | 变长 |
| Value | 变长 |

所有整数由代码显式按小端序写入，没有直接把带有内存填充的 C++ 结构体写进文件。校验值用于检测意外损坏，不用于安全或加密。

## 并发与资源管理

- `KeyValueStore` 使用 `std::shared_mutex` 保护索引；
- 多个读操作可并发，写入、删除和压缩需要独占锁；
- AOF 文件使用独立互斥锁保护定位和读写；
- LRU 缓存内部自带互斥锁；
- 后台清理线程使用 `condition_variable` 等待；
- 析构时设置停止标志、唤醒并 `join`，不使用 `detach`；
- `std::unique_ptr<AppendOnlyStore>` 表达存储后端的唯一所有权；
- 锁均由 `lock_guard`、`unique_lock` 或 `shared_lock` 通过 RAII 管理。

## Sanitizer

AddressSanitizer 和 UndefinedBehaviorSanitizer：

```bash
cmake -S . -B build-asan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMINIKV_ENABLE_SANITIZERS=ON
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

ThreadSanitizer：

```bash
cmake -S . -B build-tsan \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMINIKV_ENABLE_THREAD_SANITIZER=ON
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

两种 Sanitizer 不应在同一个构建目录中同时启用。

## 基准测试

```bash
./build/MiniKVBenchmark 20000
```

输出 SET/GET 吞吐、压缩后文件大小和缓存命中率。该程序是可重复的本地微基准，不应当作与成熟数据库对比的性能结论。

## 设计边界

- 写入后调用 `fstream::flush()`，但未直接调用操作系统 `fsync()`，因此不宣称抵御突然断电；
- 当前一次 `SET` 对应一次追加和刷新，未实现批处理和组提交；
- 压缩过程在引擎独占锁下同步执行；
- 不支持事务、复制、网络协议、访问控制和分布式一致性；
- `KEYS` 会扫描内存索引，适合演示和中小数据集；
- 单个键最大 4 KiB，单个值最大 16 MiB。

投递前快速验收见 [docs/QUICK_START_CN.md](docs/QUICK_START_CN.md)，类职责、数据结构和面试问答见 [docs/PROJECT_WALKTHROUGH.md](docs/PROJECT_WALKTHROUGH.md)。

