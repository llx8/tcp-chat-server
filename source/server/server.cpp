#include <iostream>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include "Protocol.hpp"
#include "Database.hpp"
using namespace std::chrono;

struct ClientConn {
    int fd = -1; std::string username; std::string recv_buffer; std::string write_buffer;
    system_clock::time_point last_active;
    enum State { CONNECTED, LOGGED_IN } state = CONNECTED;
};

class ChatServer {
public:
    std::unordered_map<int, std::shared_ptr<ClientConn>> connections_;
    std::unordered_map<std::string, int> username_to_fd_;
    std::shared_mutex rw_mtx;
    Database* db_ = nullptr;
    void on_readable(ClientConn& conn) {
        char buf[65536]; ssize_t n = recv(conn.fd, buf, sizeof(buf), 0);
        if (n <= 0) { if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) { remove_connection(conn.fd); } return; }
        conn.recv_buffer.append(buf, n); conn.last_active = system_clock::now();
        while (conn.recv_buffer.size() >= 4) {
            uint32_t net_len; std::memcpy(&net_len, conn.recv_buffer.data(), 4); uint32_t plen = ntohl(net_len);
            if (conn.recv_buffer.size() < 4 + plen) break;
            std::string payload = conn.recv_buffer.substr(4, plen); conn.recv_buffer.erase(0, 4 + plen);
            handle_message(conn, Message::deserialize(payload));
        }
        if (!conn.write_buffer.empty()) do_write(conn);
    }
    void do_write(ClientConn& conn) {
        while (!conn.write_buffer.empty()) {
            ssize_t sent = send(conn.fd, conn.write_buffer.data(), conn.write_buffer.size(), MSG_NOSIGNAL);
            if (sent < 0) { if (errno == EAGAIN || errno == EWOULDBLOCK) break; remove_connection(conn.fd); return; }
            if (sent == 0) break; conn.write_buffer.erase(0, sent);
        }
    }
    void queue_send(ClientConn& conn, const Message& msg) {
        std::string p = msg.serialize(); uint32_t nl = htonl(p.size());
        conn.write_buffer.append(reinterpret_cast<char*>(&nl), 4); conn.write_buffer.append(p);
    }
    void check_heartbeat() {
        auto now = system_clock::now(); std::vector<int> tc;
        { std::shared_lock<std::shared_mutex> lk(rw_mtx);
          for (auto& p : connections_) { int fd = p.first; auto& c = p.second; if (duration_cast<seconds>(now - c->last_active).count() > 30) tc.push_back(fd); } }
        for (int fd : tc) { std::cout << "fd=" << fd << " timeout\n"; remove_connection(fd); }
    }
    void remove_connection(int fd) { if (connections_.count(fd)) { auto& c = connections_[fd]; if (!c->username.empty()) { username_to_fd_.erase(c->username); std::cout << "User " << c->username << " disconnected\n"; } connections_.erase(fd); } close(fd); }
private:
    void handle_message(ClientConn& conn, const Message& msg) {
        if (msg.type == "PING") { queue_send(conn, {"PONG","Server",msg.sender,""}); return; }
        if (msg.type == "REGISTER") { std::string u = msg.sender; std::string p = msg.content; if (!db_->register_user(u, msg.content)) { queue_send(conn, {"REG_FAIL","Server",u,"user exists"}); return; } queue_send(conn, {"REG_OK","Server",u,"register success"}); std::cout << "User " << u << " registered\n"; return; }
        if (msg.type == "LOGIN") { std::string u = msg.sender; if (!db_->login_user(u, msg.content)) { queue_send(conn, {"LOGIN_FAIL","Server",u,"wrong username or password"}); return; } std::unique_lock<std::shared_mutex> lk(rw_mtx); conn.username = u; conn.state = ClientConn::LOGGED_IN; username_to_fd_[u] = conn.fd; queue_send(conn, {"LOGIN_OK","Server",u,"welcome"}); std::cout << "User " << u << " logged in (fd=" << conn.fd << ")\n"; return; }
        if (msg.type == "CHAT") { std::shared_lock<std::shared_mutex> lk(rw_mtx); for (auto& p : connections_) if (p.first != conn.fd && p.second->state == ClientConn::LOGGED_IN) queue_send(*p.second, msg); return; }
        if (msg.type == "PRIVATE") { std::shared_lock<std::shared_mutex> lk(rw_mtx); auto it = username_to_fd_.find(msg.target); if (it != username_to_fd_.end()) { queue_send(*connections_[it->second], msg); } else { queue_send(conn, {"SYS","Server",msg.sender,"user offline"}); } return; }
    }
};

int main() {
    int sfd = socket(AF_INET, SOCK_STREAM, 0); int opt = 1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(8888);
    bind(sfd, (sockaddr*)&addr, sizeof(addr)); listen(sfd, SOMAXCONN);
    int fl = fcntl(sfd, F_GETFL, 0); fcntl(sfd, F_SETFL, fl | O_NONBLOCK);
    int ep = epoll_create1(0); struct epoll_event ev{}; ev.events = EPOLLIN; ev.data.fd = sfd; epoll_ctl(ep, EPOLL_CTL_ADD, sfd, &ev);
    const int MX = 1024; struct epoll_event evs[MX];
    Database db("127.0.0.1", "root", "", "chat"); if (!db.connect()) { std::cerr << "MySQL init failed\n"; return 1; }
    ChatServer server; server.db_ = &db;
    auto lhb = steady_clock::now(); std::cout << "Server started on port 8888\n";
    while (true) {
        int n = epoll_wait(ep, evs, MX, 1000); if (n < 0) { if (errno == EINTR) continue; break; }
        auto now = steady_clock::now(); if (duration_cast<seconds>(now - lhb).count() >= 10) { server.check_heartbeat(); lhb = now; }
        for (int i = 0; i < n; i++) {
            int fd = evs[i].data.fd;
            if (fd == sfd) { while (true) { int cfd = accept(sfd, nullptr, nullptr); if (cfd < 0) { if (errno == EAGAIN||errno == EWOULDBLOCK) break; break; } int cf = fcntl(cfd, F_GETFL, 0); fcntl(cfd, F_SETFL, cf|O_NONBLOCK); auto cn = std::make_shared<ClientConn>(); cn->fd = cfd; cn->last_active = system_clock::now(); server.connections_[cfd] = cn; struct epoll_event cev{}; cev.events = EPOLLIN|EPOLLET; cev.data.fd = cfd; epoll_ctl(ep, EPOLL_CTL_ADD, cfd, &cev); std::cout << "New fd=" << cfd << "\n"; } }
            else { auto it = server.connections_.find(fd); if (it == server.connections_.end()) { epoll_ctl(ep, EPOLL_CTL_DEL, fd, nullptr); continue; } if (evs[i].events & (EPOLLHUP|EPOLLERR)) { server.remove_connection(fd); continue; } if (evs[i].events & EPOLLIN) server.on_readable(*it->second); it = server.connections_.find(fd); if (it == server.connections_.end()) continue; if (!it->second->write_buffer.empty()) { struct epoll_event cev{}; cev.events = EPOLLIN|EPOLLOUT|EPOLLET; cev.data.fd = fd; epoll_ctl(ep, EPOLL_CTL_MOD, fd, &cev); } if (evs[i].events & EPOLLOUT) { server.do_write(*it->second); it = server.connections_.find(fd); if (it != server.connections_.end() && it->second->write_buffer.empty()) { struct epoll_event cev{}; cev.events = EPOLLIN|EPOLLET; cev.data.fd = fd; epoll_ctl(ep, EPOLL_CTL_MOD, fd, &cev); } } }
        }
    }
    close(sfd); close(ep); return 0;
}
