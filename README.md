# Qt 文件传输工具（课程作业）

基于 **Qt 6 + QTcpSocket / QTcpServer** 的 TCP 文件传输工具：
客户端与服务端集于同一程序（一个实例"启动监听"当服务端，另一个实例"连接服务器"当客户端），
支持文本消息与任意文件（文本文件、图片等）可靠传输。

**详细开发步骤、技术解析与测试方法见 [开发步骤.md](开发步骤.md)。**

## 功能特性

- 自定义协议头（消息类型 + 文件名长度 + 文件名 + 数据大小 + 数据），三状态机分包组包，正确处理 TCP 粘包/拆包
- 客户端采用 `write + bytesWritten` 信号驱动的流水线发送，大文件内存占用恒定
- 鼠标拖拽文件进窗口即发送（客户端）/保存（服务端），拖入高亮反馈（`dragEnterEvent` 等虚函数重写）
- "连接设置"对话框通过自定义信号 `settingsApplied(AppSettings)` 与主窗口通信（跨窗口信号槽），设置用 QSettings 持久化
- 传输日志（着色、时间戳）、QProgressBar 进度条、状态栏提示、QMessageBox 错误框，异常不崩溃
- 全部界面使用 .ui + 布局管理器；QSS 美化（悬停变色、圆角、拖拽高亮）；图标经 .qrc 资源系统打包
- 事件过滤器示例（输入框聚焦自动全选）；接收文件先写 `.part`，完成后改名，同名自动加序号

## 编译运行

Qt 6.x + Qt Creator（或命令行 qmake）：

```bash
qmake FileTransfer.pro && make -j8    # Windows/MinGW: mingw32-make
./FileTransfer
```

## 使用方法（本地联调）

1. 启动实例 A，点 **启动监听**（默认 8888 端口）；
2. 启动实例 B，**连接设置** 填 `127.0.0.1:8888` → 确定 → 点 **连接服务器**；
3. B 中把文件拖进虚线框（或点"选择文件发送"），A 自动保存并在日志/进度条中反馈；
4. 完整性可用 `md5sum`（Windows `fc /b`）比对原文件与保存文件。

## 可选工具

- `test/`：协议自测程序（16KB 分包压力 + 连续发送粘包压力 + MD5 校验，输出 `SELFTEST PASS`）
- `tools/`：图标生成器（QPainter 手绘 PNG，改完图标重跑一次即可）

## 目录结构

```
FileTransfer.pro        # 工程文件（QT += network）
protocol.h              # 自定义协议定义
fileserver.*            # 服务端传输管理（分包组包状态机）
fileclient.*            # 客户端传输管理（流水线发送）
mainwindow.* .ui        # 主窗口（View）
settingsdialog.* .ui    # 连接设置对话框（跨窗口信号槽）
droparea.*              # 拖拽接收区（虚函数重写）
resources/              # resources.qrc + style.qss + icons/
test/  tools/           # 可选：自测程序、图标生成器
```
