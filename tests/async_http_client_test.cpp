/*
 *    Author: Zhenghuabin
 *     Email: Zhenghuabin@qiyi.com
 *   Created: Thu Jan 16 15:45:45 2014
 * Copyright: Copyright (C) 2013 iqiyi.inc
 *
 */
#include "gtest/gtest.h"
#include "../async_http_client.h"
#include "test_callback.h"
#include <glog/logging.h>

TEST(async_http_client, basic) {
    const std::string host = "newsmth.net";
    const std::string port = "80";
    const std::string path = "/forum.php";
    std::map<std::string, std::string> query_strings;
    query_strings["mod"] = "forumdisplay";
    query_strings["fid"] = "38";

    TestCallback::status_ = 500;
    boost::asio::io_service io_service;
    boost::shared_ptr<common::util::AsyncHttpClient> client(new common::util::AsyncHttpClient(io_service));
    boost::shared_ptr<TestCallback> callback(new TestCallback());
    client->perform(host, port, path, query_strings,
                   std::string(), boost::shared_ptr<TestCallback>(callback));
    io_service.run();
    EXPECT_EQ(200, int(TestCallback::status_));
}

TEST(async_http_client, timeout) {
    const std::string host = "10.15.154.91";
    const std::string port = "1026";
    const std::string path = "/echo_10_seconds";
    std::map<std::string, std::string> query_strings;

    // timeout
    TestCallback::status_ = 500;
    boost::asio::io_service io_service;
    size_t conn_timeo = 3 * 1000;
    size_t send_timeo = 3 * 1000;
    size_t recv_timeo = 3 * 1000;
    boost::shared_ptr<common::util::AsyncHttpClient> client(
        new common::util::AsyncHttpClient(io_service, conn_timeo,                                                                                              send_timeo, recv_timeo));
    boost::shared_ptr<TestCallback> callback(new TestCallback());
            
    client->perform(host, port, path, query_strings,
                    std::string(), boost::shared_ptr<TestCallback>(callback));
    io_service.run();
    EXPECT_EQ(500, int(TestCallback::status_));

    // will not timeout
    TestCallback::status_ = 500;
    io_service.reset();
    recv_timeo = 15 * 1000;
    client.reset(new common::util::AsyncHttpClient(io_service, conn_timeo,
                                                   send_timeo, recv_timeo));
    client->perform(host, port, path, query_strings,
                    std::string(), boost::shared_ptr<TestCallback>(callback));
    io_service.run();
    EXPECT_EQ(200, int(TestCallback::status_));
    
    // test default timeout
    TestCallback::status_ = 500;
    io_service.reset();
    client.reset(new common::util::AsyncHttpClient(io_service));
    client->perform(host, port, path, query_strings,
                    std::string(), boost::shared_ptr<TestCallback>(callback));
    io_service.run();
    EXPECT_EQ(200, int(TestCallback::status_)); 
}  
       
