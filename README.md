# QtGitStudio

基于 **C++17 / Qt 6** 的桌面 Git 客户端。通过 `QProcess` 异步调用系统 `git`，覆盖分支、提交、合并、变基、stash、远程等常用工作流，并重点实现 **合并冲突的可视化解析与分步应用工作流**（扫描 → 标记解析 → ResolutionPlan → 原子写回 → stage 校验 → 再扫描）。

## 与 lazygit 的关系

本仓库是**独立的 Qt 客户端实现**，运行时只依赖系统安装的 `git`，**不链接、不调用** [lazygit](https://github.com/jesseduffield/lazygit) 的 Go 代码或二进制。开发过程中参考过开源 TUI 客户端的交互思路；请勿将本项目描述为 lazygit 的 fork 或「Qt 版 lazygit」。

## 功能概览

- 仓库打开 / 克隆、分支与提交图、diff 预览
- Merge / Rebase / Cherry-pick / Revert 过程中的冲突处理
- 冲突标记有限状态解析（含 diff3 `|||||||`）与按 hunk 选择 Ours / Theirs / Both
- 乐观并发保护：index OID、工作区哈希、会话 generation
- GitHub API（仓库 / Release）与 Windows Credential Manager 存 token
- Qt Test 自动化测试（含冲突 parser / service / resolver / workflow）

## 环境要求

- Qt 6.11+（Widgets / Network / Svg / Test）
- CMake 3.16+
- Ninja
- MinGW g++ 13+（或等价工具链）
- 系统已安装 `git`

## 构建

```bash
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt安装路径> ..
ninja
```

Windows 示例（路径按本机 Qt 安装调整）：

```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 ..
ninja
```

## 测试

```bash
ctest --output-on-failure
```

## 版本

当前工程版本见 `CMakeLists.txt`（`project(QtGitStudio VERSION …)`）。冲突应用工作流对应 RC5 阶段能力。

## License

MIT — 见 [LICENSE](LICENSE)。