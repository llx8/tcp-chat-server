#include <iostream>
#include <unordered_map>
#include <vector>
#include <deque>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "Protocol.hpp"
#include "ThreadPool.hpp"

using namespace std::chrono;

struct Session {
    int fd;
    std::string username;
    std::string recv_buffer;
    system_clock::time_point last_active;
};

class ChatServer {
    std::shared_mutex rw_mtx;
    std::unordered_map<std::string, std::shared_ptr<Session>> online_users;
    std::unordered_map<std::string, std::vector<Message>> offline_messages;
    std::deque<Message> chat_history;

    void add_history(const Message& msg) {
        if (chat_history.size() > 50) chat_history.pop_front();
        chat_history.push_back(msg);
    }

public:
    void handle_client(int fd) {
        auto session = std::make_shared<Session>();
        session->fd = fd;
        session->last_active = system_clock::now();

        Message req;
        while (Connection::recv_frame(fd, session->recv_buffer, req)) {
            session->last_active = system_clock::now(); // 刷新心跳

            if (req.type == "PING") {
                Connection::send_frame(fd, {"PONG", "Server", req.sender, ""});
                continue;
            }

            if (req.type == "LOGIN") {
                std::unique_lock<std::shared_mutex> lock(rw_mtx);
                session->username = req.sender;
                online_users[req.sender] = session;
                
                Connection::send_frame(fd, {"SYS", "Server", req.sender, "Login Success"});
                std::cout << "User " << req.sender << " connected successfully.\n";
                
                // 推送离线消息
                if (offline_messages.count(req.sender)) {
                    for (const auto& m : offline_messages[req.sender]) {
                        Connection::send_frame(fd, m);
                    }
                    offline_messages.erase(req.sender);
                }
            }
            else if (req.type == "CHAT") {
                std::shared_lock<std::shared_mutex> lock(rw_mtx); // 读锁优化并发
                // add_history(req); // 若记录历史需改为写锁或独立锁
                for (const auto& [name, s] : online_users) {
                    if (name != session->username) {
                        Connection::send_frame(s->fd, req);
                    }
                }
            }
            else if (req.type == "PRIVATE") {
                std::unique_lock<std::shared_mutex> lock(rw_mtx);
                if (online_users.count(req.target)) {
                    Connection::send_frame(online_users[req.target]->fd, req);
                } else {
                    offline_messages[req.target].push_back(req);
                    Connection::send_frame(fd, {"SYS", "Server", req.sender, "Target offline. Message saved."});
                }
            }
        }
        
        // 清理断开的连接
        if (!session->username.empty()) {
            std::unique_lock<std::shared_mutex> lock(rw_mtx);
            online_users.erase(session->username);
            std::cout << "User " << session->username << " disconnected.\n";
        }
        close(fd);
    }

    void heartbeat_monitor() {
        while (true) {
            std::this_thread::sleep_for(seconds(10));
            auto now = system_clock::now();
            
            std::unique_lock<std::shared_mutex> lock(rw_mtx);
            for (auto it = online_users.begin(); it != online_users.end(); ) {
                if (duration_cast<seconds>(now - it->second->last_active).count() > 30) {
                    std::cout << "User " << it->first << " timeout.\n";
                    close(it->second->fd);
                    it = online_users.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
};

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8888);

    bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);

    int epoll_fd = epoll_create1(0);
    epoll_event ev{}, events[10];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &ev);

    ThreadPool pool(5);
    ChatServer server;
    
    // 启动心跳检测线程
    std::thread monitor(&ChatServer::heartbeat_monitor, &server);
    monitor.detach();

    std::cout << "Modern C++ Epoll Server started on port 8888\n";

    while (true) {
        int nfds = epoll_wait(epoll_fd, events, 10, -1);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                int client_fd = accept(server_fd, nullptr, nullptr);
                if (client_fd >= 0) {
                    pool.enqueue([client_fd, &server] {
                        server.handle_client(client_fd);
                    });
                }
            }
        }
    }
    return 0;
}