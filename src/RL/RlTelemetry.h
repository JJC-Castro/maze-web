#pragma once
#include <string>
#include <vector>
// ---------------------------------------------------------------------------
// RlTelemetry.h : 把 RL 的运行结果实时导出到终端（stdout）。
//
// 为什么需要它：RL 自动通关 / 训练桥以前只能盯着 3D 画面看"跑没跑对"，
// 一旦卡住既不知道卡在哪一步、也不知道为什么，只能靠截图猜。现在每一步都会
// 打一行纯文本，终端里直接看、grep、重定向到文件画图都行。
//
// 输出格式（一行一条，字段顺序固定，方便脚本解析）：
//   [RL]    0.000 session mode=auto w=15 h=15 entry=1,1 exit=13,11 optimal=41 maxSteps=164 weights=1
//   [RL]    1.254 step ep=1 n=1/164 act=RIGHT(3) cell=2,1 dist=40 r=+0.990 ret=+0.990 done=0 ok=0
//   [RL]   12.500 episode ep=1 steps=41 optimal=41 ret=+40.600 success=1 sps=8.4
//   [RL]   13.010 halt reason=timeout steps=164/164 cell=7,9 dist=23
//   [RL]    3.002 stall mode=auto target=5,7 player=4,7 seconds=2.0
//   [RL]    0.100 note bridge client connected
//
// 开关：
//   - 默认开启；游戏中按 F3 随时开关（开关本身也打一行）
//   - 训练桥按 kBridgeStepInterval 步抽样打一行，避免把终端刷爆
// ---------------------------------------------------------------------------

namespace rl
{
    namespace terminal
    {
        // 训练桥（几千步/秒）不能每步都打，按这个间隔抽样
        constexpr int kBridgeStepInterval = 200;

        void SetEnabled(bool on);
        bool Enabled();
        void Toggle();

        // 当前模式："auto"（机器人代打）/ "bridge"（Python 训练）
        void SetMode(const char* mode);
        const char* Mode();

        // ---- 会话 / 回合级 ----
        void Session(const char* mode, int width, int height,
                     int entryGx, int entryGy, int exitGx, int exitGy,
                     int optimalSteps, int maxSteps, int hasWeights);
        void Reset(int episode, int width, int height,
                   int entryGx, int entryGy, int exitGx, int exitGy,
                   int optimalSteps, int maxSteps);
        void EpisodeEnd(int episode, int steps, int optimalSteps, double ret,
                        bool success, double stepsPerSecond);

        // ---- 单步级 ----
        // treasures / hits / seconds 是 v2 追加的字段（宝物数、被咬次数、
        // 该局已耗时），默认 0 以兼容只关心网格的调用方。
        void Step(int episode, int step, int maxSteps, int action,
                  int gx, int gy, int dist, float reward,
                  double returnSum, bool done, bool success,
                  int treasures = 0, int hits = 0, double seconds = 0.0);

        // ---- 异常 / 事件 ----
        void Halt(const char* reason, int steps, int maxSteps,
                  int gx, int gy, int dist);
        void WalkerStall(int tgx, int tgy, int pgx, int pgy, double seconds);
        void Note(const char* fmt, ...);

        // ---- 路线回放（看这条路线是不是最简路径） ---------------------------
        // 用法：进入 RL 会话时 RouteBegin(迷宫字符行, 起点, 终点)，每一步决策后
        // RoutePush(当前格)，一局结束调用 RouteReport(距离场, ...)。
        // RouteReport 打一行汇总（steps / optimal / minimal / deviations / revisits）
        // 再打一张 ASCII 地图：o=最短路径, x=实际走过的格, S=起点, F=终点，
        // 同时把同样内容写到 <dumpDir>\route_last.txt（没设 dumpDir 就只打终端）。
        void SetRouteDumpDir(const char* dir);
        void RouteBegin(const std::vector<std::string>& mazeRows,
                        int startGx, int startGy, int exitGx, int exitGy);
        void RoutePush(int gx, int gy);
        void RouteReport(const std::vector<int>& distFlat, int width, int height,
                         int episode, bool success, bool printMap);
    }
}
