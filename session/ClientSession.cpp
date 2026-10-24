#include "ClientSession.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <ctime>
#include <iostream>

#include "auth/UserStore.h"
#include "common/Net.h"
#include "room/RoomManager.h"
#include "session/SessionManager.h"

using json = nlohmann::json;
using namespace std;

ClientSession::ClientSession(int fd,
                             std::string peer,
                             UserStore& users,
                             SessionManager& sessions,
                             RoomManager& rooms)
    : fd_(fd),
      peer_(std::move(peer)),
      users_(users),
      sessions_(sessions),
      rooms_(rooms) {
    // peer_ 形如 "127.0.0.1:54321"，取出 IP 部分
    const size_t pos = peer_.find(':');
    peerIp_ = (pos == string::npos) ? peer_ : peer_.substr(0, pos);
}

ClientSession::~ClientSession() {
    close(fd_);
}

bool ClientSession::sendAll(const string& data) {
    return ::sendAll(fd_, data);
}

void ClientSession::sendJson(const json& j) {
    ::sendJson(fd_, j);
}

// 原来 clientThread 里的收包循环，现在搬到这里。
// TCP 是字节流，一次 recv 可能收到半条，也可能收到两条，
// 所以必须用 recvBuf_ 缓存，再按 '\n' 拆。
void ClientSession::run() {
    char buf[1024];

    while (1) {
        int n = recv(fd_, buf, sizeof(buf), 0);

        if (n > 0) {
            // 读到的数据放到缓冲区里面。
            recvBuf_.append(buf, n);

            if (recvBuf_.size() > 1024 * 1024) {
                break;
            }

            size_t pos;
            while ((pos = recvBuf_.find('\n')) != string::npos) {
                string line = recvBuf_.substr(0, pos);
                recvBuf_.erase(0, pos + 1);

                // 如果最后一个是 \r，就删除
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (line.empty()) continue;

                handleLine(line);
            }
        } else if (n == 0) {
            cout << "[close " << peer_ << "]" << endl;
            break;
        } else if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
    }

    // 如果断开时还在房间里，先把他从房间移除，
    // 通知剩下的人，再清理在线表。
    if (session_.room_id != 0) {
        const int room_id = session_.room_id;

        // 先拿成员，删掉自己后再通知其他人
        auto members = rooms_.members(room_id);

        if (rooms_.leaveRoom(room_id, session_.user_id)) {
            json left = {
                {"type", "user_left"},
                {"room_id", room_id},
                {"user_id", session_.user_id},
                {"username", session_.username}
            };

            for (const auto& m : members) {
                if (m.user_id != session_.user_id) {
                    ::sendJson(m.fd, left);
                }
            }

            sendRoomUsers(room_id);
        }

        session_.room_id = 0;
    }

    // 当客户端断开连接时，把用户从在线表里删除。
    if (session_.logged_in) {
        sessions_.removeOnline(session_.username, fd_);
    }
}

// 把客户端发来的字符串，转换成 json 对象，
// 提取 type 字段，对指令进行判断。
void ClientSession::handleLine(const string& line) {
    json j;

    try {
        j = json::parse(line);
    } catch (const json::exception&) {
        sendJson(makeError("error", 30, "invalid json"));
        return;
    }

    const string type = j.value("type", "");

    cout << "[line " << peer_ << "] type=" << type
         << " json=" << line << endl;

    if (type == "register") {
        handleRegister(j);
    } else if (type == "login") {
        handleLogin(j);
    } else if (type == "heartbeat") {
        handleHeartbeat(j);
    } else if (type == "register_rtp") {
        handleRegisterRtp(j);
    } else if (type == "create_room") {
        handleCreateRoom(j);
    } else if (type == "list_rooms") {
        handleListRooms(j);
    } else if (type == "join_room") {
        handleJoinRoom(j);
    } else if (type == "leave_room") {
        handleLeaveRoom(j);
    } else if (type == "broadcast") {
        handleBroadcast(j);
    } else {
        sendJson(makeError("error", 30, "unknown type"));
    }
}

// 处理注册
void ClientSession::handleRegister(const json& j) {
    const std::string username = j.value("username", "");
    const std::string password = j.value("password_hash", "");

    int user_id = 0;
    int rc = users_.registerUser(username, password, user_id);

    if (rc == 1) {
        sendJson(makeError("register_resp", 1, "username empty"));
    } else if (rc == 2) {
        sendJson(makeError("register_resp", 2, "password empty"));
    } else if (rc == 5) {
        sendJson(makeError("register_resp", 5, "username exists"));
    } else if (rc != 0) {
        sendJson(makeError("register_resp", 1, "register failed"));
    } else {  // 注册成功
        sendJson(json{
            {"type", "register_resp"},
            {"code", 0},
            {"message", "ok"}
        });
    }
}

// 处理登录
void ClientSession::handleLogin(const json& j) {
    const string username = j.value("username", "");
    const string password = j.value("password_hash", "");

    // 校验用户名和密码
    int user_id = 0;
    int rc = users_.login(username, password, user_id);

    if (rc == 4) {
        sendJson(makeError("login_resp", 4,
                           "wrong username or password"));
        return;
    }
    if (rc != 0) {
        sendJson(makeError("login_resp", 1, "login failed"));
        return;
    }

    // 检查这个用户名是否已经被别的连接登录。
    // trySetOnline 把“检查 + 写入在线表”放在同一把锁里。
    if (!sessions_.trySetOnline(username, fd_)) {
        sendJson(makeError("login_resp", 3, "already online"));
        return;
    }

    // 如果这个连接之前登录过别的账号，先清掉旧记录
    if (session_.logged_in && session_.username != username) {
        sessions_.removeOnline(session_.username, fd_);
    }

    // 校验成功后，记录登录状态，写一个 token 通行证给服务器。
    session_.logged_in = true;
    session_.user_id = user_id;
    session_.username = username;
    session_.token = "token-" + std::to_string(user_id) + "-" +
                     std::to_string(std::time(nullptr));

    // 返回登录成功
    sendJson(json{
        {"type", "login_resp"},
        {"code", 0},
        {"message", "ok"},
        {"user_id", user_id},
        {"session_token", session_.token},
        {"expires_in", 1800}
    });
}

// token 是通行证，token 对上了，才保持心跳。
void ClientSession::handleHeartbeat(const json& j) {
    const string token = j.value("session_token", "");

    if (!session_.logged_in || token != session_.token) {
        sendJson(makeError("heartbeat_resp", 10, "invalid session"));
        return;
    }

    sendJson(json{
        {"type", "heartbeat_resp"},
        {"code", 0}
    });
}

// 处理 RTP 端口注册
//
// 客户端在加入房间后，会告诉服务器：
//   “我的 UDP 接收端口是 rtp_port，你往这里发 RTP”。
//
// 服务器先记在 Session 里，第五阶段做 UDP 中继时再用。
void ClientSession::handleRegisterRtp(const json& j) {
    const string token = j.value("session_token", "");

    if (!session_.logged_in || token != session_.token) {
        sendJson(makeError("register_rtp_resp", 10, "not logged in"));
        return;
    }

    const int rtp_port = j.value("rtp_port", 0);

    if (rtp_port <= 0 || rtp_port > 65535) {
        sendJson(makeError("register_rtp_resp", 1, "invalid rtp port"));
        return;
    }

    session_.rtp_port = rtp_port;

    // 如果已经在房间里，把 RTP 地址也记录到房间成员上，
    // 后面 UdpRelay 转发时要用。
    if (session_.room_id != 0) {
        rooms_.setMemberRtp(session_.room_id,
                            session_.user_id,
                            peerIp_,
                            rtp_port);
    }

    sendJson(json{
        {"type", "register_rtp_resp"},
        {"code", 0},
        {"message", "ok"},
        {"relay_port", 10001}
    });
}

// 处理聊天广播
//
// 客户端发：
//   {"type":"broadcast","session_token":"...","message":"大家好"}
//
// 服务器：
//   1. 给发送者回 broadcast_resp
//   2. 给同房间其他人发 broadcast
//
// 注意：广播不经过 UDP，走 TCP 8888。
void ClientSession::handleBroadcast(const json& j) {
    const string token = j.value("session_token", "");

    if (!session_.logged_in || token != session_.token) {
        sendJson(makeError("broadcast_resp", 10, "not logged in"));
        return;
    }

    if (session_.room_id == 0) {
        sendJson(makeError("broadcast_resp", 30, "not in room"));
        return;
    }

    const string text = j.value("message", "");

    // 先给发送者回响应
    sendJson(json{
        {"type", "broadcast_resp"},
        {"code", 0},
        {"message", "广播成功"}
    });

    // 再转发给同房间其他成员
    json bcast = {
        {"type", "broadcast"},
        {"from", session_.username},
        {"room_id", session_.room_id},
        {"message", text}
    };

    auto members = rooms_.otherMembers(session_.room_id, session_.user_id);

    for (const auto& m : members) {
        ::sendJson(m.fd, bcast);
    }
}

// 把 users 列表抽象成一个函数：
// 告诉这个房间里的每一个成员，现在房间里有谁，
// 客户端拿它刷新参会者列表。
void ClientSession::sendRoomUsers(int room_id) {
    auto members = rooms_.members(room_id);

    json users = json::array();
    for (const auto& m : members) {
        users.push_back(json{
            {"user_id", m.user_id},
            {"username", m.username}
        });
    }

    json msg = {
        {"type", "room_users"},
        {"room_id", room_id},
        {"users", users}
    };

    for (const auto& m : members) {
        ::sendJson(m.fd, msg);
    }
}

// 创建房间需要登录，登录成功后才能创建。
void ClientSession::handleCreateRoom(const json& j) {
    if (!session_.logged_in) {
        sendJson(makeError("create_room_resp", 10, "not logged in"));
        return;
    }

    // 已经在会议里面了，就不能再创建房间了
    if (session_.room_id != 0) {
        sendJson(makeError("create_room_resp", 22, "already in room"));
        return;
    }

    const string room_name = j.value("room_name", "");

    int room_id = 0;
    rooms_.createRoom(
        room_name,
        session_.user_id,
        session_.username,
        fd_,
        room_id
    );

    session_.room_id = room_id;

    // 1. 给创建者回 create_room_resp
    sendJson(json{
        {"type", "create_room_resp"},
        {"code", 0},
        {"room_id", room_id},
        {"room_name", room_name.empty() ? "我的会议室" : room_name}
    });

    // 2. 给房间里的人发 room_users
    sendRoomUsers(room_id);
}

// 处理房间列表
void ClientSession::handleListRooms(const json& j) {
    const string token = j.value("session_token", "");

    if (!session_.logged_in || token != session_.token) {
        sendJson(makeError("room_list_resp", 10, "not logged in"));
        return;
    }

    auto rooms = rooms_.listRooms();

    // ["2001:测试房间", "2002:另一个房间"]
    json user_list = json::array();
    for (const auto& room : rooms) {
        user_list.push_back(
            std::to_string(room.id) + ":" + room.name
        );
    }

    sendJson(json{
        {"type", "room_list_resp"},
        {"code", 0},
        {"user_list", user_list}
    });
}

// 处理用户加入房间
void ClientSession::handleJoinRoom(const json& j) {
    const string token = j.value("session_token", "");

    if (!session_.logged_in || token != session_.token) {
        sendJson(makeError("join_room_resp", 10, "not logged in"));
        return;
    }

    const int room_id = j.value("room_id", 0);

    // 1. 房间不存在
    if (room_id <= 0 || !rooms_.roomExists(room_id)) {
        sendJson(makeError("join_room_resp", 20, "room not found"));
        return;
    }

    // 2. 已经在目标房间
    if (session_.room_id == room_id) {
        sendJson(makeError("join_room_resp", 22, "already in room"));
        return;
    }

    // 3. 如果已经在别的房间，先离开旧房间
    if (session_.room_id != 0) {
        const int old_room_id = session_.room_id;

        // 先拿到旧成员，再删自己
        auto old_members = rooms_.members(old_room_id);

        rooms_.leaveRoom(old_room_id, session_.user_id);

        json left = {
            {"type", "user_left"},
            {"room_id", old_room_id},
            {"user_id", session_.user_id},
            {"username", session_.username}
        };

        for (const auto& m : old_members) {
            if (m.user_id != session_.user_id) {
                ::sendJson(m.fd, left);
            }
        }

        sendRoomUsers(old_room_id);
        session_.room_id = 0;
    }

    // 4. 加入新房间
    if (!rooms_.joinRoom(room_id, session_.user_id,
                         session_.username, fd_)) {
        sendJson(makeError("join_room_resp", 22, "join failed"));
        return;
    }

    session_.room_id = room_id;

    // 如果客户端在 join 之前就 register_rtp 了，
    // 加入房间后要补写成员的 RTP 地址。
    if (session_.rtp_port > 0) {
        rooms_.setMemberRtp(room_id,
                            session_.user_id,
                            peerIp_,
                            session_.rtp_port);
    }

    // 5. 先通知房间里的其他人
    auto members = rooms_.members(room_id);

    json joined = {
        {"type", "user_joined"},
        {"room_id", room_id},
        {"user_id", session_.user_id},
        {"username", session_.username}
    };

    for (const auto& m : members) {
        if (m.user_id != session_.user_id) {
            ::sendJson(m.fd, joined);
        }
    }

    // 6. 再给加入者回 join_room_resp
    sendJson(json{
        {"type", "join_room_resp"},
        {"code", 0},
        {"message", "ok"},
        {"room_id", room_id},
        {"rtp_port", 10001},
        {"rtp_host", "127.0.0.1"}
    });

    // 7. 最后给所有人发 room_users
    sendRoomUsers(room_id);
}

// 处理离开房间
void ClientSession::handleLeaveRoom(const json& j) {
    const string token = j.value("session_token", "");

    if (!session_.logged_in || token != session_.token) {
        sendJson(makeError("leave_room_resp", 10, "not logged in"));
        return;
    }

    const int room_id = j.value("room_id", 0);

    if (session_.room_id == 0 || session_.room_id != room_id) {
        sendJson(makeError("leave_room_resp", 22, "not in this room"));
        return;
    }

    // 先拿到成员列表，离开后再给剩下的人发通知
    auto members = rooms_.members(room_id);

    if (!rooms_.leaveRoom(room_id, session_.user_id)) {
        sendJson(makeError("leave_room_resp", 20, "room not found"));
        return;
    }

    session_.room_id = 0;

    // 1. 给离开者回响应
    sendJson(json{
        {"type", "leave_room_resp"},
        {"code", 0},
        {"message", "ok"}
    });

    // 2. 通知房间里剩下的人
    json left = {
        {"type", "user_left"},
        {"room_id", room_id},
        {"user_id", session_.user_id},
        {"username", session_.username}
    };

    for (const auto& m : members) {
        if (m.user_id != session_.user_id) {
            ::sendJson(m.fd, left);
        }
    }

    // 3. 给剩下的人刷新成员列表
    sendRoomUsers(room_id);
}
