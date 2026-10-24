#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <iostream>
#include <string>

#include "auth/UserStore.h"
#include "media/UdpRelay.h"
#include "room/RoomManager.h"
#include "session/ClientSession.h"
#include "session/SessionManager.h"

using namespace std;

// 全局只有这三个管理器，真正业务都在各自模块里
UserStore g_users;        // 用户表
SessionManager g_sessions;  // 在线表
RoomManager g_rooms;      // 房间表

// 全局 RTP 中继：构造时绑定 UDP 10001 并启动接收线程
UdpRelay g_relay(g_rooms);

// 线程入口：一条连接一个 ClientSession
void* clientThread(void* raw) {
    ClientSession* session = static_cast<ClientSession*>(raw);

    session->run();   // 收包、拆包、分发，都在里面
    delete session;   // 析构时 close(fd)

    return nullptr;
}

int main() {
    signal(SIGPIPE, SIG_IGN);

    int port = 8888;

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        cerr << "创建套接字失败" << endl;
        return 1;
    }

    int opt = 1;
    // 设置端口复用
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};  // 初始化为 0
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(lfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "绑定失败" << endl;
        close(lfd);
        return 1;
    }

    if (listen(lfd, 128) < 0) {
        cerr << "监听失败" << endl;
        close(lfd);
        return 1;
    }

    cout << "[server] listening on " << port << endl;

    while (1) {
        sockaddr_in caddr{};
        socklen_t len = sizeof(caddr);

        int cfd = accept(lfd, (sockaddr*)&caddr, &len);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            cerr << "accept error" << endl;
            break;
        }

        array<char, 16> ip{};
        inet_ntop(AF_INET, &caddr.sin_addr.s_addr, ip.data(), ip.size());

        // ip 和端口号拼接起来
        string peer = string(ip.data()) + ":" +
                      to_string(ntohs(caddr.sin_port));

        cout << "[accept] " << peer << endl;

        ClientSession* session = new ClientSession(
            cfd, peer, g_users, g_sessions, g_rooms);

        pthread_t tid;
        int rc = pthread_create(&tid, nullptr, clientThread, session);
        if (rc != 0) {
            cerr << "创建线程失败" << endl;
            delete session;  // 析构会 close(cfd)
            continue;
        }

        pthread_detach(tid);
    }

    close(lfd);
    return 0;
}
