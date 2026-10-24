#include "UdpRelay.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "room/RoomManager.h"

using namespace std;

namespace {
constexpr int kRtpPort = 10001;
}

UdpRelay::UdpRelay(RoomManager& rooms) : rooms_(rooms) {
    start();
}

UdpRelay::~UdpRelay() {
    stop();
}

// ============================================================
// 创建 UDP socket，绑定 0.0.0.0:10001，启动接收线程
// ============================================================
bool UdpRelay::start() {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
        perror("UdpRelay socket");
        return false;
    }

    int opt = 1;
    setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(kRtpPort);

    if (bind(fd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("UdpRelay bind");
        close(fd_);
        fd_ = -1;
        return false;
    }

    running_ = true;

    int rc = pthread_create(&thread_, nullptr, threadEntry, this);
    if (rc != 0) {
        cerr << "UdpRelay pthread_create error: " << rc << endl;
        running_ = false;
        close(fd_);
        fd_ = -1;
        return false;
    }

    threadStarted_ = true;

    cout << "[UdpRelay] listening on UDP " << kRtpPort << endl;
    return true;
}

// 停止线程并关闭 socket
void UdpRelay::stop() {
    running_ = false;

    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }

    if (threadStarted_) {
        pthread_join(thread_, nullptr);
        threadStarted_ = false;
    }
}

void* UdpRelay::threadEntry(void* raw) {
    UdpRelay* relay = static_cast<UdpRelay*>(raw);
    relay->run();
    return nullptr;
}

// ============================================================
// 收包循环
//
// recvfrom 会拿到：
//   - 数据 buf
//   - 长度 n
//   - 发送方地址 from（IP + 源端口）
// ============================================================
void UdpRelay::run() {
    char buf[65536];
    sockaddr_in from{};
    socklen_t fromLen = sizeof(from);

    while (running_) {
        ssize_t n = recvfrom(fd_, buf, sizeof(buf), 0,
                             (sockaddr*)&from, &fromLen);

        if (n <= 0) {
            if (errno == EINTR) continue;
            break;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        const int srcPort = ntohs(from.sin_port);

        SenderInfo sender;
        if (!rooms_.findSender(ip, srcPort, sender)) {
            // 不知道是谁发的，丢弃
            continue;
        }

        // 取同房间内除发送者外的其他成员
        auto targets = rooms_.otherMembers(sender.room_id, sender.user_id);

        for (const auto& target : targets) {
            if (target.rtp_port <= 0 || target.rtp_ip.empty()) {
                continue;
            }

            sockaddr_in dst{};
            dst.sin_family = AF_INET;
            dst.sin_port = htons(target.rtp_port);

            if (inet_pton(AF_INET, target.rtp_ip.c_str(),
                          &dst.sin_addr) != 1) {
                continue;
            }

            sendto(fd_, buf, n, 0,
                   (sockaddr*)&dst, sizeof(dst));
        }
    }
}
