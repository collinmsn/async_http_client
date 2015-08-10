/*
 *    Author: Zhenghuabin
 *     Email: Zhenghuabin@qiyi.com
 *   Created: Thu Jan 16 15:45:45 2014
 * Copyright: Copyright (C) 2013 iqiyi.inc
 *
 */
#ifndef __TEST_CALLBACK_H__
#define __TEST_CALLBACK_H__
#include <glog/logging.h>
#include "../async_http_client.h"

class TestCallback : public common::util::AsyncHttpCallback {
public:
TestCallback(bool disp_body = true) : disp_body_(disp_body) {
    }
    void started() {
    }
    void completed(const unsigned status, const std::string& body) {
        LOG(INFO) << "completed status: " << status;
        status_ = status;
        if (disp_body_) {
            LOG(INFO) << body;
        }
    }
    void failed(const std::exception& e) {
        LOG(INFO) << "failed: " << e.what();
    }
public:
    bool disp_body_;
    static unsigned status_;
};

#endif

