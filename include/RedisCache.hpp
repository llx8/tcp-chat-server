#pragma once
#include <string>
#include <vector>
#include <hiredis/hiredis.h>

struct Message;

class RedisCache {
public:
    RedisCache(const std::string& host, int port);
    ~RedisCache();

    bool connect();

    // 离线消息
    void push_offline(const std::string& username, const Message& msg);
    std::vector<Message> pop_offline(const std::string& username);

private:
    redisContext* ctx_ = nullptr;
    std::string host_;
    int port_;
};
