#pragma once

// ============================================================
// room/RoomManager.h
// 管理所有房间：创建、列表、加入、离开、成员查询。
// 第五阶段又加了：
//   - 记录成员 RTP 地址
//   - 根据 UDP 包的源 IP/端口找到发送者
//   - 取出房间内除发送者外的其他成员
// ============================================================

#include <pthread.h>

#include <string>
#include <vector>

#include "Room.h"

// UDP 中继找到的发送者信息
struct SenderInfo {
    int room_id = 0;
    int user_id = 0;
};

class RoomManager {
public:
    RoomManager();
    ~RoomManager();

    RoomManager(const RoomManager&) = delete;
    RoomManager& operator=(const RoomManager&) = delete;

    // 创建房间，房主自动加入
    int createRoom(const std::string& name,
                   int host_id,
                   const std::string& host_name,
                   int host_fd,
                   int& out_room_id);

    // 加入房间
    bool joinRoom(int room_id,
                  int user_id,
                  const std::string& username,
                  int fd);

    // 离开房间
    bool leaveRoom(int room_id, int user_id);

    // 房间列表副本
    std::vector<Room> listRooms();

    // 某个房间的成员副本
    std::vector<RoomMember> members(int room_id);

    // 用户是否在指定房间
    bool isInRoom(int room_id, int user_id);

    // 房间是否存在
    bool roomExists(int room_id);

    // 记录某个成员的 RTP 接收地址
    bool setMemberRtp(int room_id,
                      int user_id,
                      const std::string& rtp_ip,
                      int rtp_port);

    // 根据 UDP 包的源 IP/源端口找到是哪个用户发的。
    // 第一次看到某个源端口时，会把它学进成员的 rtp_src_port。
    bool findSender(const std::string& src_ip,
                    int src_port,
                    SenderInfo& out);

    // 取房间内除 exclude_user_id 外的其他成员
    std::vector<RoomMember> otherMembers(int room_id, int exclude_user_id);

private:
    pthread_mutex_t mutex_;
    std::map<int, Room> rooms_;
    int next_id_ = 2001;
};
