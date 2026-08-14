# MiniKV Store 快速投递准备清单

这份清单用于先运行、验收、截图和提交 Git。完成这些步骤后，再通过 `PROJECT_WALKTHROUGH.md` 消化实现原理。

## 1. 构建测试

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

预期结果：

- `minikv` 编译成功；
- `MiniKVTests` 编译成功；
- `MiniKVBenchmark` 编译成功；
- 测试显示 `100% tests passed`；
- 编译输出没有警告。

## 2. 快速演示

先清理演示数据，确保第一次演示状态一致：

```bash
rm -f data/demo.aof
```

运行示例：

```bash
./build/minikv \
  --data data/demo.aof \
  < examples/demo_commands.txt
```

然后重新打开同一个数据文件：

```bash
./build/minikv --data data/demo.aof
```

输入：

```text
GET language
GET greeting
GET user:1002
STATS
EXIT
```

预期结果：

- `language` 返回 `cpp17`；
- `greeting` 返回 `hello world`；
- 已删除的 `user:1002` 返回 `(nil)`；
- `STATS` 中 `recovered records` 大于 0。

## 3. 建议截图

准备两张终端截图：

1. 执行示例命令，包含 `SET/GET/KEYS/STATS`；
2. 程序重启后仍能读取数据，体现持久化恢复。

如果要录制演示，推荐顺序：

```text
SET → GET → TTL → KEYS → STATS → COMPACT → EXIT → 重新启动 → GET
```

## 4. Git 提交

确认位于项目根目录后执行：

```bash
git init
git add .
git status
git commit -m "feat: implement persistent MiniKV storage engine"
```

确认 `build/`、`data/` 和 `.aof` 文件没有被提交。

## 5. 投递前最低理解要求

至少能够回答：

1. 为什么使用追加日志，而不是每次重写整个数据文件？
2. `unordered_map` 中保存的是值还是磁盘元数据？
3. `GET` 的完整执行流程是什么？
4. LRU 为什么需要同时使用 `list` 和 `unordered_map`？
5. `unique_ptr` 在项目中表达了什么所有权？
6. 后台线程如何安全退出？
7. TTL 如何避免返回过期数据？
8. 压缩为什么先写临时文件？
9. 截断尾部与校验失败为什么采用不同处理？
10. `flush()` 和 `fsync()` 有什么区别？

## 6. 简历描述候选

确认项目已经在你的环境中构建、测试和演示后使用：

> **MiniKV Store 持久化键值存储引擎｜C++17、CMake**  
> 基于二进制追加日志和内存哈希索引实现键值数据持久化与启动恢复，使用 `unordered_map` 与 `list` 实现泛型 LRU 缓存；支持 TTL、后台过期清理、日志压缩和运行状态统计，通过 RAII、智能指针和读写锁管理文件与并发资源，并使用恢复测试、并发压力测试及 Sanitizer 验证核心功能。

在理解前不要把它描述成“生产级数据库”或“高性能 Redis 替代品”。

