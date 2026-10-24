#include "Net.h"
#include <sys/socket.h>
#include <cerrno>
using json = nlohmann::json;

// ============================================================
// 把一整条数据发完
//
// send 不一定一次发完：
//   send(fd, "hello", 5, ...) 可能返回 2
// 表示只发出去了 "he"，剩下的 "llo" 还要继续发。
//
// 所以用 sent 记录已经发了多少字节，直到 sent == data.size()。
// ============================================================
bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;

    while (sent < data.size()) {
        ssize_t n = send(fd, data.data() + sent,
                         data.size() - sent, MSG_NOSIGNAL);

        if (n < 0) {
            // 被信号打断，不算错误，继续发
            if (errno == EINTR) continue;
            return false;
        }

        if (n == 0) {
            // 对端已经关闭
            return false;
        }

        sent += static_cast<size_t>(n);
    }

    return true;
}

// ============================================================
// 发一条 JSON
//
// dump() 把 json 对象变成字符串，例如：
//   {"type":"login_resp","code":0}
//
// 协议要求每条消息以 '\n' 结尾，接收方才能按行拆包。
// ============================================================
void sendJson(int fd, const json& j) {
    sendAll(fd, j.dump() + "\n");
}

// ============================================================
// 构造错误响应
//
// resp_type 是“针对哪个请求的响应”，例如：
//   login      -> login_resp
//   register   -> register_resp
//   join_room  -> join_room_resp
//
// code 是错误码，message 是给人看的描述。
// ============================================================
json makeError(const std::string& resp_type, int code,
               const std::string& msg) {
    return json{
        {"type", resp_type},
        {"code", code},
        {"message", msg}
    };
}
