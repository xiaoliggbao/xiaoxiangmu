#include "RoomManager.h"

RoomManager::RoomManager() {
    pthread_mutex_init(&mutex_, nullptr);
}

RoomManager::~RoomManager() {
    pthread_mutex_destroy(&mutex_);
}

// ============================================================
// 创建房间：房主自动成为第一个成员
// ============================================================
int RoomManager::createRoom(const std::string& name,
                            int host_id,
                            const std::string& host_name,
                            int host_fd,
                            int& out_room_id) {
    pthread_mutex_lock(&mutex_);

    const int room_id = next_id_++;

    Room room;
    room.id = room_id;
    room.name = name.empty() ? "会议室" : name;
    room.host_id = host_id;

    RoomMember host;
    host.user_id = host_id;
    host.username = host_name;
    host.fd = host_fd;

    room.members[host_id] = host;
    rooms_[room_id] = room;

    out_room_id = room_id;

    pthread_mutex_unlock(&mutex_);
    return 0;
}

// 加入房间
bool RoomManager::joinRoom(int room_id,
                           int user_id,
                           const std::string& username,
                           int fd) {
    pthread_mutex_lock(&mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    if (it->second.members.count(user_id) > 0) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    RoomMember m;
    m.user_id = user_id;
    m.username = username;
    m.fd = fd;

    it->second.members[user_id] = m;

    pthread_mutex_unlock(&mutex_);
    return true;
}

// 离开房间
bool RoomManager::leaveRoom(int room_id, int user_id) {
    pthread_mutex_lock(&mutex_);

    auto it = rooms_.find(room_id);
    if (it == rooms_.end()) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    if (it->second.members.erase(user_id) == 0) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    pthread_mutex_unlock(&mutex_);
    return true;
}

// 房间列表副本
std::vector<Room> RoomManager::listRooms() {
    pthread_mutex_lock(&mutex_);

    std::vector<Room> result;
    for (const auto& pair : rooms_) {
        result.push_back(pair.second);
    }

    pthread_mutex_unlock(&mutex_);
    return result;
}

// 某个房间的成员副本
std::vector<RoomMember> RoomManager::members(int room_id) {
    pthread_mutex_lock(&mutex_);

    std::vector<RoomMember> result;

    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        for (const auto& pair : it->second.members) {
            result.push_back(pair.second);
        }
    }

    pthread_mutex_unlock(&mutex_);
    return result;
}

// 用户是否在指定房间
bool RoomManager::isInRoom(int room_id, int user_id) {
    pthread_mutex_lock(&mutex_);

    auto it = rooms_.find(room_id);
    bool in = (it != rooms_.end() &&
               it->second.members.count(user_id) > 0);

    pthread_mutex_unlock(&mutex_);
    return in;
}

// 房间是否存在
bool RoomManager::roomExists(int room_id) {
    pthread_mutex_lock(&mutex_);

    bool exists = (rooms_.find(room_id) != rooms_.end());

    pthread_mutex_unlock(&mutex_);
    return exists;
}

// ============================================================
// 记录成员的 RTP 接收地址
//
// 客户端 register_rtp 时调用。
// 服务器往这个 ip:port 转发 RTP。
// ============================================================
bool RoomManager::setMemberRtp(int room_id,
                               int user_id,
                               const std::string& rtp_ip,
                               int rtp_port) {
    pthread_mutex_lock(&mutex_);

    auto room_it = rooms_.find(room_id);
    if (room_it == rooms_.end()) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    auto member_it = room_it->second.members.find(user_id);
    if (member_it == room_it->second.members.end()) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    member_it->second.rtp_ip = rtp_ip;
    member_it->second.rtp_port = rtp_port;

    pthread_mutex_unlock(&mutex_);
    return true;
}

// ============================================================
// 根据 UDP 包的源 IP + 源端口找到发送者
//
// 客户端“注册的接收端口”和“实际发包的源端口”可能不一样，
// 所以第一次收到某个源端口时，要把它学习到 rtp_src_port。
//
// 三轮匹配：
//   1. 精确匹配已经学过的源端口
//   2. 优先认房主（教师端）
//   3. 同 IP 下第一个还没学习过源端口的成员
// ============================================================
bool RoomManager::findSender(const std::string& src_ip,
                             int src_port,
                             SenderInfo& out) {
    pthread_mutex_lock(&mutex_);

    // 第一轮：精确匹配
    for (auto& room_pair : rooms_) {
        Room& room = room_pair.second;

        for (auto& member_pair : room.members) {
            RoomMember& m = member_pair.second;

            if (m.rtp_port > 0 &&
                m.rtp_ip == src_ip &&
                m.rtp_src_port == src_port) {
                out.room_id = room.id;
                out.user_id = m.user_id;
                pthread_mutex_unlock(&mutex_);
                return true;
            }
        }
    }

    // 第二轮：优先认房主
    for (auto& room_pair : rooms_) {
        Room& room = room_pair.second;

        for (auto& member_pair : room.members) {
            RoomMember& m = member_pair.second;

            if (m.rtp_port > 0 &&
                m.rtp_ip == src_ip &&
                m.rtp_src_port == 0 &&
                room.host_id == m.user_id) {
                m.rtp_src_port = src_port;
                out.room_id = room.id;
                out.user_id = m.user_id;
                pthread_mutex_unlock(&mutex_);
                return true;
            }
        }
    }

    // 第三轮：同 IP 下第一个还没绑定源端口的成员
    for (auto& room_pair : rooms_) {
        Room& room = room_pair.second;

        for (auto& member_pair : room.members) {
            RoomMember& m = member_pair.second;

            if (m.rtp_port > 0 &&
                m.rtp_ip == src_ip &&
                m.rtp_src_port == 0) {
                m.rtp_src_port = src_port;
                out.room_id = room.id;
                out.user_id = m.user_id;
                pthread_mutex_unlock(&mutex_);
                return true;
            }
        }
    }

    pthread_mutex_unlock(&mutex_);
    return false;
}

// 取房间内除 exclude_user_id 外的其他成员
std::vector<RoomMember> RoomManager::otherMembers(int room_id,
                                                  int exclude_user_id) {
    pthread_mutex_lock(&mutex_);

    std::vector<RoomMember> result;

    auto it = rooms_.find(room_id);
    if (it != rooms_.end()) {
        for (const auto& pair : it->second.members) {
            if (pair.first != exclude_user_id) {
                result.push_back(pair.second);
            }
        }
    }

    pthread_mutex_unlock(&mutex_);
    return result;
}
