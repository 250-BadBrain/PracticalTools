# PortChecker

一个主打命令行使用的跨平台端口排查工具。

## 功能

- 列出本地端口占用
- 按协议、状态、端口、关键词筛选
- 按端口、进程名、PID、地址查找
- 测试远程主机 TCP 端口是否可连接
- 支持 Windows / Linux 构建
- 提供浏览器 GUI 和实验性原生 GUI

## 使用

推荐使用 CLI：

```bash
portchecker list
portchecker list --state listen
portchecker list --port 443
portchecker list --filter nginx
portchecker test example.com 443
```

临时查看可使用浏览器 GUI：

```bash
portchecker gui
```

## 构建

```bash
build.bat release
build.bat all
```

```bash
sh build.sh release
sh build.sh all
```

`release` 构建常用发布产物，`all` 构建 CLI 和浏览器 GUI 的全部 Windows / Linux 架构。原生 GUI 依赖平台图形库，不作为主推形态。
