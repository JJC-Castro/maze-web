#include "Maze.h"
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <cmath>
#include <algorithm>
#include <queue>
#include "Config.h"  
#include "json.hpp"
#include <fstream>      
#include <string>           
#include "json.hpp"      

using json = nlohmann::json;   // ← 新增


Maze::Maze(int sx, int sy)
    : grid(sx, sy)
{
    std::srand((unsigned)std::time(nullptr));
    // 全部初始化为墙
    for (int y = 0; y < grid.height; y++)
    {
        for (int x = 0; x < grid.width; x++)
        {
            grid.at(x, y) = CellType::WALL;
        }
    }
}

// 递归回溯DFS核心
void Maze::dfs(int x, int y)
{
    grid.at(x, y) = CellType::PATH;

    // 四个方向：dx, dy
    int dirs[4][2] = { {0, -2}, {0, 2}, {-2, 0}, {2, 0} };
    // 打乱方向
    for (int i = 0; i < 4; i++)
    {
        int r = std::rand() % 4;
        std::swap(dirs[i], dirs[r]);
    }

    for (auto& d : dirs)
    {
        int nx = x + d[0];
        int ny = y + d[1];
        if (grid.isInBounds(nx, ny) && grid.at(nx, ny) == CellType::WALL)
        {
            // 打通中间一格
            int midX = x + d[0] / 2;
            int midY = y + d[1] / 2;
            grid.at(midX, midY) = CellType::PATH;

            dfs(nx, ny);
        }
    }
}
//该函数为指定格子成为指定类型，其他类型就不能使用这些格子，防止重复
static void SpawnN(std::vector<std::pair<int, int>>& candidates,Grid<CellType>& grid,int n,CellType type)
{
    for (int i = 0; i < n && !candidates.empty(); ++i)
    {
        int idx = std::rand() % candidates.size();
        int x = candidates[idx].first;
        int y = candidates[idx].second;
        grid.at(x, y) = type;

        // 从候选里移除，避免重复
        candidates[idx] = candidates.back();
        candidates.pop_back();
    }
}

void Maze::SpawnTreasures(int scoreCount, int attackCount, int healCount)
{
    std::vector<std::pair<int, int>> candidates;

    // 收集所有通路格（排除入口、出口、边界）
    for (int y = 1; y < grid.height - 1; ++y)
        for (int x = 1; x < grid.width - 1; ++x)
        {
            if (x == entryGx && y == entryGy) continue;
            if (x == exitGx && y == exitGy)  continue;

            if (grid.at(x, y) == CellType::PATH)
                candidates.emplace_back(x, y);
        }

    // 依次投放三种宝物，互不重叠
    SpawnN(candidates, grid, scoreCount, CellType::TREASURE_SCORE);
    SpawnN(candidates, grid, attackCount, CellType::TREASURE_ATTACK);
    SpawnN(candidates, grid, healCount, CellType::TREASURE_HEAL);

    treasureCount = scoreCount + attackCount + healCount;
}

void Maze::generateRecursiveBacktrack()
{
    // 从(1,1)起点生成
    dfs(1, 1);

    // ---------- 入口：左侧边界 (0,1) ----------
    entryGx = 1;  entryGy = 1;              // 内部入口格（玩家出生点）
    grid.at(0, 1) = CellType::EXIT;         // 边界洞

    // ---------- 出口：右侧边界，选一个奇数行 ----------
    // height-2 = 19，是奇数，直接用作出口行
    exitGx = grid.width - 2;                // 19，内部出口格
    exitGy = grid.height - 2;               // 19

    // 保证 (exitGx, exitGy) 是通路（DFS 保证奇数坐标是通路）
    // 在边界 (width-1, exitGy) 开洞
    grid.at(grid.width - 1, exitGy) = CellType::EXIT;
    // 先铺减速带，再撒宝物：减速格不再是通路，宝物自然不会生成在减速带上
    SpawnSlowZones();
    SpawnMines(mineCount);
    SpawnTreasures(TREASURE_SCORE_COUNT,TREASURE_ATTACK_COUNT,TREASURE_HEAL_COUNT);
}

// ============================================================
// 减速带：在通路上随机铺若干段连续格（红色地面区）
// ============================================================
void Maze::SpawnSlowZones()
{
    if (slowZoneSegments <= 0) return;

    const int minCells = (slowZoneMinCells > 0) ? slowZoneMinCells : 1;
    const int maxCells = (slowZoneMaxCells >= minCells) ? slowZoneMaxCells : minCells;
    const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    for (int seg = 0; seg < slowZoneSegments; ++seg)
    {
        // 收集可用的通路起点（排除入口 / 出口）
        std::vector<std::pair<int, int>> starts;
        for (int y = 1; y < grid.height - 1; ++y)
            for (int x = 1; x < grid.width - 1; ++x)
            {
                if (x == entryGx && y == entryGy) continue;
                if (x == exitGx && y == exitGy) continue;
                if (grid.at(x, y) == CellType::PATH)
                    starts.emplace_back(x, y);
            }
        if (starts.empty()) break;

        const std::pair<int, int> start = starts[(size_t)std::rand() % starts.size()];

        // 打乱四个方向，挑一个能连续铺得最长的
        int order[4] = { 0, 1, 2, 3 };
        for (int i = 3; i > 0; --i)
        {
            const int j = std::rand() % (i + 1);
            const int tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }

        const int want = minCells + (std::rand() % (maxCells - minCells + 1));

        int bestLen = 0;
        int bestDir = -1;
        for (int i = 0; i < 4; ++i)
        {
            const int d = order[i];
            int len = 0;
            int cx = start.first;
            int cy = start.second;
            for (int step = 0; step < want; ++step)
            {
                if (!grid.isInBounds(cx, cy)) break;
                if (cx == entryGx && cy == entryGy) break;
                if (cx == exitGx && cy == exitGy) break;
                if (grid.at(cx, cy) != CellType::PATH) break;
                ++len;
                cx += dirs[d][0];
                cy += dirs[d][1];
            }
            if (len > bestLen) { bestLen = len; bestDir = d; }
        }

        // 连续不足 2 格就放弃这一段，保证减速带是“一段路”而不是单格
        if (bestDir < 0 || bestLen < 2) continue;

        int cx = start.first;
        int cy = start.second;
        for (int step = 0; step < bestLen; ++step)
        {
            grid.at(cx, cy) = CellType::SLOW_ZONE;
            cx += dirs[bestDir][0];
            cy += dirs[bestDir][1];
        }
    }
}

bool Maze::IsSlowZone(int gx, int gy) const
{
    if (!grid.isInBounds(gx, gy)) return false;
    return grid.at(gx, gy) == CellType::SLOW_ZONE;
}

bool Maze::IsSlowZoneAt(Vector3 worldPos) const
{
    int gx = 0;
    int gy = 0;
    WorldToGrid(worldPos, gx, gy);
    return IsSlowZone(gx, gy);
}

float Maze::SlowFactorAt(Vector3 worldPos) const
{
    return IsSlowZoneAt(worldPos) ? slowZoneFactor : 1.0f;
}
// ============================================================
// 地雷：优先布在入口 -> 出口的最短通路上，并互相拉开距离
// ============================================================
void Maze::SpawnMines(int count)
{
    if (count <= 0) return;

    const int w = grid.width;
    const int h = grid.height;
    const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };

    const auto index = [w](int x, int y) { return (size_t)y * w + x; };
    const auto walkable = [&](int x, int y) {
        return grid.isInBounds(x, y) && grid.at(x, y) != CellType::WALL;
    };

    // 在可走格上做 BFS，返回每个格子到 (sx, sy) 的通路距离（-1 表示不可达）
    const auto bfsFrom = [&](int sx, int sy) {
        std::vector<int> d((size_t)w * h, -1);
        if (!walkable(sx, sy)) return d;
        std::queue<std::pair<int, int>> q;
        d[index(sx, sy)] = 0;
        q.push({ sx, sy });
        while (!q.empty())
        {
            const std::pair<int, int> c = q.front();
            q.pop();
            for (const auto& dir : dirs)
            {
                const int nx = c.first + dir[0];
                const int ny = c.second + dir[1];
                if (!walkable(nx, ny)) continue;
                if (d[index(nx, ny)] >= 0) continue;
                d[index(nx, ny)] = d[index(c.first, c.second)] + 1;
                q.push({ nx, ny });
            }
        }
        return d;
    };

    const std::vector<int> distFromEntry = bfsFrom(entryGx, entryGy);

    // 从出口回溯到入口，得到入口 -> 出口的最短通路
    std::vector<std::pair<int, int>> path;
    if (distFromEntry[index(exitGx, exitGy)] >= 0)
    {
        int cx = exitGx;
        int cy = exitGy;
        while (!(cx == entryGx && cy == entryGy))
        {
            path.push_back({ cx, cy });
            const int here = distFromEntry[index(cx, cy)];
            bool moved = false;
            for (const auto& dir : dirs)
            {
                const int nx = cx + dir[0];
                const int ny = cy + dir[1];
                if (!walkable(nx, ny)) continue;
                if (distFromEntry[index(nx, ny)] == here - 1)
                {
                    cx = nx;
                    cy = ny;
                    moved = true;
                    break;
                }
            }
            if (!moved) break;
        }
        path.push_back({ entryGx, entryGy });
        std::reverse(path.begin(), path.end());
    }

    // 起步距离：地雷至少要离起点这么远的"通路距离"，避免刚出门就遇到
    // 有效值 = max(绝对格数地板, 最短通路长度 × 比例)，随迷宫大小自动缩放
    const int shortestPathLength = distFromEntry[index(exitGx, exitGy)];
    const int startByRatio = (shortestPathLength > 0)
        ? (int)((float)shortestPathLength * MINE_MIN_START_PATH_RATIO)
        : 0;
    const int startPreferred =
        (startByRatio > MINE_MIN_START_PATH_CELLS) ? startByRatio
                                                   : MINE_MIN_START_PATH_CELLS;

    // 从严格到宽松逐级尝试：只有严格值放不满 count 颗时才整体放宽
    const int rawStartSteps[4] = {
        startPreferred, startPreferred * 2 / 3, startPreferred / 2, 0
    };
    int startSteps[4] = { 0, 0, 0, 0 };
    int startStepCount = 0;
    for (int i = 0; i < 4; ++i)
    {
        if (startStepCount == 0 || rawStartSteps[i] < startSteps[startStepCount - 1])
        {
            startSteps[startStepCount++] = rawStartSteps[i];
        }
    }

    const auto neighborHas = [&](int x, int y, CellType type) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0) continue;
                const int nx = x + dx;
                const int ny = y + dy;
                if (grid.isInBounds(nx, ny) && grid.at(nx, ny) == type) return true;
            }
        return false;
    };

    std::vector<std::vector<int>> distFromMines;

    const auto canPlace = [&](int x, int y, int minGap, int minStart) {
        if (grid.at(x, y) != CellType::PATH) return false;               // 只能放通路
        if (x == exitGx && y == exitGy) return false;                    // 不能压出口
        // 起点周围留空，避免一出生没走几步就踩雷
        if (abs(x - entryGx) <= MINE_SPAWN_CLEAR_RADIUS &&
            abs(y - entryGy) <= MINE_SPAWN_CLEAR_RADIUS) return false;
        // 再要求离起点的"通路距离"够远：这是防止刚出门就遇到地雷的主要手段
        if (minStart > 0)
        {
            const int dEntry = distFromEntry[index(x, y)];
            if (dEntry >= 0 && dEntry < minStart) return false;
        }
        if (neighborHas(x, y, CellType::SLOW_ZONE)) return false;        // 不紧贴减速带
        if (neighborHas(x, y, CellType::MINE)) return false;             // 不紧贴已有地雷
        for (const std::vector<int>& field : distFromMines)
        {
            const int d = field[index(x, y)];
            if (d >= 0 && d < minGap) return false;                      // 按通路距离拉开
        }
        return true;
    };

    // 已经放下的地雷：整体回退重试时用来还原
    std::vector<std::pair<int, int>> placedMines;
    const auto clearPlacedMines = [&]() {
        for (const std::pair<int, int>& m : placedMines)
        {
            grid.at(m.first, m.second) = CellType::PATH;
        }
        placedMines.clear();
        distFromMines.clear();
    };

    // 从最严格的起步距离开始；放不满 count 颗就整体放宽一档重来
    for (int si = 0; si < startStepCount; ++si)
    {
        clearPlacedMines();
        const int minStart = startSteps[si];

        for (int i = 0; i < count; ++i)
        {
            bool placedOne = false;

            // 间距从 MINE_MIN_PATH_GAP 逐级放宽到 3，但相邻永远不放
            for (int gap = MINE_MIN_PATH_GAP; gap >= 3 && !placedOne; --gap)
            {
                // 先只考虑入口 -> 出口最短通路，放不下再退到全部通路格
                for (int pass = 0; pass < 2 && !placedOne; ++pass)
                {
                    std::vector<std::pair<int, int>> candidates;
                    if (pass == 0)
                    {
                        for (const auto& cell : path)
                            if (grid.at(cell.first, cell.second) == CellType::PATH)
                                candidates.push_back(cell);
                    }
                    else
                    {
                        for (int y = 1; y < h - 1; ++y)
                            for (int x = 1; x < w - 1; ++x)
                                if (grid.at(x, y) == CellType::PATH)
                                    candidates.push_back({ x, y });
                    }
                    if (candidates.empty()) continue;

                    int bestIndex = -1;
                    long bestScore = -1;
                    for (size_t ci = 0; ci < candidates.size(); ++ci)
                    {
                        const int cx = candidates[ci].first;
                        const int cy = candidates[ci].second;
                        if (!canPlace(cx, cy, gap, minStart)) continue;

                        // 离已有地雷越远越优先；还没放雷时就看离入口的通路距离
                        long score = 1 << 20;
                        if (distFromMines.empty())
                        {
                            score = distFromEntry[index(cx, cy)];
                        }
                        else
                        {
                            for (const std::vector<int>& field : distFromMines)
                            {
                                const int d = field[index(cx, cy)];
                                if (d >= 0 && d < score) score = d;
                            }
                        }
                        if (score > bestScore)
                        {
                            bestScore = score;
                            bestIndex = (int)ci;
                        }
                    }

                    if (bestIndex < 0) continue;

                    const std::pair<int, int> chosen = candidates[(size_t)bestIndex];
                    grid.at(chosen.first, chosen.second) = CellType::MINE;
                    placedMines.push_back(chosen);
                    distFromMines.push_back(bfsFrom(chosen.first, chosen.second));
                    placedOne = true;
                }
            }

            if (!placedOne) break;   // 这一档放不满，整体回退重试
        }

        if ((int)placedMines.size() == count) break;   // 已经放满，保持最严格的起步距离
    }
}

bool Maze::IsMine(int gx, int gy) const
{
    if (!grid.isInBounds(gx, gy)) return false;
    return grid.at(gx, gy) == CellType::MINE;
}

bool Maze::IsMineAt(Vector3 worldPos) const
{
    int gx = 0;
    int gy = 0;
    WorldToGrid(worldPos, gx, gy);
    return IsMine(gx, gy);
}
bool Maze::canWalk(int gx, int gy) const
{
    if (!grid.isInBounds(gx, gy)) return false;
    const CellType t = grid.at(gx, gy);
    // 神圣模式的玻璃墙 / 可破墙同样挡路：玻璃墙看不见，可破墙要打一枪
    return t != CellType::WALL && t != CellType::GLASS_WALL && t != CellType::BREAK_WALL;
}
// ============================================================
// 神圣模式：玻璃墙（看不见，挡路打不碎）+ 可破墙（堵路口，一发子弹打碎）
// ============================================================
void Maze::SpawnHolyWalls(int glassMin, int glassMax, int breakCount)
{
    const int w = grid.width;
    const int h = grid.height;
    if (w < 5 || h < 5) return;

    const int dirs[4][2] = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
    const auto index = [w](int x, int y) { return (size_t)y * w + x; };
    const auto open = [&](int x, int y) {
        return grid.isInBounds(x, y) && grid.at(x, y) != CellType::WALL;
    };

    // ---- 入口 -> 出口 的主路（完美迷宫只有一条，BFS 就是从入口的唯一路线）----
    std::vector<int> dist((size_t)w * h, -1);
    std::vector<std::pair<int, int>> path;   // 入口 -> 出口，按步序排列
    if (open(entryGx, entryGy))
    {
        std::queue<std::pair<int, int>> q;
        dist[index(entryGx, entryGy)] = 0;
        q.push({ entryGx, entryGy });
        while (!q.empty())
        {
            const std::pair<int, int> c = q.front();
            q.pop();
            for (const auto& dir : dirs)
            {
                const int nx = c.first + dir[0];
                const int ny = c.second + dir[1];
                if (!open(nx, ny)) continue;
                if (dist[index(nx, ny)] >= 0) continue;
                dist[index(nx, ny)] = dist[index(c.first, c.second)] + 1;
                q.push({ nx, ny });
            }
        }

        // 从出口沿距离递减回溯，得到入口 -> 出口的那一条路
        if (dist[index(exitGx, exitGy)] >= 0)
        {
            int cx = exitGx;
            int cy = exitGy;
            while (!(cx == entryGx && cy == entryGy))
            {
                path.push_back({ cx, cy });
                const int here = dist[index(cx, cy)];
                bool moved = false;
                for (const auto& dir : dirs)
                {
                    const int nx = cx + dir[0];
                    const int ny = cy + dir[1];
                    if (!open(nx, ny)) continue;
                    if (dist[index(nx, ny)] != here - 1) continue;
                    cx = nx;
                    cy = ny;
                    moved = true;
                    break;
                }
                if (!moved) break;
            }
            path.push_back({ entryGx, entryGy });
            std::reverse(path.begin(), path.end());
        }
    }

    // ---- 玻璃墙：把迷宫原本的墙段换成玻璃（外观透明，照样挡路）----
    // 只挑贴着通路的那一圈墙：玩家沿路走就能看见它；最外圈外墙不动。
    // 注意：玻璃墙占的是墙格，所以地图结构不变，出口该通还是通。
    const int minGlass = (glassMin > 0) ? glassMin : 0;
    const int maxGlass = (glassMax >= minGlass) ? glassMax : minGlass;
    if (maxGlass > 0)
    {
        std::vector<std::pair<int, int>> cand;
        for (int y = 1; y < h - 1; ++y)
            for (int x = 1; x < w - 1; ++x)
            {
                if (grid.at(x, y) != CellType::WALL) continue;
                bool facePath = false;                 // 至少一面贴着能走的格子
                for (const auto& dir : dirs)
                {
                    const int nx = x + dir[0];
                    const int ny = y + dir[1];
                    if (!grid.isInBounds(nx, ny)) continue;
                    if (grid.at(nx, ny) != CellType::WALL) facePath = true;
                }
                if (facePath) cand.emplace_back(x, y);
            }

        const int want = minGlass + (int)(std::rand() % (maxGlass - minGlass + 1));
        for (int i = 0; i < want && !cand.empty(); ++i)
        {
            const size_t pick = (size_t)std::rand() % cand.size();
            grid.at(cand[pick].first, cand[pick].second) = CellType::GLASS_WALL;
            cand[pick] = cand.back();
            cand.pop_back();
        }
    }

    // ---- 可破墙：堵在主路上（走廊格 = 恰好两个通路邻居），必须打一枪才能过 ----
    if (breakCount <= 0 || path.size() < 8) return;

    std::vector<int> stepCand;                     // 主路下标
    const int firstStep = 3;                       // 离入口留几步，别一出生就贴脸
    const int lastStep = (int)path.size() - 3;     // 出口前也留几步
    for (int i = firstStep; i < lastStep; ++i)
    {
        const int x = path[i].first;
        const int y = path[i].second;
        int openNeighbors = 0;
        for (const auto& dir : dirs)
            if (open(x + dir[0], y + dir[1])) ++openNeighbors;
        if (openNeighbors == 2) stepCand.push_back(i);   // 走廊格：堵住才真的挡路
    }
    if (stepCand.empty()) return;

    std::vector<int> chosen;
    for (int attempt = 0; attempt < 200 && (int)chosen.size() < breakCount; ++attempt)
    {
        const int step = stepCand[(size_t)std::rand() % stepCand.size()];
        bool tooClose = false;
        for (int c : chosen)
            if (std::abs(c - step) < 4) tooClose = true;   // 两堵墙别挨在一起
        if (tooClose) continue;

        const int x = path[step].first;
        const int y = path[step].second;
        if (grid.at(x, y) != CellType::PATH) continue;
        grid.at(x, y) = CellType::BREAK_WALL;
        chosen.push_back(step);
    }
}

bool Maze::IsGlassWall(int gx, int gy) const
{
    if (!grid.isInBounds(gx, gy)) return false;
    return grid.at(gx, gy) == CellType::GLASS_WALL;
}

bool Maze::IsBreakWall(int gx, int gy) const
{
    if (!grid.isInBounds(gx, gy)) return false;
    return grid.at(gx, gy) == CellType::BREAK_WALL;
}

// 打碎可破墙：那一格变回通路，玩家 / 敌人 / 光球都能过了
bool Maze::BreakWall(int gx, int gy)
{
    if (!IsBreakWall(gx, gy)) return false;
    grid.at(gx, gy) = CellType::PATH;
    return true;
}

bool Maze::IsWorldPosBlocked(Vector3 worldPos) const
{
    int gx, gy;
    WorldToGrid(worldPos, gx, gy);
    return !canWalk(gx, gy);
}

void Maze::WorldToGrid(Vector3 worldPos, int& gx, int& gy) const
{
    // 用 floorf 而不是 static_cast<int>，保证负坐标也正确
    gx = static_cast<int>(std::floor(worldPos.x / CELL_SIZE));
    gy = static_cast<int>(std::floor(worldPos.z / CELL_SIZE));
}

// 返回格子中心的世界坐标（x/z 为中心，y=0 为地面）
Vector3 Maze::GridToWorld(int gx, int gy) const
{
    return {
        (gx + 0.5f) * CELL_SIZE,   // 格子中心的 x
        0.0f,                       // 地面 y
        (gy + 0.5f) * CELL_SIZE    // 格子中心的 z
    };
}

std::vector<std::pair<int, int>> Maze::GetAllWallGrid() const
{
    std::vector<std::pair<int, int>> walls;
    for (int y = 0;y < grid.height;y++)
    {
        for (int x = 0;x < grid.width;x++)
        {
            if (grid.at(x, y) == CellType::WALL)
            {
                walls.emplace_back(x, y);
            }
        }
    }
    return walls;
}

void Maze::printConsole() const
{
    for (int y = 0;y < grid.height;y++)
    {
        for (int x = 0;x < grid.width;x++)
        {
            const CellType t = grid.at(x, y);
            if (t == CellType::WALL)            std::cout << "#";
            else if (t == CellType::GLASS_WALL) std::cout << "G";   // 神圣：玻璃墙
            else if (t == CellType::BREAK_WALL) std::cout << "B";   // 神圣：可破墙
            else                                std::cout << " ";
        }
        std::cout << "\n";
    }
    std::cout.flush();   // 立刻刷出来：VS 输出窗口 / 控制台能实时看到（别被缓冲吞掉）
}

// ============================================================
// 存档：把迷宫存成 JSON
// ============================================================
bool Maze::SaveToFile(const char* path) const
{
    json j;
    j["width"] = grid.width;
    j["height"] = grid.height;
    j["entryGx"] = entryGx;
    j["entryGy"] = entryGy;
    j["exitGx"] = exitGx;
    j["exitGy"] = exitGy;
    j["treasureCount"] = treasureCount;

    // 把格子压成字符串数组，每行一个字符串
    auto cellToChar = [](CellType t) -> char {
        switch (t)
        {
        case CellType::WALL:            return 'W';
        case CellType::PATH:            return '.';
        case CellType::TREASURE_SCORE:  return 'S';
        case CellType::TREASURE_ATTACK: return 'A';
        case CellType::TREASURE_HEAL:   return 'H';
        case CellType::SLOW_ZONE:       return 'Z';
        case CellType::MINE:            return 'M';
        case CellType::GLASS_WALL:      return 'G';
        case CellType::BREAK_WALL:      return 'B';
        case CellType::EXIT:            return 'E';
        default:                        return '?';
        }
        };

    std::vector<std::string> rows;
    for (int y = 0; y < grid.height; ++y)
    {
        std::string row;
        row.reserve(grid.width);
        for (int x = 0; x < grid.width; ++x)
            row.push_back(cellToChar(grid.at(x, y)));
        rows.push_back(std::move(row));
    }
    j["cells"] = rows;

    // 写文件
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << j.dump(2);   // indent = 2，方便人看
    return out.good();
}

// ============================================================
// 读档：从 JSON 恢复迷宫
// ============================================================
bool Maze::LoadFromFile(const char* path)
{
    std::ifstream in(path);
    if (!in.is_open()) return false;

    json j;
    try
    {
        in >> j;
    }
    catch (const std::exception&)
    {
        return false;   // JSON 格式错
    }

    // 读元数据
    const int w = j.value("width", 0);
    const int h = j.value("height", 0);
    if (w <= 0 || h <= 0 || w > 200 || h > 200) return false;

    entryGx = j.value("entryGx", 1);
    entryGy = j.value("entryGy", 1);
    exitGx = j.value("exitGx", w - 2);
    exitGy = j.value("exitGy", h - 2);
    treasureCount = j.value("treasureCount", 0);

    // 重新分配 grid
    grid = Grid<CellType>(w, h);

    // 读格子
    auto charToCell = [](char c) -> CellType {
        switch (c)
        {
        case 'W': return CellType::WALL;
        case '.': return CellType::PATH;
        case 'S': return CellType::TREASURE_SCORE;
        case 'A': return CellType::TREASURE_ATTACK;
        case 'H': return CellType::TREASURE_HEAL;
        case 'Z': return CellType::SLOW_ZONE;
        case 'M': return CellType::MINE;
        case 'G': return CellType::GLASS_WALL;
        case 'B': return CellType::BREAK_WALL;
        case 'E': return CellType::EXIT;
        default:  return CellType::PATH;
        }
        };

    if (!j.contains("cells") || !j["cells"].is_array()) return false;
    const auto& rows = j["cells"];
    if ((int)rows.size() != h) return false;

    for (int y = 0; y < h; ++y)
    {
        const std::string row = rows[y].get<std::string>();
        for (int x = 0; x < w; ++x)
        {
            char c = (x < (int)row.size()) ? row[x] : '.';
            grid.at(x, y) = charToCell(c);
        }
    }

    return true;
}
