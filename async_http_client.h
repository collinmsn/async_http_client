/*
 *    Author: Zhenghuabin
 *     Email: Zhenghuabin@qiyi.com
 *   Created: Wed Jan 15 18:20:26 2014
 * Copyright: Copyright (C) 2013 iqiyi.inc
 *
 */
#ifndef _ASYNC_HTTP_CLIENT_H_
#define _ASYNC_HTTP_CLIENT_H_
#include <string>
#include <map>
#include <boost/asio.hpp>
#include <boost/enable_shared_from_this.hpp>
namespace common {
    namespace util {
        class AsyncHttpCallback {
      public:
          virtual void started() = 0;
            virtual void completed(const unsigned status, const std::string& body) = 0;
            virtual void failed(const std::exception& e) = 0;
            virtual ~AsyncHttpCallback() {}
        };

        class AsyncHttpClient : public boost::enable_shared_from_this<AsyncHttpClient>,
                private boost::noncopyable {
            enum RequestStage {
                CONNECT,
                SEND_REQUEST,
                READ_RESPONSE
            };
      public:
                    AsyncHttpClient(boost::asio::io_service& io_service, size_t connect_timeout_ms = 30 * 1000,
                                    size_t snd_timeout_ms = 3600 * 1000, size_t rcv_timeout_ms = 3600 * 1000);
                    ~AsyncHttpClient();
                    void perform(const std::string& host, const std::string& port, const std::string& path,
                                 const std::map<std::string, std::string>& query_strings,
                                 const std::string& data, boost::shared_ptr<AsyncHttpCallback> callback);
                    void perform(const std::string& url,
                                 const std::map<std::string, std::string>& query_strings,
                                 const std::string& data, boost::shared_ptr<AsyncHttpCallback> callback);
                    static bool parse_url(const std::string& url, std::string* host, std::string* port,
                                          std::string* path);
                    static bool parse_query_strings(const std::string& query_string_str,
                                                    std::map<std::string, std::string>* query_strings);
      private:
                    void handle_resolve(const boost::system::error_code& err,
                                        boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
                    void handle_connect(const boost::system::error_code& err);
                    void handle_write_request(const boost::system::error_code& err);
                    void handle_read_status_line(const boost::system::error_code& err);
                    void handle_read_headers(const boost::system::error_code& err);
                    void handle_read_content(const boost::system::error_code& err);
                    void on_failed(const std::string& message);
                    void on_completed();
      private:
                    void on_timer_timeout(RequestStage stage, const boost::system::error_code& error);
                    void start_timeout_timer(RequestStage stage);
                    void cancel_timeout_timer();

      private:
                    boost::asio::io_service& io_service_;
                    boost::shared_ptr<AsyncHttpCallback> callback_;
                    boost::asio::ip::tcp::resolver resolver_;
                    boost::asio::ip::tcp::socket socket_;
                    boost::asio::streambuf request_;
                    boost::asio::streambuf response_;
                    unsigned status_code_;
                    boost::asio::deadline_timer timeout_timer_;
                    size_t connect_timeout_ms_;
                    size_t snd_timeout_ms_;
                    size_t rcv_timeout_ms_;
                };
    } // end of namespace util
} // end of namespace common

#endif /*_ASYNC_HTTP_CLIENT_H_*/
