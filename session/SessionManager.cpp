#include "SessionManager.h"

// 创建时初始化锁
SessionManager::SessionManager() {
    pthread_mutex_init(&mutex_, nullptr);
}

// 销毁时销毁锁
SessionManager::~SessionManager() {
    pthread_mutex_destroy(&mutex_);
}

// ============================================================
// 尝试把 username 标记为在线
//
// 返回 true：
//   - 原来不在线，现在写入成功；
//   - 或者原来就是同一个 fd，允许重复登录。
//
// 返回 false：
//   - 这个用户名已经被另一个 fd 登录。
// ============================================================
bool SessionManager::trySetOnline(const std::string& username, int fd) {
    pthread_mutex_lock(&mutex_);

    auto it = online_.find(username);

    if (it != online_.end() && it->second != fd) {
        // 已被别的连接占用
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    online_[username] = fd;

    pthread_mutex_unlock(&mutex_);
    return true;
}

// ============================================================
// 删除在线记录
//
// 必须判断 it->second == fd：
//   连接 A 登录 solo，fd=5
//   连接 A 断开，但此时 solo 已经在新连接 B 上登录，fd=6
//   如果直接 erase("solo")，会把 B 的在线记录删掉。
// ============================================================
void SessionManager::removeOnline(const std::string& username, int fd) {
    pthread_mutex_lock(&mutex_);

    auto it = online_.find(username);
    if (it != online_.end() && it->second == fd) {
        online_.erase(it);
    }

    pthread_mutex_unlock(&mutex_);
}

// 这个用户名是否已经被别的 fd 登录
bool SessionManager::isOnlineByOther(const std::string& username, int fd) {
    pthread_mutex_lock(&mutex_);

    auto it = online_.find(username);
    bool other = (it != online_.end() && it->second != fd);

    pthread_mutex_unlock(&mutex_);
    return other;
}

// 查询用户名的 fd，不在线返回 -1
int SessionManager::fdOf(const std::string& username) {
    pthread_mutex_lock(&mutex_);

    int fd = -1;

    auto it = online_.find(username);
    if (it != online_.end()) {
        fd = it->second;
    }

    pthread_mutex_unlock(&mutex_);
    return fd;
}
