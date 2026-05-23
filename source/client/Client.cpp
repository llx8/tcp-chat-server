#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include "Protocol.hpp"
#include "ThreadPool.hpp"

std::atomic<bool> running{true};
std::string my_username;
std::condition_variable ping_cv;
std::mutex ping_mtx;

// 核心修复1：强大的 trim 函数，清除首尾空格、回车符 \r\n，以及防手误复制的 '>'
inline std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n>");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

void recv_thread(int fd) {
    std::string buffer;
    Message msg;
    struct timeval tv{0, 500000}; 
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (running) {
        if (Connection::recv_frame(fd, buffer, msg)) {
            if (msg.type == "PONG") {
                continue; 
            }
            if (msg.type == "SYS") {
                std::cout << "\n[系统]: " << msg.content << "\n> " << std::flush;
            } else if (msg.type == "PRIVATE") {
                std::cout << "\n[私聊] " << msg.sender << ": " << msg.content << "\n> " << std::flush;
            } else {
                std::cout << "\n[群聊] " << msg.sender << ": " << msg.content << "\n> " << std::flush;
            }
        }
    }
}

void ping_thread(int fd) {
    std::unique_lock<std::mutex> lock(ping_mtx);
    while (running) {
        if (ping_cv.wait_for(lock, std::chrono::seconds(10), [] { return !running.load(); })) {
            break;
        }
        Connection::send_frame(fd, {"PING", my_username, "Server", ""});
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sockfd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connect failed");
        return 1;
    }

    std::cout << "Hello, welcome to the chat room!\n";
    std::cout << "Enter username to login: ";
    std::string raw_name;
    std::getline(std::cin, raw_name);
    // 修复点：彻底清洗登录名，防止带有不可见字符
    my_username = trim(raw_name); 

    Connection::send_frame(sockfd, {"LOGIN", my_username, "Server", "1234"});

    std::thread t_recv(recv_thread, sockfd);
    std::thread t_ping(ping_thread, sockfd);

    std::cout << "========= Commands =========\n";
    std::cout << "/all <msg>           - 群发消息\n";
    std::cout << "/to <user> <msg>     - 私聊发送\n";
    std::cout << "/quit                - 退出程序\n";
    std::cout << "============================\n> ";

    std::string raw_input;
    while (running && std::getline(std::cin, raw_input)) {
        // 修复点：清洗输入指令，即便用户复制了 '> /to C aaa' 也能正确识别
        std::string input = trim(raw_input);
        
        if (input.empty()) {
            std::cout << "> ";
            continue;
        }

        if (input == "/quit") {
            running = false;
            ping_cv.notify_all();
            break;
        } else if (input.find("/to ") == 0) {
            // 核心修复2：使用 stringstream 替代脆弱的 substr，自动处理连续空格
            std::stringstream ss(input);
            std::string cmd, target, msg;
            
            ss >> cmd >> target; // 自动提取 "/to" 和 目标用户名
            std::getline(ss, msg); // 提取剩余内容为消息
            msg = trim(msg);
            
            if (!target.empty() && !msg.empty()) {
                Connection::send_frame(sockfd, {"PRIVATE", my_username, target, msg});
                std::cout << "> " << std::flush;
            } else {
                std::cout << "Format error. Use: /to <user> <msg>\n> ";
            }
        } else if (input.find("/all ") == 0) {
            std::string msg = trim(input.substr(5));
            if (!msg.empty()) {
                Connection::send_frame(sockfd, {"CHAT", my_username, "ALL", msg});
                std::cout << "> " << std::flush;
            } else {
                std::cout << "Format error. Use: /all <msg>\n> ";
            }
        } else {
            std::cout << "Unknown command.\n> ";
        }
    }

    running = false;
    ping_cv.notify_all();
    if (t_recv.joinable()) t_recv.join();
    if (t_ping.joinable()) t_ping.join();
    close(sockfd);
    return 0;
}