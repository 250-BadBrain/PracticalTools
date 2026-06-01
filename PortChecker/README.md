# PortChecker

一个使用 Go 编写的跨平台端口检查工具。

## 功能

- 列出正在使用的本地端口
- 支持按协议、状态、端口、关键词筛选
- 支持按端口、进程名、PID、地址查找
- 支持测试远程主机 TCP 端口是否可连接
- 支持精简的本地浏览器 GUI
- 支持 Fyne 原生桌面 GUI
- GUI 支持浅色、深色和跟随系统主题
- 支持按 CLI / 浏览器 GUI / 原生 GUI 模式构建
- 支持构建 Windows / Linux 的 64 位、32 位和 ARM 版本

## 使用

直接运行程序会根据构建模式启动默认界面：

```bash
portchecker
```

也可以显式启动 GUI 或终端菜单：

```bash
portchecker gui
portchecker menu
```

也可以使用命令行参数：

```bash
portchecker list
portchecker list --proto tcp
portchecker list --state listen
portchecker list --port 443
portchecker list --filter 443
portchecker find nginx
portchecker test example.com 443
```

## 构建

Windows：

```bat
build.bat
build.bat cli windows amd64
build.bat browser windows amd64
build.bat browser linux arm64
build.bat native windows amd64
build.bat all
build.bat release
```

Linux / macOS：

```bash
sh build.sh
sh build.sh cli windows amd64
sh build.sh browser windows amd64
sh build.sh browser linux arm64
sh build.sh native linux amd64
sh build.sh all
sh build.sh release
```

不带参数运行构建脚本时，会先选择构建模式，再选择操作系统和架构。

`all` 会构建 CLI 和浏览器 GUI 的全部 Windows / Linux 架构，并构建当前环境支持的原生 GUI。`release` 只构建常用发布产物。

构建产物：

```text
dist/portchecker-browser-windows-amd64.exe
dist/portchecker-cli-linux-amd64
dist/portchecker-native-windows-amd64.exe
```

原生 GUI 使用平台图形库，默认需要在目标系统和架构上编译。跨平台发布优先使用 `cli` 或 `browser` 模式。

Linux 原生 GUI 使用同一套 `mode_native.go` 代码，不需要另写一套界面，但需要在 Linux 上编译。不同架构的同一操作系统复用同一套原生 GUI 代码，构建时选择对应架构即可。

Windows 原生 GUI 构建会使用 GUI 子系统，双击启动时不会显示 cmd 窗口。Linux 原生 GUI 从桌面环境启动时不会显示终端；如果从终端执行，则终端仍然会保留在调用进程中。

体积参考：

```text
CLI：约 3MB
浏览器 GUI：约 9MB
原生 GUI：约 25MB
```

## 文件

- `portchecker.go`：Go 主程序
- `command_windows.go`：Windows 隐藏系统命令窗口的执行封装
- `command_other.go`：非 Windows 系统命令执行封装
- `mode_browser.go`：浏览器 GUI 模式
- `mode_cli.go`：CLI 模式
- `mode_native.go`：Fyne 原生 GUI 公共界面
- `mode_native_windows.go`：Windows 原生 GUI 入口
- `mode_native_linux.go`：Linux 原生 GUI 入口
- `mode_native_unsupported.go`：未支持系统的原生 GUI 提示
- `build.bat`：Windows 构建脚本
- `build.sh`：Linux / macOS 构建脚本
