// ===========================================================================
//  RlBridge_web.cpp —— Web 版的 RlBridge「桩实现」
//
//  原来的 RlBridge.cpp 是 Windows 裸 Winsock 写的 TCP 服务端
//  （WSAStartup / socket / bind / accept / #pragma comment(lib,"ws2_32")）。
//  浏览器里既没有裸 socket、也不允许页面自己当 server，所以整块砍掉。
//
//  为什么用"桩"而不是删掉调用点：
//    RlBridge.h 本身是纯 std 的（只有 atomic / mutex / condition_variable），
//    对平台没有依赖。留一个同接口的桩，main.cpp 里的训练桥代码一行都不用改，
//    只是 Start() 永远返回 false —— 菜单里点「RL 训练桥」会看到一句友好提示，
//    其余功能（包括「RL 自动通关」的离线推理）完全不受影响。
//
//  在 CMakeLists.txt 里：Web 编本文件，桌面编 RlBridge.cpp，二选一。
// ===========================================================================
#ifdef MAZE_WEB

#include "RlBridge.h"

namespace rl
{
    RlBridge::~RlBridge() = default;

    bool RlBridge::Start(int /*port*/, std::string* errOut)
    {
        const std::string msg =
            "web: bridge unavailable (browser has no raw TCP sockets)";
        if (errOut != nullptr) *errOut = msg;
        {
            std::lock_guard<std::mutex> lock(viewMutex_);
            view_.connected = false;
            view_.status = "web: disabled";
        }
        return false;
    }

    void RlBridge::Stop() { running_.store(false); }

    bool RlBridge::TakeResetRequest() { return false; }

    void RlBridge::PublishMaze(const Maze& /*maze*/) {}

    BridgeView RlBridge::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(viewMutex_);
        return view_;
    }
}

#endif  // MAZE_WEB