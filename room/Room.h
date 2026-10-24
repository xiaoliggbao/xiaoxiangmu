#pragma once

// ============================================================
// room/Room.h
// 一个房间和房间成员的数据结构。
//
// 除了原来的 user_id/username/fd，
// 第五阶段 RTP 还需要知道每个成员：
//   - 注册的 RTP 接收 IP
//   - 注册的 RTP 接收端口
//   - 实际发包的源端口（服务器学习出来的）
// ============================================================

#include <map>
#include <string>

struct RoomMember {
    int user_id = 0;              // 用户编号
    std::string username;         // 用户名
    int fd = -1;                  // TCP 连接，发信令用

    std::string rtp_ip;           // 客户端注册的 RTP 接收 IP
    int rtp_port = -1;            // 客户端注册的 RTP 接收端口
    int rtp_src_port = 0;         // 服务器学习到的实际发包源端口
};

struct Room {
    int id = 0;                   // 房间号
    std::string name;             // 房间名
    int host_id = 0;              // 房主 user_id

    // user_id -> 成员信息
    std::map<int, RoomMember> members;
};
