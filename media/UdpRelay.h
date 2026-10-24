#pragma once

// ============================================================
// media/UdpRelay.h
// RTP 中继服务器：监听 UDP 10001。
//
// 收到一个 RTP 包后：
//   1. 根据源 IP + 源端口找到是哪个用户发的
//   2. 找到这个用户所在房间
//   3. 把包转发给房间内其他成员注册的 RTP 地址
// ============================================================

#include <pthread.h>

#include <atomic>

class RoomManager;

class UdpRelay {
public:
    explicit UdpRelay(RoomManager& rooms);
    ~UdpRelay();

    UdpRelay(const UdpRelay&) = delete;
    UdpRelay& operator=(const UdpRelay&) = delete;

    // 绑定 10001 并启动接收线程
    bool start();

    // 停止接收线程
    void stop();

private:
    static void* threadEntry(void* raw);
    void run();

    RoomManager& rooms_;

    int fd_ = -1;
    pthread_t thread_{};
    std::atomic<bool> running_{false};
    bool threadStarted_ = false;
};
