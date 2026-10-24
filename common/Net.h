#pragma once

// ============================================================
// common/Net.h
// 通用 TCP 发送工具。
//
// 服务器里所有“发一行 JSON”的地方都走这里，
// 避免每个文件都重复写 send 循环。
// ============================================================

#include <string>

#include "json.hpp"

// 把 data 全部发完。
// TCP 的 send 可能只发出去一部分，所以必须循环发。
// 返回 false 表示连接已断开或发送失败。
bool sendAll(int fd, const std::string& data);

// 把一个 JSON 对象序列化成一行字符串，末尾补 '\n'，再发出去。
// 协议规定：一条 JSON 消息以 '\n' 结尾。
void sendJson(int fd, const nlohmann::json& j);

// 构造统一的错误响应：
// {
//   "type": "login_resp",
//   "code": 4,
//   "message": "wrong password"
// }
nlohmann::json makeError(const std::string& resp_type,
                         int code,
                         const std::string& msg);
