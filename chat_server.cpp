#include <sys/eventfd.h>
#include <unistd.h>

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "Logging.h"
#include "file_utils.hpp"
#include "http_server.hpp"
#include "io_context.hpp"
#include "reflect.hpp"

using namespace std::chrono_literals;


struct Message {
    std::string user;
    std::string content;

    REFLECT(user, content);
};

struct RecvParams {
    uint32_t first;

    REFLECT(first);
};

namespace {

std::vector<Message> messages;
stop_source recv_timeout_stop = stop_source::make();

// 静态资源目录：兼容从仓库根目录或 build 目录启动
const std::string kStaticDir = [] {
    namespace fs = std::filesystem;
    if (fs::exists("static/index.html")) {
        return std::string("static/");
    }
    if (fs::exists("../static/index.html")) {
        return std::string("../static/");
    }
    return std::string("static/");
}();

// 把 SIGINT/SIGTERM 转成一次 eventfd 可读事件，让事件循环能优雅退出
int g_stop_fd = -1;
char g_stop_buf[8];

void on_terminate_signal(int) {
    uint64_t one = 1;
    ssize_t n = ::write(g_stop_fd, &one, sizeof(one));
    (void)n;
}

} // namespace

void server() {
    io_context ctx;

    async_file stop_watcher{::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)};
    g_stop_fd = stop_watcher.m_fd;
    std::signal(SIGINT, on_terminate_signal);
    std::signal(SIGTERM, on_terminate_signal);
    stop_watcher.async_read(bytes_view{g_stop_buf, sizeof(g_stop_buf)},
                            [](expected<size_t>) {
                                LOG_INFO << "shutdown signal received";
                                io_context::get().request_stop();
                            });

    messages.push_back({"系统", "你好，欢迎来到聊天室"});

    auto server = http_server::make();
    server->get_router().route("/", [](http_server::http_request &request) {
        try {
            std::string response = file_get_content(kStaticDir + "index.html");
            request.write_response(200, response, "text/html");
        } catch (std::exception const &e) {
            LOG_ERROR << "cannot serve index.html: " << e.what();
            request.write_response(500, "500 Internal Server Error");
        }
    });
    server->get_router().route(
        "/favicon.ico", [](http_server::http_request &request) {
            try {
                std::string response =
                    file_get_content(kStaticDir + "favicon.ico");
                request.write_response(200, response, "image/x-icon");
            } catch (std::exception const &e) {
                LOG_WARN << "favicon unavailable: " << e.what();
                request.write_response(404, "404 Not Found");
            }
        });
    server->get_router().route("/send", [](http_server::http_request &request) {
        Message message;
        try {
            message = reflect::json_decode<Message>(request.body);
        } catch (std::exception const &e) {
            LOG_WARN << "malformed /send payload: " << e.what();
            return request.write_response(400, "Bad Request");
        }

        messages.push_back(message);
        // 只记录昵称与长度，不把聊天正文写进日志
        LOG_INFO << "message from \"" << message.user << "\", "
                 << message.content.size() << " bytes, history "
                 << messages.size();

        // 有新消息，唤醒正在长轮询的 /recv
        recv_timeout_stop.request_stop();
        recv_timeout_stop = stop_source::make();
        request.write_response(200, "OK");
    });
    server->get_router().route("/recv", [](http_server::http_request &request) {
        RecvParams params;
        try {
            params = reflect::json_decode<RecvParams>(request.body);
        } catch (std::exception const &e) {
            LOG_WARN << "malformed /recv payload: " << e.what();
            return request.write_response(400, "Bad Request");
        }

        auto reply = [](http_server::http_request &req, uint32_t first) {
            std::vector<Message> submessages;
            if (messages.size() > first) {
                submessages.assign(messages.begin() + first, messages.end());
            }
            std::string response = reflect::json_encode(submessages);
            req.write_response(200, response);
        };

        if (messages.size() > params.first) {
            LOG_DEBUG << "/recv from index " << params.first << " returned "
                      << (messages.size() - params.first) << " message(s)";
            return reply(request, params.first);
        }

        LOG_DEBUG << "/recv from index " << params.first
                  << " parked for at most 3s";
        io_context::get().set_timeout(
            3s, [&request, reply, first = params.first] { reply(request, first); },
            recv_timeout_stop);
    });

    server->do_start("0.0.0.0", "8080");

    ctx.join();
    LOG_INFO << "chat server stopped";
}

int main() {
    Logger::setLogFileName("chat_server.log");
    if (const char *env = std::getenv("CHAT_LOG_LEVEL")) {
        if (std::string(env) == "debug") {
            Logger::setLogLevel(LOG_LEVEL_DEBUG);
        }
    }

    LOG_INFO << "==================== chat server starting ====================";

    try {
        server();
    } catch (std::exception const &e) {
        LOG_ERROR << "fatal error: " << e.what();
        return 1;
    }
    return 0;
}