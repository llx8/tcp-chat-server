#include <iostream>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "Protocol.hpp"

struct ClientConn {
    int fd = -1;
    std::string username;
    std::string recv_buffer;
    std::string write_buffer;
    enum State { CONNECTED, LOGGED_IN } state = CONNECTED;
};

class ChatServer {
public:
    std::unordered_map<int, std::shared_ptr<ClientConn>> connections_;
    std::shared_mutex rw_mtx;

    void on_readable(ClientConn& conn) {
        char buf[65536];
        ssize_t n = recv(conn.fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                remove_connection(conn.fd);
            }
            return;
        }
        conn.recv_buffer.append(buf, n);
        while (conn.recv_buffer.size() >= 4) {
            uint32_t net_len;
            std::memcpy(&net_len, conn.recv_buffer.data(), 4);
            uint32_t payload_len = ntohl(net_len);
            if (conn.recv_buffer.size() < 4 + payload_len) break;
            std::string payload = conn.recv_buffer.substr(4, payload_len);
            conn.recv_buffer.erase(0, 4 + payload_len);
            Message msg = Message::deserialize(payload);
            handle_message(conn, msg);
        }
        if (!conn.write_buffer.empty()) do_write(conn);
    }

    void do_write(ClientConn& conn) {
        while (!conn.write_buffer.empty()) {
            ssize_t sent = send(conn.fd, conn.write_buffer.data(), conn.write_buffer.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                remove_connection(conn.fd); return;
            }
            if (sent == 0) break;
            conn.write_buffer.erase(0, sent);
        }
    }

    void queue_send(ClientConn& conn, const Message& msg) {
        std::string payload = msg.serialize();
        uint32_t net_len = htonl(payload.size());
        conn.write_buffer.append(reinterpret_cast<char*>(&net_len), 4);
        conn.write_buffer.append(payload);
    }

    void remove_connection(int fd) {
        if (connections_.count(fd)) connections_.erase(fd);
        close(fd);
    }

private:
    void handle_message(ClientConn& conn, const Message& msg) {
        if (msg.type == "PING") { queue_send(conn, {"PONG","Server",msg.sender,""}); return; }
        if (msg.type == "LOGIN") {
            conn.username = msg.sender;
            conn.state = ClientConn::LOGGED_IN;
            queue_send(conn, {"LOGIN_OK","Server",msg.sender,"welcome"});
            std::cout << "User " << msg.sender << " logged in (fd=" << conn.fd << ")\n";
            return;
        }
        if (msg.type == "CHAT") {
            std::shared_lock<std::shared_mutex> lock(rw_mtx);
            for (auto& p : connections_)
                if (p.first != conn.fd && p.second->state == ClientConn::LOGGED_IN)
                    queue_send(*p.second, msg);
            return;
        }
        if (msg.type == "PRIVATE") {
            for (auto& p : connections_)
                if (p.second->username == msg.target && p.second->state == ClientConn::LOGGED_IN)
                    { queue_send(*p.second, msg); return; }
            queue_send(conn, {"SYS","Server",msg.sender,"user offline"});
        }
    }
};

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1; setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(8888);
    bind(server_fd, (sockaddr*)&addr, sizeof(addr)); listen(server_fd, SOMAXCONN);
    int flags = fcntl(server_fd, F_GETFL, 0); fcntl(server_fd, F_SETFL, flags | O_NONBLOCK);
    int epfd = epoll_create1(0);
    struct epoll_event ev{}; ev.events = EPOLLIN; ev.data.fd = server_fd; epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev);
    const int MAX = 1024; struct epoll_event events[MAX];
    ChatServer server;
    std::cout << "Server started on port 8888\n";
    while (true) {
        int n = epoll_wait(epfd, events, MAX, -1);
        if (n < 0) { if (errno == EINTR) continue; break; }
        for (int i = 0; i < n; i++) {
            int fd = events[i].data.fd;
            if (fd == server_fd) {
                while (true) {
                    int cfd = accept(server_fd, nullptr, nullptr);
                    if (cfd < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) break; break; }
                    int cf = fcntl(cfd, F_GETFL, 0); fcntl(cfd, F_SETFL, cf | O_NONBLOCK);
                    auto conn = std::make_shared<ClientConn>(); conn->fd = cfd;
                    server.connections_[cfd] = conn;
                    struct epoll_event cev{}; cev.events = EPOLLIN | EPOLLET; cev.data.fd = cfd;
                    epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &cev);
                    std::cout << "New connection fd=" << cfd << "\n";
                }
            } else {
                auto it = server.connections_.find(fd);
                if (it == server.connections_.end()) { epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr); continue; }
                if (events[i].events & (EPOLLHUP | EPOLLERR)) { server.remove_connection(fd); continue; }
                if (events[i].events & EPOLLIN) server.on_readable(*it->second);
                it = server.connections_.find(fd); if (it == server.connections_.end()) continue;
                if (!it->second->write_buffer.empty()) {
                    struct epoll_event cev{}; cev.events = EPOLLIN | EPOLLOUT | EPOLLET; cev.data.fd = fd;
                    epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &cev);
                }
                if (events[i].events & EPOLLOUT) {
                    server.do_write(*it->second);
                    it = server.connections_.find(fd);
                    if (it != server.connections_.end() && it->second->write_buffer.empty()) {
                        struct epoll_event cev{}; cev.events = EPOLLIN | EPOLLET; cev.data.fd = fd;
                        epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &cev);
                    }
                }
            }
        }
    }
    close(server_fd); close(epfd); return 0;
}
