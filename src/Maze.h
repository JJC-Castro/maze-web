#pragma once
#include <vector>
#include <utility>
#include "raylib.h"

// 全局格子大小，和队友统一
constexpr float CELL_SIZE = 2.0f;
constexpr float WALL_HEIGHT = 7.0f;   // 墙高：相机最高点(约6.4m)也在墙顶之下，看不到墙外地形

enum class CellType
{
    WALL,         // 墙体，不可通行
    PATH,         // 通路，可以行走
    //宝物
    TREASURE_SCORE,    // 加分（金）
    TREASURE_ATTACK,   // 加攻击（蓝）
    TREASURE_HEAL,     // 回血（红）

    // 减速带：可行走的红色地面，玩家和敌人经过时都会降速
    SLOW_ZONE,

    // 地雷：贴地的红黄棋盘格扁圆盘，玩家踩到扣血（敌人不受影响）
    MINE,

    // 神圣模式：玻璃墙（看上去什么都没有，实则挡路，打不碎）
    GLASS_WALL,
    // 神圣模式：可破墙（颜色比普通墙更深，堵在路口，一发子弹打碎）
    BREAK_WALL,

    EXIT          // 出口
};

template<typename T>
class Grid
{
public:
    int width;   // 网格X方向总格子数
    int height;  // 网格Y方向总格子数
    std::vector<T> data;  // 一维数组存储二维网格（y*width + x）

    Grid(int w, int h): width(w), height(h), data(w* h){ }

    T& at(int x, int y)
    {
        return data[y * width + x];
    }
    const T& at(int x, int y) const
    {
        return data[y * width + x];
    }

    bool isInBounds(int x, int y) const
    {
        return x >= 0 && x < width && y >= 0 && y < height;
    }
};

class Maze
{
public:
    Grid<CellType> grid;

    // 入口 / 出口
    int entryGx = 1, entryGy = 1;
    int exitGx = 0, exitGy = 0;
    int treasureCount = 0;// 统计当前宝物数量，方便判断是否全吃完

    // ---- 减速带（红色地面区）参数：开一局前按难度写入，生成迷宫时使用 ----
    int   slowZoneSegments = 1;    // 减速带条数
    int   slowZoneMinCells = 2;    // 每条最少格数
    int   slowZoneMaxCells = 4;    // 每条最多格数
    float slowZoneFactor = 0.5f;   // 减速带上速度倍率（越小越慢）
    int   mineCount = 0;           // 地雷数量（按难度写入，生成迷宫时使用）

    Maze(int sx, int sy);
    void generateRecursiveBacktrack();

    void SpawnTreasures(int scoreCount, int attackCount, int healCount);

    // 在通路上铺若干段减速带（生成迷宫时调用，需先写好上面的参数）
    void SpawnSlowZones();

    bool  IsSlowZone(int gx, int gy) const;      // 该格是不是减速带
    bool  IsSlowZoneAt(Vector3 worldPos) const;  // 世界坐标处是不是减速带
    float SlowFactorAt(Vector3 worldPos) const;  // 速度倍率：减速带返回 slowZoneFactor，否则 1

    // 在通路上布地雷，优先放在入口->出口的最短通路上（生成迷宫时调用）
    void SpawnMines(int count);

    bool  IsMine(int gx, int gy) const;          // 该格是不是地雷
    bool  IsMineAt(Vector3 worldPos) const;      // 世界坐标处是不是地雷

    // ---- 神圣模式：玻璃墙 / 可破墙（生成迷宫时调用）----
    // 在通路上撒 glassMin~glassMax 面玻璃墙（看不见但挡路），再把 breakCount
    // 面可破墙堵在入口 -> 出口的主路上（必须打一枪才能过）。
    void SpawnHolyWalls(int glassMin, int glassMax, int breakCount);

    bool IsGlassWall(int gx, int gy) const;      // 该格是不是玻璃墙
    bool IsBreakWall(int gx, int gy) const;      // 该格是不是可破墙
    bool BreakWall(int gx, int gy);              // 打碎可破墙（改回通路），成功返回 true

    bool canWalk(int gx, int gy) const;
    bool IsWorldPosBlocked(Vector3 worldPos) const;

    // ===== 坐标转换（中心约定）=====
    void WorldToGrid(Vector3 worldPos, int& gx, int& gy) const;
    Vector3 GridToWorld(int gx, int gy) const;

    std::vector<std::pair<int, int>> GetAllWallGrid() const;
    void printConsole() const;

    // ---- 存档 / 读档 ----
    bool SaveToFile(const char* path) const;
    bool LoadFromFile(const char* path);

private:
    void dfs(int x, int y);
};
