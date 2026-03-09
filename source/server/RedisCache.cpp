#include "RedisCache.hpp"
#include "Protocol.hpp"
#include <iostream>

RedisCache::RedisCache(const std::string& host, int port)
    : host_(host), port_(port) {}

RedisCache::~RedisCache() {
    if (ctx_) redisFree(ctx_);
}

bool RedisCache::connect() {
    ctx_ = redisConnect(host_.c_str(), port_);
    if (!ctx_ || ctx_->err) {
        std::cerr << "Redis connect failed" << std::endl;
        return false;
    }
    return true;
}

// 离线消息
void RedisCache::push_offline(const std::string& username, const Message& msg) {
    std::string json = msg.serialize();
    redisCommand(ctx_, "LPUSH offline:%s %b", username.c_str(), json.data(), json.size());
}

std::vector<Message> RedisCache::pop_offline(const std::string& username) {
    std::vector<Message> result;
    std::string key = "offline:" + username;

    redisReply* reply = (redisReply*)redisCommand(ctx_, "LLEN %s", key.c_str());
    long len = reply->integer;
    freeReplyObject(reply);

    for (long i = 0; i < len; i++) {
        reply = (redisReply*)redisCommand(ctx_, "RPOP %s", key.c_str());
        if (reply->type == REDIS_REPLY_STRING) {
            Message msg = Message::deserialize(std::string(reply->str, reply->len));
            result.push_back(msg);
        }
        freeReplyObject(reply);
    }
    return result;
}
