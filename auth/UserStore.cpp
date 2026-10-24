#include "UserStore.h"

UserStore::UserStore() {
    pthread_mutex_init(&mutex, nullptr);
}

UserStore::~UserStore() {
    pthread_mutex_destroy(&mutex);
}

int UserStore::registerUser(const string& username,
                            const string& password,
                            int& out_id) {
    if (username.empty()) return 1;
    if (password.empty()) return 2;

    pthread_mutex_lock(&mutex);

    if (users.count(username) > 0) {
        pthread_mutex_unlock(&mutex);
        return 5;
    }

    User u;
    u.id = next_id++;
    u.password_hash = password;
    users[username] = u;

    out_id = u.id;

    pthread_mutex_unlock(&mutex);
    return 0;
}

int UserStore::login(const string& username,
                     const string& password,
                     int& out_id) {
    if (username.empty()) return 1;

    pthread_mutex_lock(&mutex);

    auto it = users.find(username);//返回一个迭代器，指向键为username的元素，如果不存在则返回users.end()

    if (it == users.end()) {
        pthread_mutex_unlock(&mutex);
        return 4;
    }

    if (it->second.password_hash != password) {
        pthread_mutex_unlock(&mutex);
        return 4;
    }

    out_id = it->second.id;//把新生成的用户ID返回给调用方。

    pthread_mutex_unlock(&mutex);
    return 0;
}
