# TCP 聊天服务器

用 C++ 写的聊天服务器，基于 epoll 实现。

## 功能

- 用户注册、登录（MySQL 存储）
- 群聊：登录后可以发 `/all` 给所有在线用户
- 私聊：`/to <用户名> <消息>` 指定一个人发
- 心跳检测：30 秒无响应自动断开
- 离线消息暂存：对方不在线时消息存到 Redis，但目前还没做上线自动推送

## 协议

消息格式是 4 字节长度头（网络字节序）+ 内容，消息内容是"长度+数据"的方式依次编码 type、sender、target、content 四个字段。

## 依赖

- hiredis（Redis 客户端库）
- mysqlclient（MySQL 客户端库）
- pthread
- CMake >= 3.10

## 编译和运行

```bash
mkdir build && cd build
cmake ..
make

# 先启动 MySQL 和 Redis
# MySQL: 创建数据库 CREATE DATABASE chat;
redis-server &

# 启动服务端
./bin/Server

# 新终端启动客户端
./bin/Client
```

## 客户端命令

```
/register <用户名> <密码>   注册
/login <用户名> <密码>      登录
/all <消息>                 群发
/to <用户名> <消息>         私聊
/quit                       退出
```

## 后续优化

- 密码用 SHA256 哈希，不存明文
- 上线自动推送离线消息
- 用线程池处理消息，避免阻塞 epoll 事件循环
