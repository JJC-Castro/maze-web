#include "RlTelemetry.h"

#include "RLSpec.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#ifdef MAZE_WEB
#include <sys/stat.h>   // ::mkdir
#else
#include <direct.h>     // _mkdir：Windows 专有
#endif
#include <string>
#include <vector>

namespace rl
{
    namespace terminal
    {
        namespace
        {
            // 跨平台建目录：Windows 是 _mkdir（<direct.h>），
            // POSIX / Emscripten 是 mkdir（<sys/stat.h>）
            void MkDir(const char* dir)
            {
                if (dir == nullptr || *dir == 0) return;
#ifdef MAZE_WEB
                ::mkdir(dir, 0777);
#else
                _mkdir(dir);
#endif
            }

            bool gEnabled = true;
            const char* gMode = "auto";
            const std::chrono::steady_clock::time_point gT0 =
                std::chrono::steady_clock::now();

            double Now()
            {
                const std::chrono::duration<double> d =
                    std::chrono::steady_clock::now() - gT0;
                return d.count();
            }

            // 一行一条 + 立刻 flush：终端要实时看到，重定向到文件也要能边跑边 tail
            void Emit(const char* fmt, ...)
            {
                if (!gEnabled) return;
                std::printf("[RL] %8.3f ", Now());
                va_list ap;
                va_start(ap, fmt);
                std::vprintf(fmt, ap);
                va_end(ap);
                std::printf("\n");
                std::fflush(stdout);
            }

            // ---- 路线回放状态（一局一份，RouteBegin 时覆盖重置） ----
            std::vector<std::string> gRouteRows;   // 迷宫字符行（'W' / 'S' / 'E' ...）
            std::vector<int> gRouteCells;          // 实际走过的格，每格两个数：gx, gy
            int gRouteStartX = 0, gRouteStartY = 0;
            int gRouteExitX = 0, gRouteExitY = 0;
            bool gRouteActive = false;
            std::string gRouteDumpDir;

            const char* ActName(int a)
            {
                return (a >= 0 && a < kNActions) ? kActName[a] : "?";
            }
        }

        void SetEnabled(bool on) { gEnabled = on; }
        bool Enabled() { return gEnabled; }

        void Toggle()
        {
            gEnabled = !gEnabled;
            if (gEnabled)
            {
                Emit("note log=on");
            }
            else
            {
                // 关掉时最后再打一行，让终端里能看到"日志已关"
                std::printf("[RL] %8.3f note log=off\n", Now());
                std::fflush(stdout);
            }
        }

        void SetMode(const char* mode) { gMode = (mode != nullptr) ? mode : "?"; }
        const char* Mode() { return gMode; }

        void Session(const char* mode, int width, int height,
                     int entryGx, int entryGy, int exitGx, int exitGy,
                     int optimalSteps, int maxSteps, int hasWeights)
        {
            Emit("session mode=%s w=%d h=%d entry=%d,%d exit=%d,%d "
                 "optimal=%d maxSteps=%d weights=%d",
                 (mode != nullptr) ? mode : "?", width, height,
                 entryGx, entryGy, exitGx, exitGy,
                 optimalSteps, maxSteps, hasWeights);
        }

        void Reset(int episode, int width, int height,
                   int entryGx, int entryGy, int exitGx, int exitGy,
                   int optimalSteps, int maxSteps)
        {
            Emit("reset ep=%d w=%d h=%d entry=%d,%d exit=%d,%d optimal=%d maxSteps=%d",
                 episode, width, height, entryGx, entryGy, exitGx, exitGy,
                 optimalSteps, maxSteps);
        }

        void Step(int episode, int step, int maxSteps, int action,
                  int gx, int gy, int dist, float reward,
                  double returnSum, bool done, bool success,
                  int treasures, int hits, double seconds)
        {
            Emit("step ep=%d n=%d/%d act=%s(%d) cell=%d,%d dist=%d "
                 "r=%+.3f ret=%+.3f done=%d ok=%d treas=%d hits=%d sec=%.1f",
                 episode, step, maxSteps, ActName(action), action, gx, gy, dist,
                 (double)reward, returnSum, done ? 1 : 0, success ? 1 : 0,
                 treasures, hits, seconds);
        }

        void EpisodeEnd(int episode, int steps, int optimalSteps, double ret,
                        bool success, double stepsPerSecond)
        {
            Emit("episode ep=%d steps=%d optimal=%d ret=%+.3f success=%d sps=%.1f",
                 episode, steps, optimalSteps, ret, success ? 1 : 0, stepsPerSecond);
        }

        void Halt(const char* reason, int steps, int maxSteps,
                  int gx, int gy, int dist)
        {
            Emit("halt reason=%s steps=%d/%d cell=%d,%d dist=%d",
                 (reason != nullptr) ? reason : "?", steps, maxSteps, gx, gy, dist);
        }

        void WalkerStall(int tgx, int tgy, int pgx, int pgy, double seconds)
        {
            Emit("stall mode=%s target=%d,%d player=%d,%d seconds=%.1f",
                 gMode, tgx, tgy, pgx, pgy, seconds);
        }

        void Note(const char* fmt, ...)
        {
            if (!gEnabled) return;
            std::printf("[RL] %8.3f note ", Now());
            va_list ap;
            va_start(ap, fmt);
            std::vprintf(fmt, ap);
            va_end(ap);
            std::printf("\n");
            std::fflush(stdout);
        }

        void SetRouteDumpDir(const char* dir)
        {
            gRouteDumpDir = (dir != nullptr) ? dir : "";
            if (!gRouteDumpDir.empty()) MkDir(gRouteDumpDir.c_str());
        }

        void RouteBegin(const std::vector<std::string>& mazeRows,
                        int startGx, int startGy, int exitGx, int exitGy)
        {
            gRouteRows = mazeRows;
            gRouteCells.clear();
            gRouteStartX = startGx;
            gRouteStartY = startGy;
            gRouteExitX = exitGx;
            gRouteExitY = exitGy;
            gRouteActive = !mazeRows.empty();
            if (gRouteActive)
            {
                gRouteCells.push_back(startGx);
                gRouteCells.push_back(startGy);
            }
        }

        void RoutePush(int gx, int gy)
        {
            // 撞墙时格子不变也要记：距离不降 = deviation，这正是要看的东西
            if (!gRouteActive) return;
            gRouteCells.push_back(gx);
            gRouteCells.push_back(gy);
        }

        void RouteReport(const std::vector<int>& distFlat, int width, int height,
                         int episode, bool success, bool printMap)
        {
            if (!gRouteActive || width <= 0 || height <= 0) return;
            if ((int)distFlat.size() < width * height) return;

            const auto distAt = [&](int gx, int gy) -> int
                {
                    if (gx < 0 || gy < 0 || gx >= width || gy >= height) return -1;
                    return distFlat[(size_t)gy * width + gx];
                };

            const int cellCount = (int)(gRouteCells.size() / 2);
            const int steps = (cellCount > 0) ? cellCount - 1 : 0;
            const int optimal = distAt(gRouteStartX, gRouteStartY);

            std::vector<int> visits((size_t)width * height, 0);
            int deviations = 0;
            for (int i = 0; i < cellCount; ++i)
            {
                const int gx = gRouteCells[i * 2];
                const int gy = gRouteCells[i * 2 + 1];
                if (gx >= 0 && gy >= 0 && gx < width && gy < height)
                    ++visits[(size_t)gy * width + gx];
                if (i == 0) continue;
                const int prev = distAt(gRouteCells[(i - 1) * 2], gRouteCells[(i - 1) * 2 + 1]);
                const int cur = distAt(gx, gy);
                if (prev < 0 || cur < 0 || cur != prev - 1) ++deviations;   // 没往出口推进
            }
            int revisits = 0;
            for (int v : visits) if (v > 1) ++revisits;

            const int minimal = (deviations == 0 && optimal >= 0 && steps == optimal) ? 1 : 0;
            Emit("route ep=%d steps=%d optimal=%d minimal=%d deviations=%d revisits=%d success=%d",
                 episode, steps, optimal, minimal, deviations, revisits, success ? 1 : 0);

            std::vector<std::string> lines;

            // 起点 -> 出口的最短路径（完美迷宫唯一）：沿距离场梯度走一遍标出来
            std::vector<char> onPath((size_t)width * height, 0);
            if (optimal >= 0)
            {
                int cx = gRouteStartX, cy = gRouteStartY;
                onPath[(size_t)cy * width + cx] = 1;
                for (int guard = 0; guard <= optimal; ++guard)
                {
                    const int d = distAt(cx, cy);
                    if (d <= 0) break;
                    const int nx[4] = { cx + 1, cx - 1, cx, cx };
                    const int ny[4] = { cy, cy, cy + 1, cy - 1 };
                    bool moved = false;
                    for (int k = 0; k < 4; ++k)
                    {
                        if (distAt(nx[k], ny[k]) == d - 1)
                        {
                            cx = nx[k]; cy = ny[k];
                            onPath[(size_t)cy * width + cx] = 1;
                            moved = true;
                            break;
                        }
                    }
                    if (!moved) break;
                }
            }

            if (printMap && !gRouteRows.empty())
            {
                for (int gy = 0; gy < (int)gRouteRows.size() && gy < height; ++gy)
                {
                    const std::string& src = gRouteRows[gy];
                    std::string row;
                    for (int gx = 0; gx < width; ++gx)
                    {
                        const char base = (gx < (int)src.size()) ? src[gx] : 'W';
                        char ch = base;
                        // 只有"走了但不在最短路径上"的格才标 x；最短路径上的格一律标 o
                        if (base != 'W' && onPath[(size_t)gy * width + gx]) ch = 'o';
                        else if (base != 'W' && visits[(size_t)gy * width + gx] > 0) ch = 'x';
                        if (gx == gRouteStartX && gy == gRouteStartY) ch = 'S';
                        if (gx == gRouteExitX && gy == gRouteExitY) ch = 'F';
                        row.push_back(ch);
                    }
                    lines.push_back(row);
                    Emit("map %s", row.c_str());
                }
            }

            if (!gRouteDumpDir.empty())
            {
            MkDir(gRouteDumpDir.c_str());
            const std::string path = gRouteDumpDir + "/route_last.txt";   // 正斜杠 Windows 也认
            FILE* f = std::fopen(path.c_str(), "w");                      // fopen_s 非 MSVC 没有
            if (f != nullptr)
            {
            std::fprintf(f, "# RL route ep=%d steps=%d optimal=%d minimal=%d deviations=%d revisits=%d success=%d\n",
                         episode, steps, optimal, minimal, deviations, revisits, success ? 1 : 0);
            std::fprintf(f, "# o=shortest path  x=visited  S=start  F=exit  .=free  W=wall\n");
            for (const std::string& row : lines) std::fprintf(f, "%s\n", row.c_str());
            std::fprintf(f, "route=");
            for (int i = 0; i < cellCount; ++i)
                std::fprintf(f, "%s%d,%d", (i == 0) ? "" : " ",
                             gRouteCells[i * 2], gRouteCells[i * 2 + 1]);
            std::fprintf(f, "\n");
            std::fclose(f);
            }
            }

            // 一份路线只导一次：报完就清掉，免得 Done 之后继续走的碎片再报一条假路线
            gRouteActive = false;
            gRouteCells.clear();
        }
    }
}
