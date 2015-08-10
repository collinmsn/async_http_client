/*
 *    Author: Zhenghuabin
 *     Email: Zhenghuabin@qiyi.com
 *   Created: Thu Jan 16 11:12:55 2014
 * Copyright: Copyright (C) 2013 iqiyi.inc
 *
 */
#include <glog/logging.h>
#include <boost/bind.hpp>
#include <boost/algorithm/string.hpp>
#include "curl/curl.h"
#include "../inc/async_http_client.h"

namespace common {
namespace util {
AsyncHttpClient::AsyncHttpClient(boost::asio::io_service& io_service, size_t connect_timeout_ms,
                                 size_t snd_timeout_ms, size_t rcv_timeout_ms)
        : io_service_(io_service),
          resolver_(io_service),
          socket_(io_service),
          timeout_timer_(io_service),
          connect_timeout_ms_(connect_timeout_ms),
          snd_timeout_ms_(snd_timeout_ms),
          rcv_timeout_ms_(rcv_timeout_ms)
{
}
AsyncHttpClient::~AsyncHttpClient() {
    DLOG(INFO) << "AsyncHttpClient::~AsyncHttpClient";
}
void AsyncHttpClient::perform(const std::string& host, const std::string& port, const std::string& path,
                              const std::map<std::string, std::string>& query_strings,
                              const std::string& data, boost::shared_ptr<AsyncHttpCallback> callback) {
    callback_ = callback;
    if (callback_) {
        callback_->started();
    }
    std::string method;
    if (data.empty()) {
        method = "GET ";
    }
    else {
        method = "POST ";
    }
    std::string encoded_query_string;
    if (!query_strings.empty()) {
        encoded_query_string += "?";
    }
    for (std::map<std::string, std::string>::const_iterator it = query_strings.begin();
         it != query_strings.end(); ++it) {
        if (it != query_strings.begin()) {
            encoded_query_string += "&";
        }
        encoded_query_string += it->first + "=";
        char * escaped = curl_easy_escape(NULL, it->second.c_str(), it->second.size());
        if (!escaped) {
            std::string msg = "escape query string field failed: [" + it->first + ":" + it->second + "]";
            LOG(ERROR) << msg;
            on_failed(msg);
            return;
        }
        encoded_query_string += std::string(escaped);
        curl_free(escaped);
    }
    
    std::ostream request_stream(&request_);
    request_stream << method << path << encoded_query_string << " HTTP/1.0\r\n";
    DLOG(INFO) << method << path << encoded_query_string << " HTTP/1.0\r\n";
    request_stream << "Host: " << host << "\r\n";
    request_stream << "Accept: */*\r\n";
    if (!data.empty()) {
        request_stream << "Content-Type: application/x-www-form-urlencoded\r\n";
        request_stream << "Content-Length: " << data.size() << "\r\n";
    }
    request_stream << "Connection: close\r\n\r\n";
    request_stream << data;

    // Start an asynchronous resolve to translate the server and service names
    // into a list of endpoints.
    boost::asio::ip::tcp::resolver::query query(host, port);
    resolver_.async_resolve(query,
                            boost::bind(&AsyncHttpClient::handle_resolve, shared_from_this(),
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));
}
void AsyncHttpClient::perform(const std::string& url,
                              const std::map<std::string, std::string>& query_strings,
                              const std::string& data, boost::shared_ptr<AsyncHttpCallback> callback) {
    std::string host, port, path;
    parse_url(url, &host, &port, &path);
    perform(host, port, path, query_strings, data, callback);
}
bool AsyncHttpClient::parse_url(const std::string& url_in, std::string* host, std::string* port,
                                std::string* path) {
    // parse host, port, path from url
    // http://www.host.com:88/path
    std::string url = url_in;
    size_t pos = url.find("//");
    if (pos != std::string::npos) {
        url.erase(0, pos + 2);
    }
    pos = url.find('/');
    if (pos == std::string::npos) {
        if (path) {
            path->assign("/");
        }
    } else {
        if (path) {
            path->assign(url.substr(pos));
        }
        url.erase(pos);
    }
    pos = url.find(':');
    if (pos == std::string::npos) {
        if (port) {
            port->assign("80");
        }
    } else {
        if (port) {
            port->assign(url.substr(pos + 1));
        }
        url.erase(pos);
    }
    if (host) {
        host->assign(url);
    }
    return true;
}

bool AsyncHttpClient::parse_query_strings(const std::string& query_string_str, std::map<std::string, std::string>* query_strings) {
    std::vector<std::string> elems;
    std::vector<std::string> kv_pair;
    boost::split(elems, query_string_str, boost::is_any_of("&"), boost::token_compress_on);
    for (size_t i = 0; i < elems.size(); ++i) {
        boost::split(kv_pair, elems[i], boost::is_any_of("="), boost::token_compress_on);
        if (kv_pair.size() != 2) {
            std::string msg = "invalid query string format: " + query_string_str;
            LOG(ERROR) << msg;
            return false;
        }
        int outlen = 0;
        char* unescaped = curl_easy_unescape(NULL, kv_pair[1].c_str(), kv_pair[1].size(), &outlen);
        if (!unescaped) {
            std::string msg = "unescape query string value failed: " + elems[i];
            LOG(ERROR) << msg;
            return false;
        }
        query_strings->insert(std::map<std::string, std::string>::value_type(kv_pair[0], std::string(unescaped, outlen)));
        curl_free(unescaped);
    }
    return true;
}

void AsyncHttpClient::handle_resolve(const boost::system::error_code& err,
                                     boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
    if (err) {
        LOG(ERROR) << err.message();
        on_failed(err.message());
        return;
    }
    // Attempt a connection to each endpoint in the list until we
    // successfully establish a connection.
    boost::asio::async_connect(socket_, endpoint_iterator,
                               boost::bind(&AsyncHttpClient::handle_connect, shared_from_this(),
                                           boost::asio::placeholders::error));
    start_timeout_timer(CONNECT);
}
void AsyncHttpClient::handle_connect(const boost::system::error_code& err) {
    cancel_timeout_timer();
    if (err) {
        LOG(ERROR) << err.message();
        on_failed(err.message());
        return;
    }

    // The connection was successful. Send the request.
    boost::asio::async_write(socket_, request_,
                             boost::bind(&AsyncHttpClient::handle_write_request, shared_from_this(),
                                         boost::asio::placeholders::error));
    start_timeout_timer(SEND_REQUEST);
}
void AsyncHttpClient::handle_write_request(const boost::system::error_code& err) {
    cancel_timeout_timer();     
    if (err) {
        LOG(ERROR) << err.message();
        on_failed(err.message());
        return;
    }

    // Read the response status line. The response_ streambuf will
    // automatically grow to accommodate the entire line. The growth may be
    // limited by passing a maximum size to the streambuf constructor.
    boost::asio::async_read_until(socket_, response_, "\r\n",
                                  boost::bind(&AsyncHttpClient::handle_read_status_line, shared_from_this(),
                                              boost::asio::placeholders::error));
    start_timeout_timer(READ_RESPONSE);
}
void AsyncHttpClient::handle_read_status_line(const boost::system::error_code& err) {
    if (err) {
        LOG(ERROR) << err.message();
        on_failed(err.message());
        return;
    }

    // Check that response is OK.
    std::istream response_stream(&response_);
    std::string http_version;
    response_stream >> http_version;
    response_stream >> status_code_;
    std::string status_message;
    std::getline(response_stream, status_message);
    if (!response_stream || http_version.substr(0, 5) != "HTTP/")
    {
        std::string msg = "Invalid response";
        LOG(ERROR) << msg;
        on_failed(msg);
        return;
    }

    // Read the response headers, which are terminated by a blank line.
    boost::asio::async_read_until(socket_, response_, "\r\n\r\n",
                                  boost::bind(&AsyncHttpClient::handle_read_headers, shared_from_this(),
                                              boost::asio::placeholders::error));
}
void AsyncHttpClient::handle_read_headers(const boost::system::error_code& err)
{
    if (err) {
        LOG(ERROR) << err.message();
        on_failed(err.message());
        return;
    }

    // Process the response headers.
    std::istream response_stream(&response_);
    std::string header;
    while (std::getline(response_stream, header) && header != "\r") {
        ;
    }

    // Start reading remaining data until EOF.
    boost::asio::async_read(socket_, response_,
                            boost::asio::transfer_at_least(1),
                            boost::bind(&AsyncHttpClient::handle_read_content, shared_from_this(),
                                        boost::asio::placeholders::error));
}
void AsyncHttpClient::handle_read_content(const boost::system::error_code& err)
{
    if (!err)
    {
        // Continue reading remaining data until EOF.
        boost::asio::async_read(socket_, response_,
                                boost::asio::transfer_at_least(1),
                                boost::bind(&AsyncHttpClient::handle_read_content, shared_from_this(),
                                            boost::asio::placeholders::error));
    }
    else if (err == boost::asio::error::eof)
    {
        cancel_timeout_timer();
        LOG(INFO) << "completed status: " << status_code_;
        on_completed();
    }
    else {
        LOG(ERROR) << err.message();
        on_failed(err.message());
    }
}
void AsyncHttpClient::on_failed(const std::string& message) {
    if (callback_) {
        callback_->failed(std::runtime_error(message));
    }
}
void AsyncHttpClient::on_completed() {
    if (callback_) {
        std::istream is(&response_);
        const size_t kBufSize = 8 * 1024;
        char buf[kBufSize];
        std::string body;
        while (true) {
            is.read(buf, kBufSize);
            int num_read = is.gcount();
            body.append(buf, num_read);
            if (num_read < (int)kBufSize) {
                break;
            }
        }
        callback_->completed(status_code_, body);
    }
}

void AsyncHttpClient::on_timer_timeout(RequestStage stage, const boost::system::error_code& error) {
    DLOG(INFO) << "AsyncHttpClient::on_timer_timeout: " << stage;
    if (error == boost::asio::error::operation_aborted) {
        DLOG(INFO) << "AsyncHttpClient::on_timer_timeout: " << error.message();
    }
    else {
        LOG(WARNING) << "AsyncHttpClient::on_timer_timeout stage: " << stage << " " << error.message();
        boost::system::error_code ec;
        socket_.cancel(ec);
    }
}
void AsyncHttpClient::start_timeout_timer(RequestStage stage) {
    DLOG(INFO) << "AsyncHttpClient::start_timeout_timer stage: " << stage;
    if (stage == CONNECT) {
        timeout_timer_.expires_from_now(boost::posix_time::milliseconds(connect_timeout_ms_));
    }
    else if (stage == SEND_REQUEST) {
        timeout_timer_.expires_from_now(boost::posix_time::milliseconds(snd_timeout_ms_));
    }
    else if (stage == READ_RESPONSE) {
        timeout_timer_.expires_from_now(boost::posix_time::milliseconds(rcv_timeout_ms_));
    }
    timeout_timer_.async_wait(boost::bind(&AsyncHttpClient::on_timer_timeout, this, stage, _1));
}

void AsyncHttpClient::cancel_timeout_timer() {
    boost::system::error_code ec;
    size_t num_operation = timeout_timer_.cancel(ec);
    DLOG(INFO) << "cancel timer: " << num_operation;
}
} // end of namespace util
} // end of namespace common
