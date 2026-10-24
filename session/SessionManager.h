#pragma once

// ============================================================
// session/SessionManager.h
// 全局在线用户表。
//
// 作用：
//   1. 记录“用户名 -> 哪个 fd 在线”；
//   2. 登录时判断这个用户名是否已经被别的连接登录；
//   3. 连接断开时删除在线记录。
//
// 这个表会被多个客户端线程同时访问，所以内部用 pthread_mutex 保护。
// ============================================================

#include <pthread.h>

#include <map>
#include <string>

class SessionManager {
public:
    SessionManager();
    ~SessionManager();

    // 不允许拷贝：内部有 pthread_mutex_t
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // 尝试把 username 标记为 fd 在线。
    // 如果这个用户名已经被别的 fd 登录，返回 false。
    // “检查 + 写入”在同一把锁里完成，避免两个线程同时登录都成功。
    bool trySetOnline(const std::string& username, int fd);

    // 删除在线记录。
    // 只有 online_[username] == fd 时才删，
    // 防止旧连接断开时误删新连接的在线记录。
    void removeOnline(const std::string& username, int fd);

    // 这个用户名是否被“别的 fd”登录。
    bool isOnlineByOther(const std::string& username, int fd);

    // 查询用户名对应的 fd，不在线返回 -1。
    int fdOf(const std::string& username);

private:
    pthread_mutex_t mutex_;
    std::map<std::string, int> online_;  // 用户名 -> fd
};
