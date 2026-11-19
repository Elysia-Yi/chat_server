#include <string.h>

#include "Logging.h"
#include "http_client.hpp"
#include "io_context.hpp"

// 最小示例：每秒请求一次本地服务并打印响应长度
void client() {
    io_context ctx;
    auto client = http_client::make();

    client->do_request(
        {"GET", "http://127.0.0.1:8080"},
        [client](expected<int> ret,
                 http_client::http_response const &response) {
            if (ret.error()) {
                LOG_ERROR << "request failed: " << strerror(-ret.error());
                return;
            }
            LOG_INFO << "GET / -> status " << response.status << ", "
                     << response.body.size() << " bytes";

            io_context::get().set_timeout(std::chrono::seconds(1), [client] {
                client->do_request(
                    {"GET", "http://127.0.0.1:8080"},
                    [](expected<int> ret,
                       http_client::http_response const &response) {
                        if (ret.error()) {
                            LOG_ERROR << "request failed: "
                                      << strerror(-ret.error());
                            return;
                        }
                        LOG_INFO << "GET / -> status " << response.status
                                 << ", " << response.body.size() << " bytes";
                    });
            });
        });

    ctx.join();
}

int main() {
    Logger::setLogFileName("sample_client.log");
    try {
        client();
    } catch (std::exception const &e) {
        LOG_ERROR << "fatal error: " << e.what();
        return 1;
    }
    return 0;
}