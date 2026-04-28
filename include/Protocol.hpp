#pragma once
#include <string>
#include <sstream>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>

// 结构化的消息体
struct Message {
    std::string type;     // LOGIN, CHAT, PRIVATE, PING, PONG, SYS
    std::string sender;
    std::string target;
    std::string content;

    // 序列化
    std::string serialize() const {
        return type + "|" + sender + "|" + target + "|" + content;
    }

    // 反序列化
    static Message deserialize(const std::string& data) {
        Message msg;
        std::stringstream ss(data);
        std::getline(ss, msg.type, '|');
        std::getline(ss, msg.sender, '|');
        std::getline(ss, msg.target, '|');
        std::getline(ss, msg.content);
        return msg;
    }
};

class Connection {
public:
    // 发送数据帧：自动附加 4 字节长度头
    static bool send_frame(int fd, const Message& msg) {
        std::string payload = msg.serialize();
        uint32_t net_len = htonl(payload.size()); 
        
        std::string buffer;
        buffer.append(reinterpret_cast<char*>(&net_len), 4);
        buffer.append(payload);

        size_t total = 0;
        while (total < buffer.size()) {
            ssize_t sent = send(fd, buffer.data() + total, buffer.size() - total, MSG_NOSIGNAL);
            if (sent <= 0) return false;
            total += sent;
        }
        return true;
    }

    // 接收数据帧：处理粘包，确保提取完整一帧
    static bool recv_frame(int fd, std::string& buffer, Message& out_msg) {
        while (true) {
            if (buffer.size() < 4) {
                if (!read_more(fd, buffer)) return false;
                continue;
            }

            uint32_t net_len;
            std::memcpy(&net_len, buffer.data(), 4);
            uint32_t host_len = ntohl(net_len);

            if (buffer.size() < 4 + host_len) {
                if (!read_more(fd, buffer)) return false;
                continue;
            }

            std::string payload = buffer.substr(4, host_len);
            buffer.erase(0, 4 + host_len); 
            out_msg = Message::deserialize(payload);
            return true;
        }
    }

private:
    static bool read_more(int fd, std::string& buffer) {
        char temp[4096];
        ssize_t n = recv(fd, temp, sizeof(temp), 0);
        if (n <= 0) return false; 
        buffer.append(temp, n);
        return true;
    }
};