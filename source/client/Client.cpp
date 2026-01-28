#include <iostream>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unistd.h>
#include <arpa/inet.h>
#include "Protocol.hpp"

std::atomic<bool> running{true};
std::string my_username;
std::condition_variable ping_cv;
std::mutex ping_mtx;

inline std::string trim(const std::string& str) {
    size_t f = str.find_first_not_of(" \t\r\n>");
    if (f == std::string::npos) return "";
    return str.substr(f, str.find_last_not_of(" \t\r\n") - f + 1);
}

void recv_thread(int fd) {
    std::string buf; Message msg;
    struct timeval tv{0,500000}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    while (running) {
        if (Connection::recv_frame(fd, buf, msg)) {
            if (msg.type == "PONG") continue;
            if (msg.type == "SYS" || msg.type == "LOGIN_OK")
                std::cout << "\n[系统]: " << msg.content << "\n> " << std::flush;
            else if (msg.type == "PRIVATE")
                std::cout << "\n[私聊] " << msg.sender << ": " << msg.content << "\n> " << std::flush;
            else
                std::cout << "\n[群聊] " << msg.sender << ": " << msg.content << "\n> " << std::flush;
        }
    }
}

void ping_thread(int fd) {
    std::unique_lock<std::mutex> lk(ping_mtx);
    while (running) {
        if (ping_cv.wait_for(lk, std::chrono::seconds(10), []{ return !running.load(); })) break;
        Connection::send_frame(fd, {"PING", my_username, "Server", ""});
    }
}

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(8888);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }
    std::thread tr(recv_thread, fd), tp(ping_thread, fd);
    std::cout << "输入用户名:\n> ";
    std::string line;
    while (running && std::getline(std::cin, line)) {
        std::string in = trim(line);
        if (in.empty()) { std::cout << "> "; continue; }
        if (my_username.empty()) {
            my_username = in;
            Connection::send_frame(fd, {"LOGIN", my_username, "Server", ""});
        } else if (in == "/quit") { running = false; ping_cv.notify_all(); break; }
        else if (in.find("/to ") == 0) {
            std::stringstream ss(in); std::string _,t,m; ss >> _ >> t; std::getline(ss,m); m = trim(m);
            if (!t.empty() && !m.empty()) Connection::send_frame(fd, {"PRIVATE",my_username,t,m});
            else std::cout << "/to <user> <msg>\n> ";
        } else if (in.find("/all ") == 0) {
            std::string m = trim(in.substr(5));
            if (!m.empty()) Connection::send_frame(fd, {"CHAT",my_username,"ALL",m});
            else std::cout << "/all <msg>\n> ";
        } else { Connection::send_frame(fd, {"CHAT",my_username,"ALL",in}); }
        std::cout << "> " << std::flush;
    }
    running = false; ping_cv.notify_all();
    if (tr.joinable()) tr.join(); if (tp.joinable()) tp.join();
    close(fd); return 0;
}
