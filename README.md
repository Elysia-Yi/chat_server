# 高性能 HTTP 服务器与在线聊天室

基于 C++17 与 Linux epoll 从零实现的 HTTP/1.1 服务器，附带一个可直接使用的在线聊天室；
网络层之上接入本项目自带的**双缓冲异步日志库**（源码就在 `log/` 目录，随本项目一起编译），
用于在不阻塞事件循环的前提下记录运行日志。

## 模块划分

| 文件 | 职责 |
| :--- | :--- |
| `io_context.hpp` | Reactor 事件循环：`epoll_pwait2` 分发就绪事件；`async_file` 把 read/write/accept/connect 封装成异步回调；`request_stop()` 支持优雅退出 |
| `timer_context.hpp` | 定时器：有序 multimap 维护到期时间，`set_timeout` 注册回调，`stop_source` 可提前触发或取消 |
| `stop_source.hpp` | 「停止信号」句柄：与 `callback` 绑定，用于取消挂起的 I/O 或提前唤醒定时器 |
| `callback.hpp` | 类型擦除的一次性回调（`unique_ptr` + 虚函数），同时提供 `multishot_call` 多次调用语义 |
| `expected.hpp` | 轻量 `expected`，把 `-errno` 作为错误码传递，避免异常穿越回调 |
| `bytes_buffer.hpp` | `bytes_view` / `bytes_const_view` / `bytes_buffer` 三件套 |
| `http_codec.hpp` | HTTP/1.1 头部与正文的状态机解析、响应/请求序列化 |
| `http_server.hpp` | 监听、接受连接、按 URL 路由、keep-alive 循环 |
| `http_client.hpp` | 客户端：URL 解析、连接池复用、响应解析 |
| `log/` | **本项目的异步日志模块**：双缓冲写入、后台落盘、`LOG_*` 宏（见下节） |
| `reflect.hpp` | 结构体反射 + JSON 编解码（`REFLECT(...)` 宏） |
| `enum_magic.hpp` | 编译期枚举名与字符串互转 |
| `file_utils.hpp` | 静态文件读写 |
| `chat_server.cpp` | 聊天室业务：`/`、`/favicon.ico`、`/send`、`/recv` |
| `sample_server.cpp` / `sample_client.cpp` | 最小回显服务 / 每秒请求一次的客户端 |

## 可执行程序

`CMakeLists.txt` 为每个 `.cpp` 生成一个可执行文件，并统一链接异步日志静态库 `async_log`：

- `chat_server` — 在线聊天室，默认监听 `0.0.0.0:8080`
- `sample_server` — 请求正文回显，监听 `127.0.0.1:8080`
- `sample_client` — 请求本机服务并打印响应长度

## 聊天室接口

| 方法 | 路径 | 请求体 | 响应 |
| :--- | :--- | :--- | :--- |
| GET | `/` | — | `static/index.html` |
| GET | `/favicon.ico` | — | 站点图标 |
| POST | `/send` | `{"user":"昵称","content":"正文"}` | `OK` |
| POST | `/recv` | `{"first":N}` | `[{"user":"...","content":"..."}, ...]` |

`/recv` 是长轮询：没有第 N 条之后的新消息时不会立刻返回，而是注册一个 3 秒定时器后挂起；
一旦 `/send` 写入新消息就提前触发定时器回调，把消息返回给客户端。
前端 `static/index.html` 在收到响应后立刻发起下一次 `/recv`，因此消息延迟基本等于网络往返时间。

> 消息历史保存在进程内存中，重启即清空；消息下标从 0 开始，前端用 `messages_first` 记录已读位置。

## 异步日志接入

### 日志模块（`log/`）

日志库源码就在本项目内，`CMakeLists.txt` 直接把它编成静态库 `async_log` 并链接给各个可执行文件，
**不依赖任何外部日志库**：

```cmake
add_library(async_log STATIC
    log/AsyncLogging.cpp  log/CountDownLatch.cpp  log/FileUtil.cpp
    log/LogFile.cpp       log/Logging.cpp        log/LogStream.cpp
    log/Thread.cpp)
target_include_directories(async_log PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/log)
target_link_libraries(async_log PUBLIC Threads::Threads)
```

| 文件 | 职责 |
| :--- | :--- |
| `log/AsyncLogging.{h,cpp}` | 双缓冲核心：前端写满一块就交换指针，后台线程批量落盘 |
| `log/Logging.{h,cpp}` | 对外接口：`LOG_*` 宏、级别过滤、时间戳与线程号前缀、惰性启动与退出刷新 |
| `log/LogStream.{h,cpp}` | 4KB 定长缓冲与 `<<` 重载，整数转换不走 `printf` |
| `log/LogFile.{h,cpp}`、`log/FileUtil.{h,cpp}` | 日志文件封装与追加写 |
| `log/Thread.{h,cpp}`、`CountDownLatch`、`MutexLock.h`、`Condition.h` | 线程与同步原语 |

业务线程只把日志格式化进内存缓冲并立即返回，磁盘 I/O 全在后台线程完成，因此不会拖慢事件循环：

```cpp
LOG_INFO  << "listening on " << host << ':' << port;
LOG_DEBUG << "connection accepted, fd=" << connfd;
LOG_WARN  << "no route for " << request.url;
LOG_ERROR << "accept failed: " << strerror(-ret.error());
```

| 位置 | 级别 | 内容 |
| :--- | :--- | :--- |
| 进程启动、监听成功 | INFO | 启动横幅、监听地址端口 |
| `/send` | INFO | 昵称、正文长度、历史条数（不记录正文） |
| `/recv` | DEBUG | 起始下标与立即返回 / 挂起 |
| 连接建立、对端关闭、空连接超时 | DEBUG | 连接 fd 与断开原因 |
| 路由不存在 | WARN | URL |
| 读写失败、JSON 解析失败 | WARN | 错误原因 |
| 静态文件缺失 | ERROR | 异常信息 |
| SIGINT / SIGTERM | INFO | 记录后优雅退出 |

日志文件名默认 `chat_server.log`（当前工作目录）：

- 默认级别 `INFO`；设置环境变量 `CHAT_LOG_LEVEL=debug` 打开 DEBUG。
- 信号通过 `eventfd` 转换为一次可读事件，唤醒事件循环后退出；
  进程正常结束前 `atexit` 会刷新后台缓冲区，Ctrl-C 不会丢最后一段日志。

## 构建与运行

```bash
cmake -B build
cmake --build build -j

cd build
./chat_server                     # http://127.0.0.1:8080
CHAT_LOG_LEVEL=debug ./chat_server
```

`chat_server` 会自动识别静态资源目录（从项目根目录或 `build/` 启动都能找到 `static/index.html`）。
前端加载同目录下的 `static/jquery.min.js`，无需外网。

## 说明

- `include/bearssl*` 与 `include/bearssl/` 是第三方 TLS 库头文件，仅作编译期依赖保留，
  聊天室本身不启用 HTTPS，其中的版权声明按原样保留。
- 服务器为单线程 Reactor：所有回调都在事件循环线程中执行，因此业务回调里不要做阻塞操作。