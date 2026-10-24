#pragma once

// ============================================================
// session/ClientSession.h
//
// 一条客户端 TCP 连接对应一个 ClientSession 对象。
//
// 原来写在 main.cpp 里的：
//   - clientThread 的收包循环
//   - handleLine
//   - handleRegister / handleLogin / handleHeartbeat
//   - 房间相关的 handleCreateRoom / handleListRooms
//     / handleJoinRoom / handleLeaveRoom
//
// 现在都搬到这个类里。每个连接 new 一个对象，
// 线程结束时 delete，析构函数负责 close(fd)。
// ============================================================

#include <string>

#include "common/Session.h"
#include "json.hpp"

class UserStore;
class SessionManager;
class RoomManager;

class ClientSession {
public:
    ClientSession(int fd,
                  std::string peer,
                  UserStore& users,
                  SessionManager& sessions,
                  RoomManager& rooms);

    ~ClientSession();

    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    // 线程入口：一直收，直到客户端断开
    void run();

private:
    void handleLine(const std::string& line);

    // 注册/登录/心跳
    void handleRegister(const nlohmann::json& j);
    void handleLogin(const nlohmann::json& j);
    void handleHeartbeat(const nlohmann::json& j);
    void handleRegisterRtp(const nlohmann::json& j);

    // 房间
    void handleCreateRoom(const nlohmann::json& j);
    void handleListRooms(const nlohmann::json& j);
    void handleJoinRoom(const nlohmann::json& j);
    void handleLeaveRoom(const nlohmann::json& j);
    void handleBroadcast(const nlohmann::json& j);

    // 给房间内所有成员发 room_users
    void sendRoomUsers(int room_id);

    // 只给当前连接发
    void sendJson(const nlohmann::json& j);
    bool sendAll(const std::string& data);

    int fd_;                  // 当前连接的 socket
    std::string peer_;        // "127.0.0.1:54321"
    std::string peerIp_;      // 只保存 IP，register_rtp 时用
    std::string recvBuf_;     // TCP 拆包缓冲区
    Session session_;         // 当前连接的登录状态

    UserStore& users_;        // 全局用户表
    SessionManager& sessions_;  // 全局在线表
    RoomManager& rooms_;      // 全局房间表
};
