#include "Database.hpp"
#include <iostream>
#include <cstring>

Database::Database(const std::string& host, const std::string& user,
                   const std::string& pass, const std::string& db)
    : host_(host), user_(user), pass_(pass), db_(db) {}

Database::~Database() {
    if (conn_) mysql_close(conn_);
}

bool Database::connect() {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        std::cerr << "mysql_init failed\n";
        return false;
    }
    if (!mysql_real_connect(conn_, host_.c_str(), user_.c_str(),
                            pass_.c_str(), db_.c_str(), 0, nullptr, 0)) {
        std::cerr << "mysql connect failed: " << mysql_error(conn_) << "\n";
        return false;
    }
    mysql_query(conn_,
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INT AUTO_INCREMENT PRIMARY KEY,"
        "  username VARCHAR(64) UNIQUE,"
        "  password VARCHAR(64)"
        ")");
    return true;
}

bool Database::register_user(const std::string& username, const std::string& password) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "INSERT INTO users (username, password) VALUES ('%s', '%s')",
        username.c_str(), password.c_str());
    if (mysql_query(conn_, buf)) {
        return false; // duplicate username
    }
    return true;
}

bool Database::login_user(const std::string& username, const std::string& password) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "SELECT id FROM users WHERE username='%s' AND password='%s'",
        username.c_str(), password.c_str());
    if (mysql_query(conn_, buf)) return false;
    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) return false;
    bool ok = mysql_num_rows(res) > 0;
    mysql_free_result(res);
    return ok;
}
