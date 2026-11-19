#pragma once

#include <string.h>

#include <map>
#include <memory>
#include <string>

#include "Logging.h"
#include "expected.hpp"
#include "http_codec.hpp"
#include "io_context.hpp"
#include "stop_source.hpp"

// HTTP/1.1 服务器：监听、接受连接、按 URL 路由到业务回调
struct http_server : std::enable_shared_from_this<http_server> {
    using pointer = std::shared_ptr<http_server>;

    static pointer make() {
        return std::make_shared<pointer::element_type>();
    }

    struct http_request {
        std::string url;
        http_method method;
        std::string body;

        http_response_writer<> *m_res_writer = nullptr;
        callback<> m_resume;

        void write_response(
            int status, std::string_view content,
            std::string_view content_type = "text/plain;charset=utf-8") {
            m_res_writer->begin_header(status);
            m_res_writer->write_header("Server", "cpp-http");
            m_res_writer->write_header("Content-type", content_type);
            m_res_writer->write_header("Connection", "keep-alive");
            m_res_writer->write_header("Content-length",
                                       std::to_string(content.size()));
            m_res_writer->end_header();
            m_res_writer->write_body(content);
            m_resume();
        }
    };

    struct http_router {
        std::map<std::string, callback<http_request &>> m_routes;

        void route(std::string url, callback<http_request &> cb) {
            m_routes.insert_or_assign(url, std::move(cb));
        }

        void do_handle(http_request &request) {
            auto it = m_routes.find(request.url);
            if (it != m_routes.end()) {
                return it->second(multishot_call, request);
            }
            LOG_WARN << "no route for " << request.url;
            return request.write_response(404, "404 Not Found");
        }
    };

    struct http_connection_handler
        : std::enable_shared_from_this<http_connection_handler> {
        async_file m_conn;
        bytes_buffer m_readbuf{1024};
        http_request_parser<> m_req_parser;
        http_response_writer<> m_res_writer;
        http_router *m_router = nullptr;
        http_request m_request;

        using pointer = std::shared_ptr<http_connection_handler>;

        static pointer make() {
            return std::make_shared<pointer::element_type>();
        }

        void do_start(http_router *router, int connfd) {
            m_router = router;
            m_conn = async_file{connfd};
            LOG_DEBUG << "connection accepted, fd=" << connfd;
            return do_read();
        }

        void do_read() {
            // 空连接最多保留 10 秒，超时则取消读取并断开
            stop_source stop_io(std::in_place);
            stop_source stop_timer(std::in_place);
            auto read_done = std::make_shared<bool>(false);
            io_context::get().set_timeout(
                std::chrono::seconds(10),
                [stop_io, read_done] {
                    if (*read_done) {
                        return;  // 读取已经完成，这里只是取消定时器
                    }
                    LOG_DEBUG << "connection idle timeout, closing";
                    stop_io.request_stop();
                },
                stop_timer);
            return m_conn.async_read(
                m_readbuf,
                [self = shared_from_this(), stop_timer,
                 read_done](expected<size_t> ret) {
                    *read_done = true;
                    stop_timer.request_stop();
                    if (ret.error()) {
                        LOG_WARN << "read failed: " << strerror(-ret.error())
                                 << ", closing connection";
                        return;
                    }
                    size_t n = ret.value();
                    if (n == 0) {
                        LOG_DEBUG << "peer closed the connection";
                        return;
                    }
                    self->m_req_parser.push_chunk(
                        self->m_readbuf.subspan(0, n));
                    if (!self->m_req_parser.request_finished()) {
                        return self->do_read();
                    }
                    return self->do_handle();
                },
                stop_io);
        }

        void do_handle() {
            m_request.url = m_req_parser.url();
            m_request.method = m_req_parser.method();
            m_request.body = std::move(m_req_parser.body());
            m_request.m_res_writer = &m_res_writer;
            m_request.m_resume = [self = shared_from_this()] {
                self->do_write(self->m_res_writer.buffer());
            };
            m_req_parser.reset_state();

            LOG_DEBUG << "request " << m_request.url << ", body "
                      << m_request.body.size() << " bytes";
            m_router->do_handle(m_request);
        }

        void do_write(bytes_const_view buffer) {
            return m_conn.async_write(buffer, [self = shared_from_this(),
                                               buffer](expected<size_t> ret) {
                if (ret.error()) {
                    LOG_WARN << "write failed: " << strerror(-ret.error())
                             << ", closing connection";
                    return;
                }
                auto n = ret.value();

                if (buffer.size() == n) {
                    self->m_res_writer.reset_state();
                    return self->do_read();
                }
                return self->do_write(buffer.subspan(n));
            });
        }
    };

    async_file m_listening;
    address_resolver::address m_addr;
    http_router m_router;

    http_router &get_router() {
        return m_router;
    }

    void do_start(std::string name, std::string port) {
        address_resolver resolver;
        auto entry = resolver.resolve(name, port);
        m_listening = async_file::async_bind(entry);
        LOG_INFO << "listening on " << name << ':' << port;
        return do_accept();
    }

    void do_accept() {
        return m_listening.async_accept(m_addr, [self = shared_from_this()](
                                                    expected<int> ret) {
            if (ret.error()) {
                LOG_ERROR << "accept failed: " << strerror(-ret.error());
                return self->do_accept();
            }
            int connfd = ret.value();
            http_connection_handler::make()->do_start(&self->m_router, connfd);
            return self->do_accept();
        });
    }
};