# 性能基准：提交图加载与布局

本文数据全部来自仓库内的基准测试 [`tests/test_perf_commit_graph.cpp`](../tests/test_perf_commit_graph.cpp) 的**真实运行结果**，没有估算值。

## 测什么

测试用 `git fast-import` 生成合成仓库：`main` 上 N 个线性提交，每 250 个提交拉出一条 3 个提交的 feature 分支，并在 10 个提交后合并回 `main`。这样图里既有分叉也有合并，不是一条直线。

| 列 | 含义 |
|---|---|
| load | `GitCommitGraphService::load`：执行 `git log --all --date-order --decorate=full` 并解析（3 次取中位数） |
| parse | 只算 `GitCommitGraphService::parseLog` 的纯解析时间（3 次取中位数） |
| graph60 | `CommitGraphWidget::setCommits`：默认视图，只渲染最近 60 个提交 |
| graphAll | 点「查看全部」：对**全部**提交做车道分配，并创建场景图元 |
| relation | `BranchRelationWidget::refresh`：分支关系图（读分支 + 读提交 + 建场景） |
| generate | 生成测试仓库的耗时（只作参考，不属于 QtGitStudio） |

所有耗时单位都是毫秒，Release 构建，`QT_QPA_PLATFORM=offscreen`。这些数字**不含**屏幕绘制（paint）时间。

## 结果

### GitHub Actions `ubuntu-24.04`（Qt 6.8.3，git 2.55.0）

CI 运行：<https://github.com/Lin32014012/QtGitStudio/actions/runs/39785399175>

| 提交数 N | 实际提交数（含 feature） | load | parse | graph60 | graphAll | relation | generate |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 1,009 | 8.6 | 0.8 | 1.6 | 26.5 | 19.0 | 161 |
| 5,000 | 5,057 | 33.7 | 4.3 | 1.6 | 137.9 | 67.4 | 881 |
| 10,000 | 10,117 | 68.1 | 8.6 | 1.6 | 293.2 | 141.9 | 1621 |

### 开发机（8 vCPU Intel Xeon，Debian 13，Qt 6.8.2，git 2.47.3）

| 提交数 N | 实际提交数 | load | parse | graph60 | graphAll | relation | generate |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,000 | 1,009 | 8.1 | 1.0 | 2.3 | 35.6 | 21.0 | 33 |
| 5,000 | 5,057 | 35.2 | 5.9 | 2.3 | 233.8 | 76.5 | 445 |
| 10,000 | 10,117 | 70.5 | 11.0 | 2.2 | 419.9 | 149.5 | 528 |
| 20,000 | 20,237 | 141.1 | 21.9 | 2.0 | 803.3 | 325.4 | 794 |

## 结论

1. **耗时随提交数线性增长**：提交数翻倍，load、parse、graphAll、relation 也大约翻倍，没有发现 O(n²) 的热点。车道分配和索引都用 `QHash`，属于 O(n)。所以这次**没有做算法改动**，也就没有改动前后的对比数据。
2. **默认视图几乎不受仓库大小影响**：只画最近 60 个提交，在 10k 提交的仓库里大约 2 ms。
3. **瓶颈在「查看全部」**：10k 提交时要 0.3–0.4 秒，主要花在创建 `QGraphicsItem` 上（每个提交 4 个以上图元），而且是在 UI 线程上同步执行。`load` 同样是同步调用 `git log`。

## 后续可以优化的方向

- 把 `git log` 和解析挪到工作线程（`QtConcurrent` 或复用 `executeAsync`），避免界面卡顿。
- 「查看全部」改成按视口懒加载或分页创建图元，只为可见行创建图元。
- 给 `git log` 加 `--max-count`，并做增量加载。

## 自己复现

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
# 默认测 1000,5000,10000；可以用环境变量改
QT_QPA_PLATFORM=offscreen QGS_PERF_SIZES=1000,5000,10000,20000 ./build/tests/test_perf_commit_graph
# 或者通过 ctest 标签运行
ctest --test-dir build -L perf -V
```

每次 CI 也会运行这个基准，结果表格会显示在该次运行的 Job Summary 里。
