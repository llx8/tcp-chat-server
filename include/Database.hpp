#pragma once
#include <string>
#include <mysql/mysql.h>

class Database {
public:
    Database(const std::string& host, const std::string& user,
             const std::string& pass, const std::string& db);
    ~Database();

    bool connect();
    bool register_user(const std::string& username, const std::string& password);
    bool login_user(const std::string& username, const std::string& password);

private:
    MYSQL* conn_ = nullptr;
    std::string host_, user_, pass_, db_;
};
