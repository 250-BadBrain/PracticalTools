# InvisibleCompiler

一个后台静默运行的隐形 C / C++ 编译器小工具。

## 功能

- 双击启动后静默监听当前目录
- 再次双击会关闭正在运行的监听进程
- 自动创建 `input.cpp`
- 保存 `input.cpp`、`input.cc`、`input.cxx` 或 `input.c` 后自动编译运行
- C++ 文件调用系统 `g++`
- C 文件调用系统 `gcc`
- 编译和运行结果写入 `output.txt`
- 运行超时时间为 3 秒

## 使用

把可执行文件放在一个目录中，双击运行。

再次双击同一个可执行文件，会静默关闭后台监听进程。

编辑同目录下的源码文件并保存：

```text
input.cpp
```

查看同目录下的输出文件：

```text
output.txt
```

## 构建

Windows：

```bat
build.bat
build.bat all
build.bat release
build.bat windows amd64
build.bat linux arm64
```

Linux / macOS：

```bash
sh build.sh
sh build.sh all
sh build.sh release
sh build.sh windows amd64
sh build.sh linux arm64
```

`all` 会构建全部 Windows / Linux 架构。`release` 只构建常用发布产物。

## 依赖

系统需要能在命令行中调用：

```text
gcc
g++
```
