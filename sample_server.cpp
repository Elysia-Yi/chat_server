#include <string>

#include "Logging.h"
#include "http_server.hpp"
#include "io_context.hpp"

// 最小示例：把请求正文原样回显
void server() {
    io_context ctx;
    auto server = http_server::make();
    server->get_router().route("/", [](http_server::http_request &request) {
        std::string response;
        if (request.body.empty()) {
            response = "你好，你的请求正文为空哦";
        } else {
            response = "你好，你的请求是: [" + request.body
                + "]，共 " + std::to_string(request.body.size())
                + " 字节";
        }
        request.write_response(200, response);
    });

    server->do_start("127.0.0.1", "8080");

    ctx.join();
}

int main() {
    Logger::setLogFileName("sample_server.log");
    try {
        server();
    } catch (std::exception const &e) {
        LOG_ERROR << "fatal error: " << e.what();
        return 1;
    }
    return 0;
}