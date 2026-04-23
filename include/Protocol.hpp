#pragma once
#include <string>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>

// 消息结构体
struct Message {
    std::string type;
    std::string sender;
    std::string target;
    std::string content;

    std::string serialize() const {
        std::string result;
        uint32_t len;

        len = htonl(type.size());
        result.append(reinterpret_cast<const char*>(&len), 4);
        result.append(type);

        len = htonl(sender.size());
        result.append(reinterpret_cast<const char*>(&len), 4);
        result.append(sender);

        len = htonl(target.size());
        result.append(reinterpret_cast<const char*>(&len), 4);
        result.append(target);

        len = htonl(content.size());
        result.append(reinterpret_cast<const char*>(&len), 4);
        result.append(content);

        return result;
    }

    static Message deserialize(const std::string& data) {
        Message msg;
        size_t pos = 0;

        msg.type = read_tlv_field(data, pos);
        msg.sender = read_tlv_field(data, pos);
        msg.target = read_tlv_field(data, pos);
        msg.content = read_tlv_field(data, pos);
        return msg;
    }

private:
    static std::string read_tlv_field(const std::string& data, size_t& pos) {
        if (pos + 4 > data.size()) return "";
        uint32_t net_len;
        std::memcpy(&net_len, data.data() + pos, 4);
        uint32_t len = ntohl(net_len);
        pos += 4;
        if (pos + len > data.size()) return "";
        std::string field = data.substr(pos, len);
        pos += len;
        return field;
    }
};

// 连接工具类
class Connection {
public:
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

    // 接收一帧，处理粘包
    static bool recv_frame(int fd, std::string& buffer, Message& out_msg) {
        while (true) {
            if (buffer.size() > 65536) { buffer.clear(); return false; }
            if (buffer.size() < 4) {
                if (!read_more(fd, buffer)) return false;
                continue;
            }

            uint32_t net_len;
            std::memcpy(&net_len, buffer.data(), 4);
            uint32_t payload_len = ntohl(net_len);

            if (buffer.size() < 4 + payload_len) {
                if (!read_more(fd, buffer)) return false;
                continue;
            }

            std::string payload = buffer.substr(4, payload_len);
            buffer.erase(0, 4 + payload_len);
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
