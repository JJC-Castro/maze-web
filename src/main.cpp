using namespace std;
#include "raylib.h"
#include "raymath.h"
#include "Config.h"
#include "Player.h"
#include "Menu.h"
#include "GameTimer.h"
#include "UIFont.h"
#include "Maze.h"
#include "Enemy.h"
#include "Treasure.h"
#include "Save.h"
#include "LightSource.h"
#include "Account.h"
#include "TextInput.h"
#include "FootstepAudio.h"
#include "SfxAudio.h"
#include "Nailong.h"
#include "TPCamera.h"
// WebPlatform 桌面版也有一份空实现（比如 SyncCanvasToWindow），
// 所以无条件包含 —— 主循环里那句调用就不用再套一层 #ifdef。
#include "WebPlatform.h"
#include "Leaderboard.h"    // 全网榜（桌面版是空实现，调它不花钱）

#include "RL/RLNet.h"
#include "RL/RlAgent.h"
#include "RL/RlWalker.h"
#include "RL/RlBridge.h"
#include "RL/RlTelemetry.h"
#include "RL/MazeSolver.h"      // 肉鸽稀有增益「伙伴」用它的 BFS 距离场带路
#include <cstdio>
#include <cstdlib>

#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

// 雷达配置以屏幕像素和世界单位为基准
constexpr float RADAR_RADIUS = 100.0f;
constexpr float RADAR_MARGIN = 20.0f;
constexpr float RADAR_WORLD_RANGE = 18.0f;
constexpr float RADAR_DOT_RADIUS = 4.5f;

//初始血条上限按照难度设定
int playerHealth = 0;

// ---- 奶龙（第三人称主角）----
std::unique_ptr<Nailong> nailong;   // 进入游戏时懒加载：有 glb 用模型，没有就程序化奶龙
bool thirdPerson = true;            // 视角开关：true = 第三人称奶龙，false = 第一人称
GameMode gameMode = GameMode::NAILONG;    // 当前模式（登录 / 跳过后在模式选择页设定）

// ---- 第三人称环绕相机 / 自动索敌 ----
TPCamera tpCam;                            // 奶龙的环绕机位（独立封装，见 TPCamera.h）
float    modelYaw = 0.0f;                  // 角色模型朝向（走跑时对齐移动方向）
bool     aimLocked = false;                // 本帧是否锁到敌人（自动索敌）
Vector3  aimLockPos{ 0.0f, 0.0f, 0.0f };   // 锁定目标的世界坐标
bool cursorLocked = true;           // 鼠标锁定状态（Tab 切换）
int playerMaxHealth = EnemyConfig::PLAYER_MAX_HEALTH;   // ← 新增：当前难度的上限

// 玩家是否站在出口格上
static bool IsAtExit(const Maze& maze, const Player& player)
{
    const Vector3 exitPos = maze.GridToWorld(maze.exitGx, maze.exitGy);
    const Vector3 feet = player.GetFeetPosition();
    const float dx = feet.x - exitPos.x;
    const float dz = feet.z - exitPos.z;
    const float radius = CELL_SIZE * 0.5f;

    return dx * dx + dz * dz < radius * radius;
}

// ---- 伙伴专用：从终点出发的 BFS 距离场 ----
// 为什么不直接用 RL 那套 rl::DistField：那是跨模块共享的引擎结构，
// 一旦它的 CellType 映射 / 缓存出问题会直接把伙伴的行为污染成不可预测的东西。
// 伙伴只需要“往终点走”，所以这里自带一份、每一步都做边界检查的小 BFS，
// 失败就返回 false，伙伴自动退化成“贴在玩家身前 3m”，绝不会崩。
static std::vector<int> g_companionDist;

static int CompanionDistAt(const std::vector<int>& dist, int w, int h, int gx, int gy)
{
    if (gx < 0 || gy < 0 || gx >= w || gy >= h) return -2;   // 越界：不可能等于 d-1（d>=1）
    const size_t idx = ((size_t)gy * (size_t)w) + (size_t)gx;
    if (idx >= dist.size()) return -2;
    return dist[idx];
}

static bool BuildCompanionDist(const Maze& maze, std::vector<int>& dist)
{
    dist.clear();
    const int w = maze.grid.width;
    const int h = maze.grid.height;
    if (w <= 0 || h <= 0) return false;
    if (!maze.grid.isInBounds(maze.exitGx, maze.exitGy)) return false;
    if (!maze.canWalk(maze.exitGx, maze.exitGy)) return false;

    dist.assign((size_t)w * (size_t)h, -1);
    std::vector<int> queue;
    queue.reserve((size_t)w * (size_t)h);
    const int start = (int)(((size_t)maze.exitGy * (size_t)w) + (size_t)maze.exitGx);
    dist[(size_t)start] = 0;
    queue.push_back(start);

    static const int ddx[4] = { 1, -1, 0, 0 };
    static const int ddy[4] = { 0, 0, 1, -1 };
    for (size_t head = 0; head < queue.size(); ++head)
    {
        const int idx = queue[head];
        const int gx = idx % w;
        const int gy = idx / w;
        const int nd = dist[(size_t)idx] + 1;
        for (int k = 0; k < 4; ++k)
        {
            const int nx = gx + ddx[k];
            const int ny = gy + ddy[k];
            if (!maze.grid.isInBounds(nx, ny)) continue;
            const size_t nidx = ((size_t)ny * (size_t)w) + (size_t)nx;
            if (dist[nidx] != -1) continue;
            if (!maze.canWalk(nx, ny)) continue;
            dist[nidx] = nd;
            queue.push_back((int)nidx);
        }
    }
    return true;
}

struct PatrolScanHit
{
    float distance = 0.0f;
    Vector3 point{ 0.0f, 0.0f, 0.0f };
    Vector3 normal{ 0.0f, 0.0f, 0.0f };
    bool blocked = false;
};

// 两点之间有没有墙挡着（只做 2D 网格采样，索敌和弹道都用它）
static bool MazeLineClear(const Maze& maze, Vector3 from, Vector3 to)
{
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float distance = sqrtf(dx * dx + dz * dz);
    if (distance < 0.001f) return true;

    const int steps = (int)(distance / 0.25f) + 1;
    for (int i = 1; i < steps; ++i)
    {
        const float t = (float)i / (float)steps;
        if (maze.IsWorldPosBlocked(Vector3{ from.x + dx * t, from.y, from.z + dz * t }))
        {
            return false;
        }
    }
    return true;
}

// 目标是否在游戏窗口内：既要在相机前方，投影也要落在屏幕范围内
static bool IsVisibleOnScreen(const Camera3D& cam, Vector3 worldPos)
{
    const Vector3 toTarget = Vector3Subtract(worldPos, cam.position);
    const Vector3 forward  = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    if (Vector3DotProduct(toTarget, forward) <= 0.0f) return false;      // 在身后

    const Vector2 screen = GetWorldToScreen(worldPos, cam);
    const float margin = 40.0f;                                          // 允许贴边一点点
    return screen.x >= -margin &&
           screen.x <= (float)GetScreenWidth() + margin &&
           screen.y >= -margin &&
           screen.y <= (float)GetScreenHeight() + margin;
}

// 神圣模式：沿视线找第一堵"可破墙"（中间隔着普通墙 / 玻璃墙就打不到）
// 命中时带出那面墙的格子坐标和距离（米）
static bool FindBreakWallInSight(const Maze& maze, Vector3 origin, Vector3 dir,
                                 float maxDist, int& outGx, int& outGy, float& outDist)
{
    if (Vector3LengthSqr(dir) < 1e-6f) return false;
    const Vector3 d = Vector3Normalize(dir);
    const float step = CELL_SIZE * 0.2f;          // 0.4m 一步，不漏格子
    for (float dist = step; dist <= maxDist; dist += step)
    {
        const Vector3 p = { origin.x + d.x * dist,
                            origin.y + d.y * dist,
                            origin.z + d.z * dist };
        int gx = 0;
        int gy = 0;
        maze.WorldToGrid(p, gx, gy);
        if (!maze.grid.isInBounds(gx, gy)) return false;     // 打出迷宫范围
        const CellType c = maze.grid.at(gx, gy);
        if (c == CellType::BREAK_WALL)
        {
            outGx = gx;
            outGy = gy;
            outDist = dist;
            return true;
        }
        if (c == CellType::WALL || c == CellType::GLASS_WALL) return false;
    }
    return false;
}

// 自动索敌：半径内挑最近、有视线、还活着的敌人；找到就把位置写进 outPos
static bool FindAutoAimTarget(const Maze& maze, Vector3 from,
                              const vector<EnemyRenderState>& states,
                              float range, const Camera3D& cam, Vector3& outPos)
{
    float bestSquared = range * range;
    bool  found = false;

    for (const EnemyRenderState& enemy : states)
    {
        if (!enemy.active || enemy.health <= 0.0f) continue;

        const float dx = enemy.position.x - from.x;
        const float dz = enemy.position.z - from.z;
        const float squared = dx * dx + dz * dz;
        if (squared > bestSquared) continue;                         // 更远的跳过
        if (!MazeLineClear(maze, from, enemy.position)) continue;    // 隔着墙打不到
        // 只在窗口中看得见时才索敌：跑到屏幕外就不再自动锁
        const Vector3 aimPoint = { enemy.position.x,
                                   enemy.position.y + EnemyConfig::ENEMY_HEIGHT * 0.5f,
                                   enemy.position.z };
        if (!IsVisibleOnScreen(cam, aimPoint)) continue;

        bestSquared = squared;
        outPos = enemy.position;
        found = true;
    }
    return found;
}

static PatrolScanHit CastPatrolScanRay(
    const Maze& maze,
    Vector3 origin,
    Vector3 direction,
    float maxDistance)
{
    PatrolScanHit hit;
    hit.distance = maxDistance;
    hit.point = {
        origin.x + direction.x * maxDistance,
        GROUND_Y + EnemyConfig::PATROL_SCAN_FLOOR_LIFT,
        origin.z + direction.z * maxDistance
    };

    const float step = max(0.01f, EnemyConfig::PATROL_SCAN_STEP);
    float previousDistance = 0.0f;

    for (float distance = step; distance <= maxDistance; distance += step)
    {
        const Vector3 sample = {
            origin.x + direction.x * distance,
            0.0f,
            origin.z + direction.z * distance
        };

        if (!maze.IsWorldPosBlocked(sample))
        {
            previousDistance = distance;
            continue;
        }

        float low = previousDistance;
        float high = distance;
        for (int refine = 0; refine < 6; ++refine)
        {
            const float middle = (low + high) * 0.5f;
            const Vector3 middleSample = {
                origin.x + direction.x * middle,
                0.0f,
                origin.z + direction.z * middle
            };

            if (maze.IsWorldPosBlocked(middleSample))
            {
                high = middle;
            }
            else
            {
                low = middle;
            }
        }

        int lowGridX = 0;
        int lowGridY = 0;
        int highGridX = 0;
        int highGridY = 0;
        maze.WorldToGrid(
            { origin.x + direction.x * low, 0.0f, origin.z + direction.z * low },
            lowGridX,
            lowGridY
        );
        maze.WorldToGrid(
            { origin.x + direction.x * high, 0.0f, origin.z + direction.z * high },
            highGridX,
            highGridY
        );

        if (lowGridX != highGridX)
        {
            hit.normal.x = highGridX > lowGridX ? -1.0f : 1.0f;
        }
        else if (lowGridY != highGridY)
        {
            hit.normal.z = highGridY > lowGridY ? -1.0f : 1.0f;
        }

        hit.blocked = true;
        hit.distance = low;
        hit.point = {
            origin.x + direction.x * low + hit.normal.x * 0.01f,
            GROUND_Y + EnemyConfig::PATROL_SCAN_FLOOR_LIFT,
            origin.z + direction.z * low + hit.normal.z * 0.01f
        };
        return hit;
    }

    return hit;
}

static void DrawPatrolScanArea(
    const Maze& maze,
    Vector3 origin,
    float radius,
    float alpha)
{
    const int rayCount = max(8, EnemyConfig::PATROL_SCAN_RAY_COUNT);
    Vector3 previousPoint{ 0.0f, 0.0f, 0.0f };
    bool hasPrevious = false;

    for (int ray = 0; ray <= rayCount; ++ray)
    {
        const float angle =
            2.0f * PI * static_cast<float>(ray) /
            static_cast<float>(rayCount);
        const Vector3 direction = { sinf(angle), 0.0f, cosf(angle) };
        const PatrolScanHit hit =
            CastPatrolScanRay(maze, origin, direction, radius);
        const float visibleDistance = min(radius, hit.distance);

        Vector3 point = {
            origin.x + direction.x * visibleDistance,
            GROUND_Y + EnemyConfig::PATROL_SCAN_FLOOR_LIFT,
            origin.z + direction.z * visibleDistance
        };
        if (hit.blocked && radius >= hit.distance)
        {
            point = hit.point;
        }

        if (hasPrevious)
        {
            if (Vector3Distance(previousPoint, point) > 0.001f)
            {
                DrawTriangle3D(
                    origin,
                    previousPoint,
                    point,
                    Fade(RED, alpha)
                );
            }
        }

        if (hit.blocked && radius >= hit.distance)
        {
            const float travelPastWall = max(0.0f, radius - hit.distance);
            const float projection = min(
                1.0f,
                travelPastWall / max(0.01f, EnemyConfig::PATROL_SCAN_STEP)
            );
            const float wallHeight =
                EnemyConfig::PATROL_SCAN_WALL_HEIGHT *
                (0.3f + 0.7f * projection);

            const Color wallColor = Fade(
                RED,
                min(
                    EnemyConfig::PATROL_SCAN_MAX_ALPHA,
                    max(alpha, EnemyConfig::PATROL_SCAN_MIN_ALPHA)
                )
            );
            DrawCylinderEx(
                point,
                { point.x, point.y + wallHeight, point.z },
                EnemyConfig::PATROL_SCAN_WALL_WIDTH,
                EnemyConfig::PATROL_SCAN_WALL_WIDTH,
                6,
                wallColor
            );
        }

        previousPoint = point;
        hasPrevious = true;
    }
}

static void DrawPatrolScan(
    const Maze& maze,
    const EnemyRenderState& enemy)
{
    if (!enemy.active || enemy.type != EnemyType::Patrol)
    {
        return;
    }

    const Vector3 origin = {
        enemy.position.x,
        GROUND_Y + EnemyConfig::PATROL_SCAN_FLOOR_LIFT,
        enemy.position.z
    };

    DrawPatrolScanArea(
        maze,
        origin,
        EnemyConfig::PATROL_VISION_RANGE,
        EnemyConfig::PATROL_SCAN_FILL_ALPHA * 0.25f
    );

    const int ringCount = max(1, EnemyConfig::PATROL_SCAN_RING_COUNT);
    for (int ring = 0; ring < ringCount; ++ring)
    {
        const float phase = fmodf(
            static_cast<float>(GetTime()) * EnemyConfig::PATROL_SCAN_SPEED +
                static_cast<float>(ring) / static_cast<float>(ringCount),
            1.0f
        );
        const float radius = phase * EnemyConfig::PATROL_VISION_RANGE;
        const float alpha =
            (1.0f - phase) * EnemyConfig::PATROL_SCAN_FILL_ALPHA;

        DrawPatrolScanArea(maze, origin, radius, alpha);
    }
}

// ============================================================
// 3D 场景：地面 + 迷宫墙体 + 宝物 + 入口/出口 + 敌人
// ============================================================
static void DrawScene3D(Camera3D cam, Vector3 groundPos, Vector3 groundSize,
                        const Maze& maze, const EnemyManager& enemyManager,
                        const vector<EnemyRenderState>& enemyStates,
                        Vector3 breakFlashPos, float breakFlashTimer)
{
    BeginMode3D(cam);

    DrawCube(groundPos, groundSize.x, groundSize.y, groundSize.z, DARKGRAY);
    DrawCubeWires(groundPos, groundSize.x, groundSize.y, groundSize.z, BLACK);

    // 迷宫墙体
    for (const auto& cell : maze.GetAllWallGrid())
    {
        const Vector3 cubePos = maze.GridToWorld(cell.first, cell.second);
        const Vector3 wallCenter = { cubePos.x, WALL_HEIGHT / 2.0f, cubePos.z };
        DrawCube(wallCenter, CELL_SIZE, WALL_HEIGHT, CELL_SIZE, GRAY);
        DrawCubeWires(wallCenter, CELL_SIZE, WALL_HEIGHT, CELL_SIZE, BLACK);
    }

    // 神圣模式：玻璃墙（半透明玻璃：看得见、挡得住、打不碎）
    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
        {
            if (maze.grid.at(x, y) != CellType::GLASS_WALL) continue;
            const Vector3 glassPos = maze.GridToWorld(x, y);
            const Vector3 glassCenter = { glassPos.x, WALL_HEIGHT / 2.0f, glassPos.z };
            DrawCube(glassCenter, CELL_SIZE, WALL_HEIGHT, CELL_SIZE, HOLY_GLASS_WALL_COLOR);
            DrawCubeWires(glassCenter, CELL_SIZE, WALL_HEIGHT, CELL_SIZE,
                          HOLY_GLASS_WALL_EDGE_COLOR);
        }

    // 神圣模式：可破墙（比普通墙深，位置就在格子里）
    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
        {
            if (maze.grid.at(x, y) != CellType::BREAK_WALL) continue;
            const Vector3 cubePos = maze.GridToWorld(x, y);
            const Vector3 wallCenter = { cubePos.x, WALL_HEIGHT / 2.0f, cubePos.z };
            DrawCube(wallCenter, CELL_SIZE, WALL_HEIGHT, CELL_SIZE, HOLY_BREAK_WALL_COLOR);
            DrawCubeWires(wallCenter, CELL_SIZE, WALL_HEIGHT, CELL_SIZE,
                          Fade(BLACK, 0.9f));
        }

    // 减速带：贴地的红色薄板，顶面略高于地面，既不相嵌也不悬空
    const float slowZoneCenterY =
        GROUND_Y + SLOW_ZONE_TOP_LIFT - SLOW_ZONE_THICKNESS * 0.5f;
    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
        {
            if (maze.grid.at(x, y) != CellType::SLOW_ZONE) continue;
            const Vector3 c = maze.GridToWorld(x, y);
            DrawCube(Vector3{ c.x, slowZoneCenterY, c.z },
                     CELL_SIZE, SLOW_ZONE_THICKNESS, CELL_SIZE, SLOW_ZONE_COLOR);
            DrawCubeWires(Vector3{ c.x, slowZoneCenterY, c.z },
                     CELL_SIZE, SLOW_ZONE_THICKNESS, CELL_SIZE, Fade(BLACK, 0.55f));
        }

    // 地雷：贴地的红黄棋盘格扁圆盘（底面和顶面都以 GROUND_Y 对齐，不嵌地也不悬空）
    const float mineTopY = GROUND_Y + MINE_HEIGHT;
    const int mineTiles = 4;                                  // 顶面 4x4 小格
    const float mineTileSize = (MINE_RADIUS * 2.0f) / (float)mineTiles;
    const float mineTileH = 0.01f;
    const float mineTileLimit = MINE_RADIUS - mineTileSize * 0.35f;
    const float mineTileLimitSq = mineTileLimit * mineTileLimit;
    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
        {
            if (maze.grid.at(x, y) != CellType::MINE) continue;
            const Vector3 c = maze.GridToWorld(x, y);

            // 圆柱侧壁 / 底座
            DrawCylinderEx(Vector3{ c.x, GROUND_Y, c.z },
                           Vector3{ c.x, mineTopY, c.z },
                           MINE_RADIUS, MINE_RADIUS, 24, MINE_COLOR_BODY);

            // 顶面红黄棋盘格：每格一块薄片，贴在圆柱顶面上
            for (int ty = 0; ty < mineTiles; ++ty)
                for (int tx = 0; tx < mineTiles; ++tx)
                {
                    const float ox = (tx + 0.5f) * mineTileSize - MINE_RADIUS;
                    const float oz = (ty + 0.5f) * mineTileSize - MINE_RADIUS;
                    if (ox * ox + oz * oz > mineTileLimitSq) continue;
                    const Color tileColor =
                        ((tx + ty) % 2 == 0) ? MINE_COLOR_RED : MINE_COLOR_YELLOW;
                    DrawCube(
                        Vector3{ c.x + ox, mineTopY + mineTileH * 0.5f, c.z + oz },
                        mineTileSize, mineTileH, mineTileSize, tileColor);
                }
        }

    Treasure::Draw(maze);

    // 入口（蓝）/ 出口（绿）地面标记
    const Vector3 entry = maze.GridToWorld(maze.entryGx, maze.entryGy);
    DrawCube(Vector3{ entry.x, 0.05f, entry.z },
             CELL_SIZE * 0.9f, 0.1f, CELL_SIZE * 0.9f, BLUE);

    const Vector3 exit = maze.GridToWorld(maze.exitGx, maze.exitGy);
    DrawCube(Vector3{ exit.x, 0.05f, exit.z },
             CELL_SIZE * 0.9f, 0.1f, CELL_SIZE * 0.9f, LIME);

    // 敌人
    for (const EnemyRenderState& enemy : enemyStates)
    {
        DrawPatrolScan(maze, enemy);

        const float height = enemy.bounds.max.y - enemy.bounds.min.y;
        const Vector3 center = { enemy.position.x,
                                 enemy.bounds.min.y + height / 2.0f,
                                 enemy.position.z };
        const float width = EnemyConfig::ENEMY_HALF_WIDTH * 2.0f;
        const Color color = (enemy.type == EnemyType::Patrol) ? RED : BLUE;

        DrawCube(center, width, height, width, color);
        DrawCubeWires(center, width, height, width, BLACK);


    }

    for (const OrbRenderState& orb : enemyManager.GetOrbRenderStates())
    {
        DrawSphere(orb.position, orb.radius * 1.4f, Fade(GOLD, 0.35f));
        DrawSphere(orb.position, orb.radius, GOLD);
        DrawSphereWires(orb.position, orb.radius, 8, 8, ORANGE);
    }

    // 神圣模式：可破墙被打碎的亮光，闪一下就消失（提示"打中了"）
    if (breakFlashTimer > 0.0f)
    {
        const float k = breakFlashTimer / HOLY_BREAK_FLASH_SECONDS;   // 1 -> 0
        const float size = CELL_SIZE * (1.0f + 0.6f * (1.0f - k));
        const float sizeY = WALL_HEIGHT * k;
        DrawCube(breakFlashPos, size, sizeY, size,
                 Fade(Color{ 255, 236, 170, 255 }, 0.75f * k));
        DrawCubeWires(breakFlashPos, size, sizeY, size, Fade(WHITE, 0.9f * k));
    }

    EndMode3D();
}

static bool HasLineOfSightToPoint(
    const Maze& maze,
    Vector3 from,
    Vector3 to)
{
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float distance = sqrt(dx * dx + dz * dz);
    const int steps = max(
        1,
        static_cast<int>(ceil(distance / 0.2f))
    );

    for (int step = 1; step < steps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const Vector3 sample = {
            from.x + dx * t,
            0.0f,
            from.z + dz * t
        };

        if (maze.IsWorldPosBlocked(sample))
        {
            return false;
        }
    }

    return true;
}

static void DrawEnemyHealthBars2D(
    Camera3D cam,
    const Maze& maze,
    const vector<EnemyRenderState>& enemyStates)
{
    const float barWidth = 56.0f;
    const float barHeight = 7.0f;

    const Vector3 camForward = Vector3Normalize(
        Vector3Subtract(cam.target, cam.position)
    );

    for (const EnemyRenderState& enemy : enemyStates)
    {
        if (!enemy.active) continue;

        const float healthRatio = max(
            0.0f,
            min(1.0f, enemy.health / EnemyConfig::ENEMY_MAX_HEALTH)
        );

        const Vector3 anchor = {
            enemy.position.x,
            enemy.bounds.max.y + 0.28f,
            enemy.position.z
        };
        if (!HasLineOfSightToPoint(maze, cam.position, anchor))
        {
            continue;
        }

        const Vector3 toBar = Vector3Subtract(anchor, cam.position);
        if (Vector3DotProduct(toBar, camForward) <= 0.0f) continue;

        const Vector2 screen = GetWorldToScreen(anchor, cam);
        if (screen.x < -barWidth || screen.x > GetScreenWidth() + barWidth ||
            screen.y < -barHeight || screen.y > GetScreenHeight() + barHeight)
        {
            continue;
        }

        const float left = screen.x - barWidth * 0.5f;
        const float top = screen.y - barHeight * 0.5f;
        DrawRectangle(
            (int)left,
            (int)top,
            (int)barWidth,
            (int)barHeight,
            Fade(BLACK, 0.75f)
        );
        if (healthRatio > 0.0f)
        {
            DrawRectangle(
                (int)left,
                (int)top,
                (int)(barWidth * healthRatio),
                (int)barHeight,
                LIME
            );
        }
        DrawRectangleLines(
            (int)left,
            (int)top,
            (int)barWidth,
            (int)barHeight,
            Fade(RAYWHITE, 0.9f)
        );
    }
}

static void DrawRadarScanCircle(
    Vector2 radarCenter,
    Vector2 scanCenter,
    float scanRadius,
    float thickness,
    Color color)
{
    const int segments = max(16, EnemyConfig::PATROL_RADAR_SCAN_SEGMENTS);
    const float clipRadius = RADAR_RADIUS - 2.0f;
    Vector2 previousPoint{ 0.0f, 0.0f };
    bool wasInside = false;

    for (int segment = 0; segment <= segments; ++segment)
    {
        const float angle =
            2.0f * PI * static_cast<float>(segment) /
            static_cast<float>(segments);
        const Vector2 point = {
            scanCenter.x + cosf(angle) * scanRadius,
            scanCenter.y + sinf(angle) * scanRadius
        };
        const float dx = point.x - radarCenter.x;
        const float dy = point.y - radarCenter.y;
        const bool isInside = dx * dx + dy * dy <= clipRadius * clipRadius;

        if (wasInside && isInside)
        {
            DrawLineEx(previousPoint, point, thickness, color);
        }

        previousPoint = point;
        wasInside = isInside;
    }
}

static void DrawEnemyRadar(
    const Player& player,
    const vector<EnemyRenderState>& enemyStates,
    Vector3 exitWorld,
    bool showExit)
{
    const Vector2 center = {
        (float)GetScreenWidth() - RADAR_RADIUS - RADAR_MARGIN,
        RADAR_RADIUS + RADAR_MARGIN
    };
    const float radarScale = RADAR_RADIUS / RADAR_WORLD_RANGE;

    DrawCircleV(center, RADAR_RADIUS, Fade(BLACK, 0.58f));
    DrawCircleLines(
        (int)center.x,
        (int)center.y,
        RADAR_RADIUS,
        Fade(RAYWHITE, 0.9f)
    );
    DrawCircleLines(
        (int)center.x,
        (int)center.y,
        RADAR_RADIUS * 0.5f,
        Fade(RAYWHITE, 0.18f)
    );
    DrawLine(
        (int)(center.x - RADAR_RADIUS),
        (int)center.y,
        (int)(center.x + RADAR_RADIUS),
        (int)center.y,
        Fade(RAYWHITE, 0.18f)
    );
    DrawLine(
        (int)center.x,
        (int)(center.y - RADAR_RADIUS),
        (int)center.x,
        (int)(center.y + RADAR_RADIUS),
        Fade(RAYWHITE, 0.18f)
    );

    Vector3 forward = player.GetForward();
    forward.y = 0.0f;
    if (Vector3Length(forward) < 0.001f) forward = Vector3{ 0.0f, 0.0f, 1.0f };
    forward = Vector3Normalize(forward);
    const Vector3 right = player.GetRight();
    const Vector3 playerFeet = player.GetFeetPosition();

    for (const EnemyRenderState& enemy : enemyStates)
    {
        if (!enemy.active) continue;

        const Vector3 delta = {
            enemy.position.x - playerFeet.x,
            0.0f,
            enemy.position.z - playerFeet.z
        };
        const float distanceSquared = delta.x * delta.x + delta.z * delta.z;
        if (distanceSquared > RADAR_WORLD_RANGE * RADAR_WORLD_RANGE) continue;

        const float forwardDistance = Vector3DotProduct(delta, forward);
        const float rightDistance = Vector3DotProduct(delta, right);
        const Vector2 dot = {
            center.x + rightDistance * radarScale,
            center.y - forwardDistance * radarScale
        };
        const Color dotColor =
            (enemy.type == EnemyType::Patrol) ? RED : BLUE;

        if (enemy.type == EnemyType::Patrol)
        {
            const float visionRadius =
                EnemyConfig::PATROL_VISION_RANGE * radarScale;
            const float maxAlpha = EnemyConfig::PATROL_RADAR_SCAN_ALPHA;

            DrawRadarScanCircle(
                center,
                dot,
                visionRadius,
                EnemyConfig::PATROL_RADAR_SCAN_THICKNESS,
                Fade(RED, maxAlpha * 0.3f)
            );

            const int ringCount =
                max(1, EnemyConfig::PATROL_SCAN_RING_COUNT);
            for (int ring = 0; ring < ringCount; ++ring)
            {
                const float phase = fmodf(
                    static_cast<float>(GetTime()) *
                        EnemyConfig::PATROL_SCAN_SPEED +
                        static_cast<float>(ring) /
                            static_cast<float>(ringCount),
                    1.0f
                );
                const float radius = phase * visionRadius;
                const float alpha = (1.0f - phase) * maxAlpha;

                DrawRadarScanCircle(
                    center,
                    dot,
                    radius,
                    EnemyConfig::PATROL_RADAR_SCAN_THICKNESS,
                    Fade(RED, alpha)
                );
            }
        }

        DrawCircleV(dot, RADAR_DOT_RADIUS, dotColor);
        DrawCircleLines(
            (int)dot.x,
            (int)dot.y,
            RADAR_DOT_RADIUS + 1.0f,
            Fade(BLACK, 0.8f)
        );
    }

    // 肉鸽增益「雷达」：终点方向标成绿点（超出雷达范围就贴着圆边）
    if (showExit)
    {
        const float ex = exitWorld.x - playerFeet.x;
        const float ez = exitWorld.z - playerFeet.z;
        Vector2 exitDot = {
            center.x + (right.x * ex + right.z * ez) * radarScale,
            center.y - (forward.x * ex + forward.z * ez) * radarScale
        };
        const float ox = exitDot.x - center.x;
        const float oy = exitDot.y - center.y;
        const float len = sqrtf(ox * ox + oy * oy);
        if (len > RADAR_RADIUS - 6.0f)
        {
            const float k = (RADAR_RADIUS - 6.0f) / ((len > 0.001f) ? len : 1.0f);
            exitDot = Vector2{ center.x + ox * k, center.y + oy * k };
        }
        DrawCircleV(exitDot, RADAR_DOT_RADIUS + 1.0f, ROGUE_EXIT_MARK_COLOR);
        DrawCircleLines((int)exitDot.x, (int)exitDot.y, RADAR_DOT_RADIUS + 4.0f,
            ROGUE_EXIT_MARK_COLOR);
    }

    DrawCircleV(center, 4.0f, RAYWHITE);
    DrawCircleLines((int)center.x, (int)center.y, 5.0f, BLACK);
}

// 数迷宫里还剩多少金色宝物
int CountScoreTreasures(const Maze& maze)
{
    int n = 0;
    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
            if (maze.grid.at(x, y) == CellType::TREASURE_SCORE)
                ++n;
    return n;
}

// 存档到指定槽位
static bool DoSave(const std::unique_ptr<Maze>& maze,
    const std::unique_ptr<Player>& player,
    int playerHealth, int playerMaxHealth,
    int playerAttacksRemaining, int score, double elapsed,
    int slot)
{
    if (!maze || !player) return false;

    GameSave::Data data;
    data.playerHealth = playerHealth;
    data.playerMaxHealth = playerMaxHealth;
    data.playerAttacksRemaining = playerAttacksRemaining;
    data.score = score;
    data.elapsed = elapsed;

    char path[64];
#ifdef MAZE_WEB
    // Web：存档必须写进 IndexedDB 挂载点（/maze），写相对路径刷新页面就没了。
    // 目录由 WebPlatform::InitFileSystem() 建好。
    snprintf(path, sizeof(path), "/maze/saves/slot%d.json", slot);
#else
    snprintf(path, sizeof(path), "saves/slot%d.json", slot);
#endif
    return GameSave::Save(path, *maze, *player, data);
}

// 从指定槽位读档（返回读到的数据，供 main 更新）
static bool DoLoad(std::unique_ptr<Maze>& maze,
    std::unique_ptr<Player>& player,
    std::unique_ptr<EnemyManager>& enemyManager,
    GameSave::Data& outData,
    int slot)
{
    if (!maze)   maze = std::make_unique<Maze>(21, 21);
    if (!player) player = std::make_unique<Player>(Vector3{ 0, EYE_HEIGHT, 0 });

    char path[64];
#ifdef MAZE_WEB
    // Web：存档必须写进 IndexedDB 挂载点（/maze），写相对路径刷新页面就没了。
    // 目录由 WebPlatform::InitFileSystem() 建好。
    snprintf(path, sizeof(path), "/maze/saves/slot%d.json", slot);
#else
    snprintf(path, sizeof(path), "saves/slot%d.json", slot);
#endif
    return GameSave::Load(path, *maze, *player, outData);
}

// ============================================================
// 手写页面：登录页 / 管理员密码页 / 记录排名页
// （Menu 只能画按钮列表，这三个页面要带输入框或表格，所以自己画）
// ============================================================
struct TextPageLayout
{
    Rectangle panel{};
    Rectangle input{};
    Rectangle btnA{};      // 登录页 = 登录按钮；密码页不用（宽 0）
    Rectangle btnB{};      // 登录页 = 跳过按钮；密码页不用（宽 0）
    float titleY = 0.0f;
    float subY = 0.0f;
    float msgY = 0.0f;
    float hintY = 0.0f;
};

static TextPageLayout MakeTextPageLayout(bool twoButtons)
{
    const float sw = (float)GetScreenWidth();
    const float sh = (float)GetScreenHeight();
    const float w = 560.0f;
    const float h = twoButtons ? 340.0f : 300.0f;

    TextPageLayout L;
    L.panel = Rectangle{ (sw - w) * 0.5f, (sh - h) * 0.5f, w, h };
    L.titleY = L.panel.y + 30.0f;
    L.subY = L.panel.y + 108.0f;
    L.input = Rectangle{ L.panel.x + 40.0f, L.panel.y + 156.0f, w - 80.0f, 56.0f };
    L.msgY = L.input.y + 60.0f;

    if (twoButtons)
    {
        const float btnY = L.input.y + 84.0f;
        const float bw = (w - 80.0f - 20.0f) * 0.5f;
        L.btnA = Rectangle{ L.panel.x + 40.0f, btnY, bw, 52.0f };
        L.btnB = Rectangle{ L.panel.x + 40.0f + bw + 20.0f, btnY, bw, 52.0f };
        L.hintY = L.panel.y + h - 30.0f;
    }
    else
    {
        L.btnA = Rectangle{ 0.0f, 0.0f, 0.0f, 0.0f };
        L.btnB = Rectangle{ 0.0f, 0.0f, 0.0f, 0.0f };
        L.hintY = L.panel.y + h - 30.0f;
    }
    return L;
}

// 排行榜页面当前查看哪一种榜。
enum class RankView
{
    Score,
    Time
};

struct RankLayout
{
    Rectangle boardTabs[2]{};   // 最高分榜 / 最快用时榜
    Rectangle tabs[7]{};        // 简单 / 普通 / 困难 / 地狱 / 神圣 / 饥饿 / 肉鸽
    Rectangle back{};
    float titleY = 0.0f;
    float subtitleY = 0.0f;
    float tableX = 0.0f;
    float tableW = 0.0f;
    float tableTop = 0.0f;
    float rowH = 0.0f;
    float colX[5]{};
};

static RankLayout MakeRankLayout()
{
    const float sw = (float)GetScreenWidth();
    RankLayout L;
    L.titleY = 24.0f;
    L.subtitleY = 148.0f;

    // 第一行：排行榜类型切换。
    // 标题下方内容整体下移，避免与上方标题重叠
    const float boardW = 180.0f;
    const float boardH = 46.0f;
    const float boardGap = 16.0f;
    const float boardTotal = 2.0f * boardW + boardGap;
    const float boardX = (sw - boardTotal) * 0.5f;
    L.boardTabs[0] = Rectangle{ boardX, 96.0f, boardW, boardH };
    L.boardTabs[1] = Rectangle{ boardX + boardW + boardGap, 96.0f, boardW, boardH };

    // 第二行：难度切换。
    const float tabW = 128.0f;
    const float tabH = 46.0f;
    const float gap = 16.0f;
    const float tabsW = 7.0f * tabW + 6.0f * gap;
    const float tabX = (sw - tabsW) * 0.5f;
    for (int i = 0; i < 7; ++i)
        L.tabs[i] = Rectangle{ tabX + (float)i * (tabW + gap), 176.0f, tabW, tabH };

    L.tableW = 880.0f;
    L.tableX = (sw - L.tableW) * 0.5f;
    L.tableTop = 248.0f;
    L.rowH = 34.0f;
    L.colX[0] = L.tableX + 30.0f;    // 名次
    L.colX[1] = L.tableX + 140.0f;   // 用户名
    L.colX[2] = L.tableX + 460.0f;   // 分数
    L.colX[3] = L.tableX + 600.0f;   // 用时
    L.colX[4] = L.tableX + 720.0f;   // 模式
    L.back = Rectangle{ (sw - 220.0f) * 0.5f, 652.0f, 220.0f, 50.0f };
    return L;
}

// 画一个扁平按钮（内容为 GBK 文案）
static void DrawFlatButton(Font font, Rectangle r, const char* gbkLabel, bool highlighted)
{
    if (r.width <= 0.0f) return;

    const bool hot = highlighted || CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.35f, 8,
        hot ? Color{ 58, 96, 190, 255 } : Color{ 24, 34, 60, 235 });
    DrawRectangleRoundedLinesEx(r, 0.35f, 8, 1.5f, Color{ 90, 130, 220, 255 });

    const std::string label = Utf8(gbkLabel);
    const Vector2 m = MeasureTextEx(font, label.c_str(), (float)UI_TEXT_SIZE, 1.0f);
    DrawTextEx(font, label.c_str(),
        Vector2{ r.x + (r.width - m.x) * 0.5f, r.y + (r.height - m.y) * 0.5f },
        (float)UI_TEXT_SIZE, 1.0f, RAYWHITE);
}

// 水平居中的 UTF-8 文本
static void DrawUtf8Centered(Font font, const std::string& utf8Text, float centerX, float y,
                             int fontSize, Color color)
{
    const Vector2 m = MeasureTextEx(font, utf8Text.c_str(), (float)fontSize, 1.0f);
    DrawTextEx(font, utf8Text.c_str(), Vector2{ centerX - m.x * 0.5f, y },
        (float)fontSize, 1.0f, color);
}

// 从 UTF-8 字符串里取下一个码点（非法字节返回 -1 并跳过）
static int NextCodepoint(const std::string& text, size_t& index)
{
    if (index >= text.size()) return -1;

    const unsigned char c = (unsigned char)text[index];
    int cp = 0;
    int extra = 0;

    if (c < 0x80) { cp = c; extra = 0; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
    else { ++index; return -1; }

    ++index;
    for (int i = 0; i < extra && index < text.size(); ++i, ++index)
        cp = (cp << 6) | ((unsigned char)text[index] & 0x3F);

    return cp;
}

// 暂停菜单右下角的"管理员"按钮
static Rectangle AdminButtonRect()
{
    return Rectangle{ (float)GetScreenWidth() - 176.0f,
                      (float)GetScreenHeight() - 74.0f, 156.0f, 48.0f };
}

// 状态名（只给终端遥测用，方便自动化脚本核对"现在走到哪一步了"）
static const char* GameStateName(GameState s)
{
    switch (s)
    {
    case GameState::MENU:        return "MENU";
    case GameState::HELP:        return "HELP";
    case GameState::DIFFICULTY:  return "DIFFICULTY";
    case GameState::LOAD_MENU:   return "LOAD_MENU";
    case GameState::LOGIN:       return "LOGIN";
    case GameState::RANKING:     return "RANKING";
    case GameState::DIFF_INFO:   return "DIFF_INFO";
    case GameState::ADMIN_LOGIN: return "ADMIN_LOGIN";
    case GameState::RL_LOGIN:    return "RL_LOGIN";
    case GameState::ROGUE_REWARD:return "ROGUE_REWARD";
    case GameState::PLAYING:     return "PLAYING";
    case GameState::PAUSED:      return "PAUSED";
    case GameState::ADMIN_BUFFS: return "ADMIN_BUFFS";
    }
    return "?";
}

// RL 权重目录：exe 在 <仓库>\codes\x64\<配置>\codes.exe，上溯两级到 <仓库>\codes
static std::string RlAssetDir()
{
#ifdef MAZE_WEB
    // Web：权重由 CMake --preload-file 打进虚拟 FS，路径写死即可。
    // 缺文件时 RlAgent 会自己退化成「朝出口贪心走」，所以这里不必报错。
    return std::string("/assets/rl");
#else
    char* exePath = nullptr;
    _get_pgmptr(&exePath);
    const std::string exe = exePath ? std::string(exePath) : std::string();
    std::string dir = ".";
    const size_t slash = exe.find_last_of("\\/");
    if (slash != std::string::npos) dir = exe.substr(0, slash);
    for (int i = 0; i < 2; ++i)
    {
        const size_t up = dir.find_last_of("\\/");
        if (up == std::string::npos) break;
        dir = dir.substr(0, up);
    }
    return dir + "\\rl_assets";
#endif
}

// RL 路线导出目录：<仓库>\rl\out（和 Python 训练产物放一起，.gitignore 已忽略）
static std::string RlRouteDir()
{
#ifdef MAZE_WEB
    // Web：路线导出落在 IndexedDB 目录里（纯调试产物，别往相对路径写）
    return std::string("/maze/rl_log");
#else
    std::string dir = RlAssetDir();                 // <仓库>\codes\rl_assets
    for (int i = 0; i < 2; ++i)                     // 再上溯两级到 <仓库>
    {
        const size_t up = dir.find_last_of("\\/");
        if (up == std::string::npos) return std::string("rl\\out");
        dir = dir.substr(0, up);
    }
    return dir + "\\rl\\out";
#endif
}

// 三个难度各一份权重；地狱复用困难（迷宫尺寸一样）
static std::string RlWeightFileName(Difficulty d)
{
    const char* name = (d == Difficulty::EASY) ? "easy"
        : ((d == Difficulty::HARD || d == Difficulty::HELL ||
            d == Difficulty::HOLY) ? "hard" : "normal");
    return std::string("policy_") + name + ".bin";
}

// 暂停菜单条目。管理员模式下额外插入 RL 入口：
// 原来 RL 入口是贴在窗口右下角的浮动按钮，为此必须在 PLAYING 里一直 EnableCursor()，
// 结果鼠标永远解锁、视角没法转。现在改成菜单项（管理员模式的入口本来就都在这个菜单里）。
// rlAllowed：只有正常模式才允许出现 RL 入口（奶龙模式没有 RL）
// ---- RL 门禁：只允许「简单 / 普通 / 困难」+ 正常模式第一人称 ----
// 训练环境（rl/rl/env_vec.py）里只有 墙 / 通路 / 敌人 / 宝物 四种语义；地狱 / 神圣 /
// 饥饿 / 肉鸽 里的玻璃墙、可破墙、减速带、地雷在 RL 观测里全被归一成通路。真跑起来
// 机器人会在减速带里跑不动（减速带内还不能加速跑）、踩地雷掉血、被玻璃墙卡住，
// 于是又慢又卡 —— 所以这几个难度直接不给 RL 入口。
static bool IsRlSupportedDifficulty(Difficulty d)
{
    return d == Difficulty::EASY || d == Difficulty::NORMAL || d == Difficulty::HARD;
}

static std::vector<MenuItem> PauseMenuItems(bool adminMode, bool rlMode, bool rlTrainMode,
                                           bool rlAllowed, bool rogueActive = false)
{
    std::vector<MenuItem> items{ { "继续游戏", MenuAction::RESUME } };

    // 肉鸽不支持存档 / 读档：本局从第一层一路打到第五层，中途不许改写局面
    if (!rogueActive)
    {
        items.push_back({ "保存到一", MenuAction::SAVE_SLOT_1 });
        items.push_back({ "保存到二", MenuAction::SAVE_SLOT_2 });
        items.push_back({ "保存到三", MenuAction::SAVE_SLOT_3 });
        items.push_back({ "读取存档", MenuAction::OPEN_LOAD_MENU });
    }

    items.push_back({ "记录排名", MenuAction::OPEN_RANKING });
    items.push_back({ "重新开始", MenuAction::RESTART });

    if (adminMode && rlAllowed && rlMode)
        items.push_back({ "停止 RL 自动通关", MenuAction::RL_STOP });
    else if (adminMode && rlAllowed && !rlTrainMode)
    {
        items.push_back({ "RL 自动通关（机器人代打）", MenuAction::RL_AUTO });
        items.push_back({ "RL 训练桥", MenuAction::RL_TRAIN });
    }

    if (adminMode && rogueActive)              // 管理员新功能（增益 / 跳层）只在肉鸽里给
    {
        items.push_back({ "增益选择", MenuAction::ADMIN_BUFFS });
        if (rogueActive)
        {
            items.push_back({ "跳到下一层", MenuAction::ADMIN_NEXT_FLOOR });
            items.push_back({ "回到上一层", MenuAction::ADMIN_PREV_FLOOR });
        }
    }


    items.push_back({ "返回主菜单", MenuAction::BACK });
    return items;
}

// ---- 肉鸽：增益档位 0 = 一般 / 1 = 特殊 / 2 = 稀有 ----
// 三档的枚举范围定义在 Config.h 的 BUFF_*_FIRST/LAST 里，抽卡、配色、
// 购买结算（只有稀有要花钱）都看这个函数，别再各写一份判断。
static int BuffTier(Buff b)
{
    const int v = (int)b;
    if (v >= BUFF_RARE_FIRST)    return 2;
    if (v >= BUFF_SPECIAL_FIRST) return 1;
    return 0;
}

// ---- 肉鸽：这一档要花几个宝物（一般 / 特殊免费，稀有 1 个）----
static int BuffCost(Buff b)
{
    return (BuffTier(b) == 2) ? ROGUE_RARE_COST : 0;
}

// ---- 肉鸽：增益名字（弹窗用，GBK）----
static const char* BuffName(Buff b)
{
    switch (b)
    {
    case Buff::HUNGER_SLOW:    return "慢速消化";
    case Buff::ENEMY_WEAK:     return "怪力削弱";
    case Buff::STRONG_BULLET:  return "一击必杀";
    case Buff::FAST_MOVE:      return "疾行";
    case Buff::DARK_FREE:      return "暗视";
    case Buff::MORE_HP:        return "强健";
    case Buff::MORE_SCORE:     return "高分";
    case Buff::MINE_NO_BLIND:  return "防眩";
    case Buff::BREAK_ALL:      return "拆迁";
    case Buff::SHOW_EXIT:      return "雷达";
    case Buff::MORE_SATIETY:   return "饱食";
    case Buff::ENEMY_HALF:     return "怪少一半";
    case Buff::TREASURE_X2:    return "宝物翻倍";
    case Buff::AMMO_X2:        return "弹药翻倍";
    case Buff::HEAL_X2:        return "血包翻倍";
    case Buff::DOUBLE_JUMP:    return "二段跳";
    case Buff::SLOW_TIMER:     return "计时减慢";
    case Buff::KILL_SATIETY:   return "击杀回饱腹";
    case Buff::HURT_SPEED:     return "受伤提速";
    case Buff::TELEPORT_START: return "随机启程";
    case Buff::COPY_MAP:       return "复刻地图";
    case Buff::NO_ENEMY:       return "清场";
    case Buff::NO_MINE:        return "排雷";
    case Buff::INF_SATIETY:    return "无限饱腹";
    case Buff::NO_LIGHT_LIMIT: return "常亮视野";
    case Buff::SHRINK_MAZE:    return "迷宫缩小";
    case Buff::LOCK_HP:        return "锁血";
    case Buff::COMPANION:      return "伙伴带路";
    case Buff::HOVER:          return "滞空";
    case Buff::PHASE_THROUGH:  return "穿墙";
    case Buff::SKIP_FLOOR:     return "跳层";
    }
    return "增益";
}

// ---- 肉鸽：增益说明（弹窗里那行小字）----
static const char* BuffDesc(Buff b)
{
    switch (b)
    {
    case Buff::HUNGER_SLOW:    return "饱腹值消耗减半（原来的一半）";
    case Buff::ENEMY_WEAK:     return "怪物每下最多打掉 5 点血";
    case Buff::STRONG_BULLET:  return "子弹一发就能打掉一只怪";
    case Buff::FAST_MOVE:      return "走路和跑步速度都翻倍";
    case Buff::DARK_FREE:      return "开局 30 秒内不受光源限制";
    case Buff::MORE_HP:        return "生命上限 +50，并回满这 50 点";
    case Buff::MORE_SCORE:     return "立刻 +50 分";
    case Buff::MINE_NO_BLIND:  return "踩地雷不再致盲（伤害不变）";
    case Buff::BREAK_ALL:      return "打碎地图上所有可破墙";
    case Buff::SHOW_EXIT:      return "雷达上把终点标成绿点";
    case Buff::MORE_SATIETY:   return "饱腹上限 +20，并回满这些";
    case Buff::ENEMY_HALF:     return "此后每层怪物数量减半";
    case Buff::TREASURE_X2:    return "此后每层金色宝物数量翻倍";
    case Buff::AMMO_X2:        return "此后每层弹药包数量翻倍";
    case Buff::HEAL_X2:        return "此后每层血包数量翻倍";
    case Buff::DOUBLE_JUMP:    return "跳起来以后在空中还能再跳一次";
    case Buff::SLOW_TIMER:     return "计时速度减半：1 秒只记 0.5 秒";
    case Buff::KILL_SATIETY:   return "每击杀一只怪回复 10 点饱腹值";
    case Buff::HURT_SPEED:     return "受伤后 5 秒内移速提升到 4 倍";
    case Buff::TELEPORT_START: return "开局传送到地图上的随机位置";
    case Buff::COPY_MAP:       return "下一层直接用这一层的地图";
    case Buff::NO_ENEMY:       return "之后各层不再刷怪物";
    case Buff::NO_MINE:        return "之后各层不再刷地雷";
    case Buff::INF_SATIETY:    return "饱腹值不再下降";
    case Buff::NO_LIGHT_LIMIT: return "不用大笑也永远不受光源限制";
    case Buff::SHRINK_MAZE:    return "迷宫缩小一档（困难变普通、普通变简单）";
    case Buff::LOCK_HP:        return "血量不再下降（代价：整局不能跑步）";
    case Buff::COMPANION:      return "绿方块伙伴带你走到终点、每 2 秒帮你打一发（代价：血上限锁 10、移速 1/4）";
    case Buff::HOVER:          return "长按空格可以一直滞空，松手立刻落下来";
    case Buff::PHASE_THROUGH:  return "可以直接穿过墙壁（代价：下一层回到第一层重新开始）";
    case Buff::SKIP_FLOOR:     return "直接跳过下一层（代价：用时 +150 秒）";
    }
    return "";
}

// ---- 肉鸽增益弹窗：右下角「刷新」按钮的矩形（输入与绘制共用）----
static Rectangle RogueRefreshRect()
{
    return Rectangle{ (float)GetScreenWidth() - 210.0f, 92.0f, 170.0f, 44.0f };
}

// ---- 肉鸽增益弹窗：第 index 行的矩形（输入与绘制共用同一个几何，鼠标点得准）----
static Rectangle RogueOfferRow(int index)
{
    const float rowW = 720.0f;
    const float rowH = 46.0f;              // 第 5 层最多 10 行：10*(46+8)=540，118~658 排得下
    const float gap = 8.0f;
    const float rowX = (float)GetScreenWidth() * 0.5f - rowW * 0.5f;
    const float topY = 118.0f;
    return Rectangle{ rowX, topY + (float)index * (rowH + gap), rowW, rowH };
}

// ---- 管理员「增益选择」页：一屏 ADMIN_BUFF_ROWS 行，光标滑出去就跟着滚 ----
static const int BUFF_ALL_COUNT = BUFF_RARE_LAST - BUFF_GENERAL_FIRST + 1;
static const int ADMIN_BUFF_ROWS = 12;

static int AdminBuffScroll(int cursor)
{
    int scroll = cursor - ADMIN_BUFF_ROWS + 1;
    if (scroll < 0) scroll = 0;
    const int maxScroll = (BUFF_ALL_COUNT > ADMIN_BUFF_ROWS)
        ? (BUFF_ALL_COUNT - ADMIN_BUFF_ROWS) : 0;
    if (scroll > maxScroll) scroll = maxScroll;
    return scroll;
}

static Rectangle AdminBuffRow(int index, int scroll)
{
    const float rowW = 720.0f;
    const float rowH = 44.0f;
    const float gap = 2.0f;
    const float rowX = (float)GetScreenWidth() * 0.5f - rowW * 0.5f;
    const float topY = 112.0f;
    return Rectangle{ rowX, topY + (float)(index - scroll) * (rowH + gap), rowW, rowH };
}

// 背景音乐索引：神圣复用地狱那首（4 个难度只有 4 首曲子）
static int BgmIndex(Difficulty d)
{
    // 神圣 / 饥饿 复用地狱那首；肉鸽由调用方传"当前层难度"，这里兜底也按地狱算
    if (d == Difficulty::HOLY || d == Difficulty::HUNGER || d == Difficulty::ROGUE)
        return (int)Difficulty::HELL;
    return (int)d;
}

// 地面：按迷宫实际尺寸算，四周各留 4 格边距，保证迷宫边缘不会露出蓝色背景
static void GroundRect(const Maze& maze, Vector3& outPos, Vector3& outSize)
{
    const float w = (float)maze.grid.width * CELL_SIZE;
    const float h = (float)maze.grid.height * CELL_SIZE;
    const float margin = CELL_SIZE * 4.0f;

    outPos = Vector3{ w * 0.5f, 0.0f, h * 0.5f };
    outSize = Vector3{ w + margin * 2.0f, 0.2f, h + margin * 2.0f };
}

int main()
{
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Maze Game");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);       // 关掉 raylib 默认的 ESC 关窗口，ESC 留给暂停

    // ---- 奶龙（第三人称主角）：有 glb 就加载模型，没有就程序化建模 ----
    nailong = std::make_unique<Nailong>();
    nailong->Load();

    

    // ---- 难度 ----
    Difficulty currentDifficulty = Difficulty::NORMAL;

    // ---- 饥饿模式（饱腹值）运行期状态 ----
    // 饥饿模式 / 肉鸽模式共用：平时 4s 掉 1 点饱腹，冲刺 2s 掉 1 点（加速 = 双倍消耗）；
    // 饱腹归零当场致盲 3 秒，随后进入饥饿掉血态（4s -1 血，冲刺 2s -1 血）；
    // 吃到任意宝物 / 弹药包 / 血包回 5 点。能量条整条换成这个红色饱腹条，冲刺不再受能量限制。
    struct HungerRuntime
    {
        bool  enabled = false;      // 饥饿 / 肉鸽 模式启用
        bool  infinite = false;     // 增益：饱腹值不再下降
        float drainScale = 1.0f;    // 增益：饱腹消耗减半（0.5）
        float satiety = 0.0f;       // 当前饱腹值
        float satietyMax = 1.0f;    // 饱腹值上限
        float blindTimer = 0.0f;    // 饱腹耗尽致盲剩余 (s)
        bool  starving = false;     // 已进入饥饿掉血态
        float starveTick = 0.0f;    // 饥饿掉血计时 (s)

        void Reset(float startValue, float maxValue)
        {
            satiety = startValue;
            satietyMax = (maxValue > 0.0f) ? maxValue : 1.0f;
            blindTimer = 0.0f;
            starving = false;
            starveTick = 0.0f;
        }
        void Add(float amount)                      // 吃补给 / 增益：回饱腹（不超上限）
        {
            satiety = min(satietyMax, satiety + amount);
        }
        void AddMax(float amount)                   // 增益：上限 +amount，同时回等量
        {
            satietyMax += amount;
            satiety += amount;
        }
        float Ratio() const
        {
            return (satietyMax > 0.0f) ? (satiety / satietyMax) : 0.0f;
        }
    };
    HungerRuntime hunger;

    // HUD 显示用：本局实际撒了多少金色宝物 / 本局弹药上限。
    // 饥饿模式宝物翻倍、肉鸽每层难度不同，所以分母不能再用宏硬编码。
    int runTreasureTarget = TREASURE_SCORE_COUNT;
    int runAttackLimit = EnemyConfig::PLAYER_ATTACK_LIMIT;

    // ---- 肉鸽模式（难度第 7 项）运行期状态 ----
    // 5 层连打：每层通关弹一次增益窗，选到的增益在本局剩余所有层里一直有效，退出本局即失效。
    struct MazeSnapshot                       // 「复刻地图」增益要的整张地图快照
    {
        bool  valid = false;
        int   width = 0;
        int   height = 0;
        std::vector<CellType> cells;
        int   entryGx = 1, entryGy = 1;
        int   exitGx = 0, exitGy = 0;
    };

    struct RogueRuntime
    {
        bool  active = false;                 // 本局是不是肉鸽
        int   floor = 0;                      // 当前层（0..ROGUE_FLOOR_COUNT-1）
        bool  copyNextMap = false;            // 增益「复刻地图」：下一层用本层地图
        float darkFreeTimer = 0.0f;           // 增益「暗视」：开局 30s 无光源限制
        std::vector<Buff> owned;              // 已选增益

        // 增益弹窗
        std::vector<Buff> offered;            // 这一窗的候选
        std::vector<int>  chosen;             // 已勾选的候选下标
        int   cursor = 0;
        int   pickCount = 1;                  // 要选几个

        // ---- 宝物币 / 刷新（稀有增益要花宝物买；刷新全局只有一次）----
        int   treasureCoins = 0;              // 宝物币：每层捡到的金宝物 + 第 5 层白送的
        int   goldPicked = 0;                 // 本局一共捡到几个金宝物（宝物币的来源，只统计不花）
        bool  giftGiven = false;              // 第 5 层的白送宝物发过没有（刷新不重复发）
        int   refreshLeft = ROGUE_REFRESH_TIMES;   // 还剩几次刷新机会

        // ---- 增益的运行时状态 ----
        float hurtSpeedTimer = 0.0f;          // 「受伤提速」剩余时间 (s)
        int   killSatietyScore = 0;           // 「击杀回饱腹」累计回过的饱腹值（只用于 HUD 显示）

        // 「伙伴」增益：绿方块 + 带路 + 帮打怪
        bool  companionActive = false;
        Vector3 companionPos{ 0.0f, 0.0f, 0.0f };
        float companionFireTimer = 0.0f;      // 距离下一发还有多久
        float companionFieldTimer = 0.0f;     // 距离场重算计时（拆墙 / 破雷后要跟着更新）

        bool Has(Buff b) const
        {
            for (Buff x : owned) if (x == b) return true;
            return false;
        }
        void Clear()
        {
            active = false;
            floor = 0;
            copyNextMap = false;
            darkFreeTimer = 0.0f;
            owned.clear();
            offered.clear();
            chosen.clear();
            cursor = 0;
            pickCount = 1;
            goldPicked = 0;
            treasureCoins = 0;
            giftGiven = false;
            refreshLeft = ROGUE_REFRESH_TIMES;
            companionActive = false;
            companionPos = Vector3{ 0.0f, 0.0f, 0.0f };
            companionFireTimer = 0.0f;
            companionFieldTimer = 0.0f;
            hurtSpeedTimer = 0.0f;
            killSatietyScore = 0;
        }
    };
    RogueRuntime rogue;
    MazeSnapshot rogueMap;

    // 肉鸽 5 层的难度：简单 / 困难 / 地狱 / 神圣 / 神圣（最后一层墙全透明）
    const Difficulty kRogueFloor[ROGUE_FLOOR_COUNT] = {
        Difficulty::EASY, Difficulty::HARD, Difficulty::HELL,
        Difficulty::HOLY, Difficulty::HOLY };

    // 每层增益窗的选项数 / 必选数：3选2 / 5选3 / 5选3 / 8选4 / 10选5
    const int kRogueOptN[ROGUE_FLOOR_COUNT] = { 3, 5, 5, 8, 10 };
    const int kRoguePickN[ROGUE_FLOOR_COUNT] = { 2, 3, 3, 4, 5 };

    // 肉鸽：游戏内一切"难度相关"逻辑都按当前层的难度算（写日志 / 排名仍然记 ROGUE）
    const auto EffectiveDifficulty = [&]() -> Difficulty
        {
            if (currentDifficulty != Difficulty::ROGUE) return currentDifficulty;
            const int f = (rogue.floor < ROGUE_FLOOR_COUNT) ? rogue.floor : ROGUE_FLOOR_COUNT - 1;
            return kRogueFloor[f];
        };

    // 奶龙大笑计时（奶龙 F 键动画用；原来的「狂笑」增益已改成常亮视野）
    float laughTimer = 0.0f;

    // 肉鸽稀有增益「伙伴」：到终点的 BFS 距离场（复用 RL 那套 MazeSolver）


    // ---- 迷宫 / 玩家 / 敌人（用 unique_ptr，方便按难度重建）----
    std::unique_ptr<Maze> maze;
    std::unique_ptr<Player> player;
    std::unique_ptr<EnemyManager> enemyManager;

    int playerHealth = 0;
    int playerAttacksRemaining = 0;
    float playerAttackCooldown = 0.0f;
    int score = 0;
    bool escaped = false;

    PlayerSnapshot playerSnapshot;

#ifdef MAZE_WEB
    // Web：先把 IndexedDB 挂到 /maze 上，之后 Account / Save 才有地方落盘。
    // 读回旧数据是异步的，所以这里不等待 —— 不影响开局，只影响"上次的成绩还在不在"。
    web::InitFileSystem();
#endif

    // ---- 全网榜后端地址 ----
    // 默认同域 /api（Cloudflare Pages 把 /api/* 路由到 Worker）。
    // 想指到别的域名 / 本地 python 后端，改 web/config.js 的 apiBase，不用重新编译。
    lb::SetBaseUrl("/api");

    // ---- 字体（加载失败会自动退回默认字体）----
    UIFont uiFont;
    uiFont.Load(UI_FONT_SIZE, UI_GLYPHS);
    Font font = uiFont.Get();

    // ---- 计时器 ----
    GameTimer timer;
    timer.SetFont(font);

 

    // ---- 开始界面 ----
    Menu mainMenu("3D 迷宫",
        { { "开始游戏", MenuAction::START_GAME },
          { "记录排名", MenuAction::OPEN_RANKING },
          { "读取存档", MenuAction::OPEN_LOAD_MENU },
          { "操作说明", MenuAction::HELP },
          { "退出游戏", MenuAction::EXIT_GAME } },
        MenuStyle::PAGE);

    mainMenu.SetSubtitle("MAZE GAME");
    mainMenu.SetHint("W/S 或 上下键 选择   Enter 确认   ESC 退出");
    mainMenu.SetFont(font);

    // 退出游戏前先问一句：任何误触（ESC / 鼠标点错）都不再直接杀进程
    Menu confirmExitMenu("退出游戏",
        { { "退出游戏", MenuAction::EXIT_GAME },
          { "返回",     MenuAction::BACK } });

    Menu loadMenu("读取存档",
        { { "存档一", MenuAction::LOAD_SLOT_1 },
          { "存档二", MenuAction::LOAD_SLOT_2 },
          { "存档三", MenuAction::LOAD_SLOT_3 },
          { "返回",   MenuAction::BACK } },
        MenuStyle::PAGE);
    loadMenu.SetHint("W/S 或 上下键 选择   Enter 确认   ESC 返回");
    loadMenu.SetFont(font);

    // ---- 难度选择菜单 ----
    Menu difficultyMenu("难度选择",
        { { "简单", MenuAction::DIFF_EASY,    MenuAction::INFO_EASY },
          { "普通", MenuAction::DIFF_NORMAL,  MenuAction::INFO_NORMAL },
          { "困难", MenuAction::DIFF_HARD,    MenuAction::INFO_HARD },
          { "地狱", MenuAction::DIFF_HELL,    MenuAction::INFO_HELL },
          { "神圣", MenuAction::DIFF_HOLY,    MenuAction::INFO_HOLY },
          { "饥饿", MenuAction::DIFF_HUNGER,  MenuAction::INFO_HUNGER },
          { "肉鸽", MenuAction::DIFF_ROGUE,   MenuAction::INFO_ROGUE },
          { "返回", MenuAction::BACK } },
        MenuStyle::PAGE);
    difficultyMenu.SetSubtitle("选中难度后按 Enter 直接开始\n"
                               "点右侧 ? 查看该难度的详情（地图大小 / 敌人数量 / 是否限制视野）");
    difficultyMenu.SetHint("W/S 或 上下键 选择   Enter 开始/确认   ESC 返回");
    difficultyMenu.SetFont(font);

    // ---- 模式选择（登录 / 跳过后先选模式，再选难度）----
    Menu modeMenu("选择模式",
        { { "正常模式", MenuAction::MODE_NORMAL },
          { "奶龙模式", MenuAction::MODE_NAILONG },
          { "返回",     MenuAction::BACK } },
        MenuStyle::PAGE);
    modeMenu.SetSubtitle("正常模式  第一人称原版玩法（锁定第一人称）\n"
                         "奶龙模式  第三人称奶龙，V 键切换视角，也支持管理员模式");
    modeMenu.SetHint("W/S 或 鼠标 选择   Enter 确认   ESC 返回");
    modeMenu.SetFont(font);

    // ---- 当前难度名（HUD 显示用）----
    const auto DifficultyName = [](Difficulty d) -> const char*
        {
            switch (d)
            {
            case Difficulty::EASY: return "简单";
            case Difficulty::HARD: return "困难";
            case Difficulty::HELL: return "地狱";
            case Difficulty::HOLY: return "神圣";
            case Difficulty::HUNGER: return "饥饿";
            case Difficulty::ROGUE: return "肉鸽";
            default:               return "普通";
            }
        };

    // ---- 操作说明 ----
    Menu helpMenu("操作说明",
                  { { "返回", MenuAction::BACK } },
                  MenuStyle::DIALOG);
    helpMenu.SetSubtitle("W A S D    移动\n"
                         "Shift      奔跑\n"
                         "Space      跳跃\n"
                         "鼠标       转动视角\n"
                         "ESC        暂停\n"
                         "目标：走到出口（宝物为可选加分）");
    helpMenu.SetHint("Enter / ESC 返回");
    helpMenu.SetFont(font);

    // ---- 难度详情弹窗（点难度菜单右边的 ? 弹出）----
    Menu diffInfoMenu("难度详情",
                      { { "返回", MenuAction::BACK } },
                      MenuStyle::DIALOG);
    diffInfoMenu.SetHint("Enter / ESC 返回");
    diffInfoMenu.SetFont(font);

    // ---- 暂停界面（条目由 PauseMenuItems 统一生成，管理员模式下多两条 RL 入口）----
    Menu pauseMenu;
    pauseMenu.Init("已暂停", PauseMenuItems(false, false, false, false), MenuStyle::DIALOG);
    pauseMenu.SetHint("ESC 直接继续");
    pauseMenu.SetFont(font);

    // ---- 结算界面（死亡 / 通关）----
    Menu overMenu;
    overMenu.Init("",
                  { { "重新开始", MenuAction::RESTART },
                    { "记录排名", MenuAction::OPEN_RANKING },
                    { "返回主菜单", MenuAction::BACK } },
                  MenuStyle::DIALOG);
    overMenu.SetHint("R 重新开始    ESC 返回主菜单");
    overMenu.SetFont(font);

    // 游戏内键位提示
    // 游戏中的键位提示：只有奶龙模式才有 V 切视角 / F 大笑
    const string hudHintNailong = Utf8("WASD 移动    Shift 疾跑    Space 跳跃    鼠标 转视角    V 切视角    F 大笑    Tab 锁定鼠标    ESC 暂停");
    const string hudHintNormal  = Utf8("WASD 移动    Shift 疾跑    Space 跳跃    鼠标 转视角    Tab 锁定鼠标    ESC 暂停");

    // 地面：每帧按迷宫实际尺寸算（见 GroundRect），保证边缘不会露出蓝色背景
    Vector3 groundPos{ 0.0f, 0.0f, 0.0f };
    Vector3 groundSize{ 0.0f, 0.2f, 0.0f };

    // ---- 光源（视野限制）：只有"地狱"难度会用到 ----
    LightSource light;
    light.Init(GetScreenWidth(), GetScreenHeight());

    // ---- 敌人脚步声：巡逻怪和追击怪使用不同样本 ----
    FootstepAudio footstepAudio;
    footstepAudio.Load();   // 加载失败时游戏继续运行，只是没有脚步声音效

    // ---- 普通游戏音效：宝物拾取、玩家受伤、发射光球 ----
    SfxAudio sfxAudio;
    sfxAudio.Load();        // 加载失败时游戏继续运行，只是没有对应音效

    // ---- 登录 / 记录排名 / 管理员模式 ----
    TextInput nameInput;                        // 登录页用户名输入框
    TextInput passwordInput;                    // 管理员密码输入框
    std::string playerName = Utf8("游客");      // 未登录就按游客记录
    bool loginError = false;
    bool adminMode = false;
    int  adminBuffCursor = 0;                  // 管理员「增益选择」页的光标
    bool passwordError = false;

    // ---- 强化学习（RL）模式 ----
    bool rlMode = false;          // 机器人自动走迷宫（离线推理，游戏内直接看）
    bool rlTrainMode = false;     // 训练桥：把本局当环境交给 Python PPO（需要手动开）
    bool rlUnlocked = false;      // 密码 rl666666 通过后解锁
    bool rlPasswordError = false;
    bool rlHalted = false;        // 机器人超时/停下
    bool rlFinishing = false;    // 已踏进出口格：等 walker 真正走进出口再收尾
int rlLastEnemyAlive = -1;    // 上一帧活着的敌人数（减少 = 我们打死了它）
bool rlEnemyCountReady = false;  // 敌人线程 tick 过一次没有（快照为空 != 全灭）
    TextInput rlPasswordInput;
    rl::RLNet rlNet;
    rl::RlAgent rlAgent;
    rl::RlWalker rlWalker;
    rl::RlBridge rlBridge;
    std::string rlStatus = "RL: 未加载权重";
    int rlLastAction = -1;
    bool rlSprint = false;              // 当前这一步是否按住 Shift（冲刺）
    float rlLastReward = 0.0f;
    int rlSteps = 0;
    double rlEpisodeReturn = 0.0;
    double rlReturnSum = 0.0;
    int rlEpisodeCount = 0;
    float rlWalkerStall = 0.0f;       // 走路卡死计时（秒），只用来打终端诊断日志

    // 需要时把当前难度的权重读进来（RLNet 只做前向推理，纯 C++）
    const auto EnsureRlWeights = [&]() -> bool
        {
            const std::string fileName = RlWeightFileName(currentDifficulty);
            const std::string candidates[2] = {
                RlAssetDir() + "\\" + fileName,          // <仓库>\codes\rl_assets
                std::string("rl_assets\\") + fileName,   // 相对工作目录（VS 调试默认在 codes\）
            };
            for (const std::string& path : candidates)
            {
                if (rlNet.Ready() && rlNet.Path() == path) return true;
                std::string err;
                if (rlNet.Load(path, &err))
                {
                    rlStatus = "RL: 权重已加载 " + fileName;
                    return true;
                }
            }
            rlStatus = "RL: 找不到 " + fileName;
            return false;
        };

    // 一局结束后把"实际路线 vs 最短路线"导出到终端 / rl\out\route_last.txt
    const auto ReportRlRoute = [&](bool success)
        {
            const rl::DistField& field = rlAgent.Field();
            if (!field.Ready()) return;
            std::vector<int> distFlat;
            distFlat.reserve((size_t)field.Width() * field.Height());
            for (int gy = 0; gy < field.Height(); ++gy)
                for (int gx = 0; gx < field.Width(); ++gx)
                    distFlat.push_back(field.At(gx, gy));
            rl::terminal::RouteReport(distFlat, field.Width(), field.Height(),
                (int)rlEpisodeCount, success, true);
        };
    int rankTab = 1;                            // 0..4 = 简单 / 普通 / 困难 / 地狱 / 神圣
    RankView rankView = RankView::Score;         // 当前查看最高分榜还是最快用时榜
    std::vector<ScoreRecord> rankRows;
    GameState rankReturn = GameState::MENU;     // 排名页返回到哪里

    // ---- 字体热补：用户输入的名字 / 记录里的中文名不在图集里会显示成 '?' ----
    const auto RefreshFonts = [&]()
        {
            font = uiFont.Get();
            mainMenu.SetFont(font);
            loadMenu.SetFont(font);
            difficultyMenu.SetFont(font);
            modeMenu.SetFont(font);
            helpMenu.SetFont(font);
            pauseMenu.SetFont(font);
            overMenu.SetFont(font);
            diffInfoMenu.SetFont(font);
            timer.SetFont(font);
        };

    const auto EnsureGlyphs = [&](const std::string& utf8Text)
        {
            std::vector<int> need;
            size_t index = 0;
            while (index < utf8Text.size())
            {
                const int cp = NextCodepoint(utf8Text, index);
                if (cp <= 32 || uiFont.HasCodepoint(cp)) continue;

                bool dup = false;
                for (int v : need) if (v == cp) { dup = true; break; }
                if (!dup) need.push_back(cp);
            }

            if (need.empty()) return;
            if (uiFont.ReloadWith(need)) RefreshFonts();
            else TraceLog(LOG_WARNING, "EnsureGlyphs: font reload failed");
        };

    const auto LoadRank = [&](int tab)
        {
            rankTab = tab;
            const RankBoard board = (rankView == RankView::Time)
                ? RankBoard::Time
                : RankBoard::Score;
            rankRows = Account::RankByDifficulty(
                static_cast<Difficulty>(tab),
                board,
                RANK_LIST_MAX
            );

            std::string names;
            for (const ScoreRecord& r : rankRows) names += r.name;
            EnsureGlyphs(names);
        };

    // ---- 难度详情文案 ----
    const auto DifficultyInfoText = [](Difficulty d) -> std::string
        {
            if (d == Difficulty::ROGUE)
            {
                std::string rogueText =
                    "肉鸽 5 层连打：简单 → 困难 → 地狱 → 神圣 → 神圣（墙全透明）\n";
                rogueText += "初始生命 100 / 满弹药 / 饱腹值 30，层间继承血量 弹药 分数 饱腹\n";
                rogueText += "每层开场选增益：3 选 2 → 5 选 3 → 5 选 3 → 8 选 4 → 10 选 5\n";
                rogueText += "一般增益免费，稀有增益要花 1 个宝物买；一局只有 1 次刷新机会\n";
                rogueText += "第 3、4 层必出一个特殊增益；第 5 层必出一个稀有并白送 1 个宝物\n";
                rogueText += "增益只在本局生效（退出即失效）；已选过的不会再出现\n";
                rogueText += "5 层全部通关才记录总分与总时间；中途死亡或退出不记录";
                return rogueText;
            }

            const DifficultyConfig cfg = GetDifficultyConfig(d);

            std::string text = "地图大小  " + std::to_string(cfg.mazeWidth) + " x " +
                std::to_string(cfg.mazeHeight) + " 格\n";
            text += "敌人数量  " + std::to_string(cfg.enemyCount) + " 个\n";
            text += "初始生命  " + std::to_string(cfg.playerMaxHealth) + "\n";
            text += "初始攻击  " + std::to_string(cfg.attackLimit) + " 次\n";
            text += std::string("光源限制  ") + (IsLightLimited(d)
                ? ("有（光照半径 " + std::to_string((int)LIGHT_RADIUS_METERS) + " 米）")
                : std::string("无")) + "\n";
            if (d == Difficulty::HOLY || d == Difficulty::HUNGER)
            {
                text += "特殊墙体  玻璃墙 " + std::to_string(HOLY_GLASS_WALL_MIN) + "-" +
                    std::to_string(HOLY_GLASS_WALL_MAX) + " 面（替换迷宫墙体，看得见但挡路）\n";
                text += "          可破墙 " + std::to_string(HOLY_BREAK_WALLS) +
                    " 面（堵住路口，一发子弹打碎）\n";
            }
            if (d == Difficulty::HUNGER)
            {
                text += "饱腹值    10 点（平时 4 秒 -1，冲刺 2 秒 -1）\n";
                text += "          归零致盲 3 秒，之后每 4 秒 -1 血（冲刺 2 秒）\n";
                text += "补给      宝物 / 弹药包 / 血包均回复 5 点饱腹\n";
                text += "加速      可无限冲刺，但饱腹双倍消耗\n";
            }

            const int treasureMul = (d == Difficulty::HUNGER) ? HUNGER_TREASURE_SCALE : 1;
            text += "宝物数量  " + std::to_string(TREASURE_TOTAL_COUNT * treasureMul) + " 个（金 " +
                std::to_string(TREASURE_SCORE_COUNT * treasureMul) + " / 蓝 " +
                std::to_string(TREASURE_ATTACK_COUNT * treasureMul) + " / 红 " +
                std::to_string(TREASURE_HEAL_COUNT * treasureMul) + "）";
            return text;
        };

    GameState state = GameState::MENU;
    bool shouldExit = false;
    EnableCursor();
    bool overMenuShown = false;                   // 结算菜单是否已弹出
    bool wasSprinting = false;                    // 上一帧是否正在加速跑，用于只在起步瞬间播放音效
    bool wasOnSlowZone = false;                   // 上一帧是否踩在减速带上，用于进入瞬间播放音效
    float mineFlashTimer = 0.0f;                  // 踩雷红色全屏闪烁剩余时间 (s)
    float mineShakeTimer = 0.0f;                  // 踩雷屏幕震动剩余时间 (s)
    float mineBlindTimer = 0.0f;                  // 踩雷致盲（窗口全黑）剩余时间 (s)
    // ---- 玩家脚步声 / 喘气的运行时状态 ----
    Vector3 lastFootstepPos{ 0.0f, 0.0f, 0.0f };
    bool    lastFootstepPosValid = false;
    float   playerFootstepAccum = 0.0f;

    // 神圣模式：刚被打碎的可破墙（画一小段亮光，提示"打中了"）
    Vector3 holyBreakFlashPos = { 0.0f, 0.0f, 0.0f };
    float   holyBreakFlashTimer = 0.0f;

    // ---- 开一局 / 重开：玩家、宝物、血量、计分、计时、敌人全部复位 ----
       // ---- 开一局 / 重开：按难度重建迷宫、玩家、敌人 ----
    const auto ResetRun = [&](bool spawnEnemies = true, bool inheritStats = false)
        {
            // 肉鸽：层间继承（血量 / 上限 / 弹药 / 分数 / 饱腹值 带回下一层；重开才清零）
            const int   keepHealth = playerHealth;
            const int   keepMaxHealth = playerMaxHealth;
            const int   keepAttacks = playerAttacksRemaining;
            const int   keepScore = score;
            const float keepSatiety = hunger.satiety;
            const float keepSatietyMax = hunger.satietyMax;

            // 模式决定视角：正常模式锁死第一人称，奶龙模式默认第三人称（可 V 切）
            thirdPerson = (gameMode == GameMode::NAILONG);
            if (enemyManager)
            {
                enemyManager->Stop();
                enemyManager.reset();     // ← 就是加这一行
            }

            // ---- 按当前难度重建迷宫（肉鸽用"当前层"的难度）----
            const Difficulty effDiff = EffectiveDifficulty();
            // 增益「迷宫缩小」：缩小一档（困难->普通、普通->简单；地狱 / 神圣 / 饥饿->普通，
            // 简单已经最小就不再缩），只有迷宫尺寸变了，其他参数照旧按本层难度算。
            Difficulty sizeDiff = effDiff;
            if (rogue.active && rogue.Has(Buff::SHRINK_MAZE))
            {
                switch (effDiff)
                {
                case Difficulty::NORMAL: sizeDiff = Difficulty::EASY;   break;
                case Difficulty::EASY:   sizeDiff = Difficulty::EASY;   break;
                case Difficulty::HARD:   sizeDiff = Difficulty::NORMAL; break;
                default:                 sizeDiff = Difficulty::NORMAL; break;
                }
            }
            const DifficultyConfig sizeCfg = GetDifficultyConfig(sizeDiff);
            const DifficultyConfig cfg = GetDifficultyConfig(effDiff);

            // 肉鸽第 5 层：除了可破墙，其余墙体全部换成全透明玻璃墙
            const bool voidWalls = rogue.active && rogue.floor == ROGUE_FLOOR_COUNT - 1;
            // 增益「复刻地图」：把本层地图原样搬到下一层（连尺寸一起）
            const bool copyMap = rogue.active && rogue.copyNextMap && rogueMap.valid;

            // 尺寸按「迷宫缩小」之后的档位（sizeCfg），其他参数照旧用本层难度
            maze = std::make_unique<Maze>(copyMap ? rogueMap.width : sizeCfg.mazeWidth,
                                          copyMap ? rogueMap.height : sizeCfg.mazeHeight);
            // 按难度写入减速带参数（条数 / 每条格数 / 减速倍率）
            // RL 模式：不撒减速带 / 地雷。训练环境（rl/rl/env_vec.py）里没有这两种格子，
            // 观测里它们被归一成通路；真撒下去机器人会在减速带里掉到 40%~60% 速度（还不能
            // 加速跑）、踩地雷掉血，于是又慢又卡。RL 模式的迷宫与训练环境保持完全一致。
            const bool rlPlainMaze = (rlMode || rlTrainMode);
            maze->slowZoneSegments = rlPlainMaze ? 0 : cfg.slowZoneSegments;
            maze->slowZoneMinCells = cfg.slowZoneMinCells;
            maze->slowZoneMaxCells = cfg.slowZoneMaxCells;
            maze->slowZoneFactor = cfg.slowZoneFactor;
            // 增益「排雷」：之后各层都不再生成地雷
            maze->mineCount = (rlPlainMaze || (rogue.active && rogue.Has(Buff::NO_MINE))) ? 0 : cfg.mineCount;

            if (copyMap)
            {
                // 复刻：格子 / 入口 / 出口全用上一层那份（跳过生成，减速带、地雷、宝物位置一起带过来）
                maze->grid.data = rogueMap.cells;
                maze->entryGx = rogueMap.entryGx;
                maze->entryGy = rogueMap.entryGy;
                maze->exitGx = rogueMap.exitGx;
                maze->exitGy = rogueMap.exitGy;
                rogue.copyNextMap = false;
            }
            else
            {
                maze->generateRecursiveBacktrack();
            }

            // 神圣系（神圣 / 肉鸽第 4~5 层）：玻璃墙 + 可破墙（复刻来的地图本身已经带好了）
            if (effDiff == Difficulty::HOLY && !copyMap)
                maze->SpawnHolyWalls(HOLY_GLASS_WALL_MIN, HOLY_GLASS_WALL_MAX,
                    HOLY_BREAK_WALLS);

            // 第 5 层：所有墙体（可破墙除外）都换成全透明玻璃墙
            if (voidWalls)
            {
                for (int gy = 0; gy < maze->grid.height; ++gy)
                    for (int gx = 0; gx < maze->grid.width; ++gx)
                        if (maze->grid.at(gx, gy) == CellType::WALL)
                            maze->grid.at(gx, gy) = CellType::GLASS_WALL;
            }

            // 增益「拆迁」：地图上所有可破墙直接拆掉
            if (rogue.active && rogue.Has(Buff::BREAK_ALL))
            {
                for (int gy = 0; gy < maze->grid.height; ++gy)
                    for (int gx = 0; gx < maze->grid.width; ++gx)
                        if (maze->grid.at(gx, gy) == CellType::BREAK_WALL)
                            maze->grid.at(gx, gy) = CellType::PATH;
            }
            // 开局自检：难度 + 神圣墙数量（控制台 / VS 输出窗口都能看到）
            {
                int holyGlass = 0;
                int holyBreak = 0;
                for (int gy = 0; gy < maze->grid.height; ++gy)
                    for (int gx = 0; gx < maze->grid.width; ++gx)
                    {
                        const CellType c = maze->grid.at(gx, gy);
                        if (c == CellType::GLASS_WALL)      ++holyGlass;
                        else if (c == CellType::BREAK_WALL) ++holyBreak;
                    }
                TraceLog(LOG_INFO, "run start: difficulty=%d eff=%d floor=%d glass=%d break=%d",
                    (int)currentDifficulty, (int)effDiff, rogue.active ? rogue.floor : -1,
                    holyGlass, holyBreak);
            }
            maze->printConsole();
            rl::terminal::Note("reset maze %dx%d enemies=%d spawn=%d rl=%d",
                maze->grid.width, maze->grid.height, cfg.enemyCount,
                spawnEnemies ? 1 : 0, (rlMode || rlTrainMode) ? 1 : 0);

            // ---- 玩家出生点（增益「随机启程」：从通路格里随机挑一个出生）----
            int spawnGx = maze->entryGx;
            int spawnGy = maze->entryGy;
            if (rogue.active && rogue.Has(Buff::TELEPORT_START))
            {
                std::vector<std::pair<int, int>> pathCells;
                for (int gy = 1; gy < maze->grid.height - 1; ++gy)
                    for (int gx = 1; gx < maze->grid.width - 1; ++gx)
                        if (maze->grid.at(gx, gy) == CellType::PATH)
                            pathCells.emplace_back(gx, gy);
                if (!pathCells.empty())
                {
                    const size_t pick = (size_t)GetRandomValue(0, (int)pathCells.size() - 1);
                    spawnGx = pathCells[pick].first;
                    spawnGy = pathCells[pick].second;
                }
            }
            const Vector3 entryPos = maze->GridToWorld(spawnGx, spawnGy);
            const Vector3 feet = { entryPos.x, GROUND_Y, entryPos.z };

            if (!player)
                player = std::make_unique<Player>(
                    Vector3{ feet.x, feet.y + EYE_HEIGHT, feet.z });

            player->SetFeetPosition(feet);
            player->SetVelocity(Vector3{ 0.0f, 0.0f, 0.0f });
            player->ResetStamina();
            wasSprinting = false;
            wasOnSlowZone = false;
            mineFlashTimer = 0.0f;
            mineShakeTimer = 0.0f;
            mineBlindTimer = 0.0f;
            holyBreakFlashTimer = 0.0f;
            lastFootstepPosValid = false;
            playerFootstepAccum = 0.0f;
            player->flying = false;
            player->LookAt(Vector3{ feet.x, feet.y + EYE_HEIGHT, feet.z + 1.0f });
            modelYaw = 0.0f;
            aimLocked = false;
            tpCam.Reset(feet, 0.0f);          // 相机复位到角色正后方

            // ---- 难度参数 ----
            playerHealth = cfg.playerMaxHealth;
            playerMaxHealth = cfg.playerMaxHealth;   // ← 加这行
            // 饥饿模式 / 肉鸽：能量条换成红色饱腹值条，冲刺能量无限（由饱腹值兜底）
            hunger.enabled = (currentDifficulty == Difficulty::HUNGER) || rogue.active;
            // 双保险：keepMaxHealth <= 0 说明还没有任何可继承的状态（例如开局那一层
            // 被误传了 inheritStats=true），这时宁可当作新的一层从难度默认值开始。
            if (rogue.active && inheritStats && keepMaxHealth > 0)
            {
                playerHealth = keepHealth;
                playerMaxHealth = keepMaxHealth;
                playerAttacksRemaining = keepAttacks;
                score = keepScore;
                hunger.Reset(keepSatiety, keepSatietyMax);
            }
            else if (rogue.active)                     // 肉鸽第一层：100 血 / 满弹药 / 30 饱腹
            {
                playerHealth = ROGUE_START_HEALTH;
                playerMaxHealth = ROGUE_START_HEALTH;
                playerAttacksRemaining = cfg.attackLimit;
                hunger.Reset(ROGUE_START_SATIETY, ROGUE_START_SATIETY);
            }
            else if (hunger.enabled)                   // 饥饿难度
            {
                playerHealth = HUNGER_MAX_HEALTH;
                playerMaxHealth = HUNGER_MAX_HEALTH;
                hunger.Reset(HUNGER_START_SATIETY, HUNGER_START_SATIETY);
            }
            else
            {
                hunger.Reset(0.0f, 1.0f);
            }
            player->infiniteStamina = hunger.enabled || adminMode;
            // ---- 肉鸽增益里直接作用在玩家身上的三个开关（每次开局按已拥有增益重设）----
            player->doubleJumpUnlocked = rogue.active && rogue.Has(Buff::DOUBLE_JUMP);
            player->hoverUnlocked       = rogue.active && rogue.Has(Buff::HOVER);
            player->phaseThroughWalls   = rogue.active && rogue.Has(Buff::PHASE_THROUGH);
            player->sprintDisabled      = rogue.active && rogue.Has(Buff::LOCK_HP);
            if (player->phaseThroughWalls) player->flying = false;   // 穿墙和飞行不叠加
            rogue.hurtSpeedTimer = 0.0f;
            rogue.companionActive = rogue.active && rogue.Has(Buff::COMPANION);
            rogue.companionFireTimer = ROGUE_COMPANION_FIRE_SECONDS;
            rogue.companionFieldTimer = 0.0f;
            if (rogue.companionActive)
                rogue.companionPos = Vector3{ feet.x, GROUND_Y + 0.3f, feet.z };

            // ---- 肉鸽增益：移速 / 饱腹消耗 / 饱腹无限 / 开局暗视 ----
            const float moveMul = (rogue.active && rogue.Has(Buff::FAST_MOVE)) ? 2.0f : 1.0f;
            player->walkSpeed = WALKSPEED * moveMul;
            player->sprintSpeed = SPRINTSPEED * moveMul;
            hunger.drainScale = (rogue.active && rogue.Has(Buff::HUNGER_SLOW)) ? 0.5f : 1.0f;
            hunger.infinite = rogue.active && rogue.Has(Buff::INF_SATIETY);
            rogue.darkFreeTimer = (rogue.active && rogue.Has(Buff::DARK_FREE))
                ? ROGUE_DARK_FREE_SECONDS : 0.0f;
            // 稀有「伙伴」：血上限固定 10（层间继承时同样按这个上限夹住）
            if (rogue.active && rogue.Has(Buff::COMPANION))
                playerMaxHealth = ROGUE_COMPANION_HP_CAP;
            playerHealth = min(playerHealth, playerMaxHealth);
            // 肉鸽层间：弹药 / 分数上面已经还原成上一层的值，不能再被"按难度重置"覆盖
            if (!rogue.active || !inheritStats)
            {
                playerAttacksRemaining = cfg.attackLimit;
                score = 0;
            }
            playerAttackCooldown = 0.0f;
            runAttackLimit = cfg.attackLimit;
            escaped = false;
            overMenuShown = false;

            // v2：RL 模式同样撒宝物。观测里有宝物平面（plane2），训练环境
            // env_vec.py 也一样撒宝物，两边必须完全一致；吃到宝物有 +2.0 奖励。
            // 饥饿模式：宝物 / 弹药包 / 血包都是神圣模式的两倍
            // 饥饿模式宝物 / 弹药包 / 血包全部 x2；肉鸽的三个「翻倍」增益各自再 x2。
            // 每层都按已拥有的增益重新算，所以是"此后每层都生效"。
            const int hungerMul =
                (currentDifficulty == Difficulty::HUNGER) ? HUNGER_TREASURE_SCALE : 1;
            const int goldMul = hungerMul *
                ((rogue.active && rogue.Has(Buff::TREASURE_X2)) ? 2 : 1);
            const int ammoMul = hungerMul *
                ((rogue.active && rogue.Has(Buff::AMMO_X2)) ? 2 : 1);
            const int healMul = hungerMul *
                ((rogue.active && rogue.Has(Buff::HEAL_X2)) ? 2 : 1);
            runTreasureTarget = TREASURE_SCORE_COUNT * goldMul;
            Treasure::Reset(*maze,
                runTreasureTarget,
                TREASURE_ATTACK_COUNT * ammoMul,
                TREASURE_HEAL_COUNT * healMul);

            // ---- 敌人（RL 自动通关 / 训练桥模式不刷敌人：纯迷宫求解）----
            enemyManager = std::make_unique<EnemyManager>(*maze);
            playerSnapshot.feetPosition = player->GetFeetPosition();
            playerSnapshot.bounds = player->GetBoundingBox();
            playerSnapshot.alive = true;
            // 增益「清场」：之后各层都不再刷敌人；「怪少一半」：数量减半（向上取整）
            int enemyCount = (rogue.active && rogue.Has(Buff::NO_ENEMY)) ? 0 : cfg.enemyCount;
            if (rogue.active && rogue.Has(Buff::ENEMY_HALF) && enemyCount > 0)
                enemyCount = (enemyCount + 1) / 2;
            // 增益「一击必杀」：玩家光球命中即秒杀
            enemyManager->orbOneShot = rogue.active && rogue.Has(Buff::STRONG_BULLET);
            if (spawnEnemies)
            {
                enemyManager->BornInitialEnemies(playerSnapshot, enemyCount);
                enemyManager->Start();
            }

            // 肉鸽层间继承：累计用时不能清零，总用时 = 本局所有层的时间之和
            if (inheritStats) timer.Resume();   // 继承：接着上一层的秒数继续跑
            else              timer.Start();    // 新的一局 / 第一层：从 0 开始
            sfxAudio.PlayGameStart();
            sfxAudio.PlayBackground(BgmIndex(effDiff));   // 该难度的背景音乐，循环播放
        };

    // ---- 肉鸽：某个增益在当前局是否生效 ----
    const auto RogueHas = [&](Buff b) { return rogue.active && rogue.Has(b); };

    // ---- 肉鸽：选中瞬间就结算的增益 ----
    const auto ApplyBuffInstant = [&](Buff b)
        {
            switch (b)
            {
            case Buff::MORE_HP:                       // 生命上限 +50 并回满这 50 点
                playerMaxHealth += ROGUE_MORE_HP;
                playerHealth = min(playerMaxHealth, playerHealth + ROGUE_MORE_HP);
                break;
            case Buff::MORE_SCORE:                    // 立刻 +50 分
                score += ROGUE_MORE_SCORE;
                break;
            case Buff::MORE_SATIETY:                  // 饱腹上限 +20 并回等量
                hunger.AddMax(ROGUE_MORE_SATIETY);
                break;
            case Buff::INF_SATIETY:
                hunger.infinite = true;
                break;
            case Buff::HUNGER_SLOW:
                hunger.drainScale = 0.5f;
                break;
            case Buff::COPY_MAP:                      // 存下本层地图，下一层原样复用
                if (maze)
                {
                    rogueMap.valid = true;
                    rogueMap.width = maze->grid.width;
                    rogueMap.height = maze->grid.height;
                    rogueMap.cells = maze->grid.data;
                    rogueMap.entryGx = maze->entryGx;
                    rogueMap.entryGy = maze->entryGy;
                    rogueMap.exitGx = maze->exitGx;
                    rogueMap.exitGy = maze->exitGy;
                    rogue.copyNextMap = true;
                }
                break;
            case Buff::DOUBLE_JUMP:                   // 解锁二段跳
                if (player) player->doubleJumpUnlocked = true;
                break;
            case Buff::LOCK_HP:                       // 锁血：血不再掉，代价是不能跑步
                if (player) player->sprintDisabled = true;
                break;
            case Buff::COMPANION:                     // 血上限锁 10、移速 1/4，伙伴跟着来
                playerMaxHealth = ROGUE_COMPANION_HP_CAP;
                playerHealth = min(playerHealth, playerMaxHealth);
                rogue.companionActive = true;
                rogue.companionFireTimer = ROGUE_COMPANION_FIRE_SECONDS;
                rogue.companionFieldTimer = 0.0f;
                if (player)
                {
                    rogue.companionPos = player->GetFeetPosition();
                    rogue.companionPos.y = GROUND_Y + 0.3f;
                }
                break;
            case Buff::HOVER:                         // 滞空：空中长按空格挂住
                if (player) player->hoverUnlocked = true;
                break;
            case Buff::PHASE_THROUGH:                 // 穿墙开关（回第一层的代价在进层时结算）
                if (player) player->phaseThroughWalls = true;
                break;
            default:
                break;                                // 其余增益在生成下一层 / 每帧逻辑里生效
            }
        };

    // ---- 管理员：任意增益直接到手（暂停菜单 -> 增益选择页用）----
    const auto GrantBuff = [&](Buff b)
        {
            if (rogue.Has(b)) return;
            rogue.owned.push_back(b);
            ApplyBuffInstant(b);
        };

    // ---- 管理员：把增益关掉（数值型能还原的都还原，其余靠 owned 重新推导）----
    const auto RevokeBuff = [&](Buff b)
        {
            for (size_t k = 0; k < rogue.owned.size(); ++k)
            {
                if (rogue.owned[k] == b)
                {
                    rogue.owned.erase(rogue.owned.begin() + (long)k);
                    break;
                }
            }
            switch (b)
            {
            case Buff::MORE_HP:                       // 收回 +50 血上限
                playerMaxHealth = max(1, playerMaxHealth - ROGUE_MORE_HP);
                // 伙伴在场上时血上限固定 10，别因为收回「血量更高」把上限压到 1
                if (rogue.Has(Buff::COMPANION)) playerMaxHealth = ROGUE_COMPANION_HP_CAP;
                playerHealth = min(playerHealth, playerMaxHealth);
                break;
            case Buff::MORE_SCORE:                    // 收回 +50 分
                score = max(0, score - ROGUE_MORE_SCORE);
                break;
            case Buff::MORE_SATIETY:                  // 收回 +20 饱腹上限
                hunger.Reset(max(0.0f, hunger.satiety - ROGUE_MORE_SATIETY),
                             max(1.0f, hunger.satietyMax - ROGUE_MORE_SATIETY));
                break;
            case Buff::DOUBLE_JUMP:
                if (player) player->doubleJumpUnlocked = false;
                break;
            case Buff::LOCK_HP:
                if (player) player->sprintDisabled = false;
                break;
            case Buff::HOVER:
                if (player) player->hoverUnlocked = false;
                break;
            case Buff::PHASE_THROUGH:
                if (player) player->phaseThroughWalls = false;
                if (player) player->flying = false;
                break;
            case Buff::COMPANION:                     // 伙伴撤走，血上限还回基础值（+「血量更高」那 50）
                rogue.companionActive = false;
                playerMaxHealth = ROGUE_START_HEALTH +
                    (rogue.Has(Buff::MORE_HP) ? ROGUE_MORE_HP : 0);
                playerHealth = min(playerHealth, playerMaxHealth);
                break;
            default:
                break;                                // 其余增益都是每帧 / 每层从 owned 推导，删掉即失效
            }
        };

    // ---- 肉鸽：从候选池里随机抽一个（抽走即从池子里删掉，同一窗不会重复）----
    const auto TakeRandomFrom = [](std::vector<Buff>* pool) -> Buff
        {
            const int idx = GetRandomValue(0, (int)pool->size() - 1);
            const Buff b = (*pool)[idx];
            pool->erase(pool->begin() + idx);
            return b;
        };

    // ---- 肉鸽：抽这一窗的候选增益 ----
    // 选项 / 必选：3选2 → 5选3 → 5选3 → 8选4 → 10选5（见 kRogueOptN / kRoguePickN）。
    // 概率：一般 1 / 特殊 1/10 / 稀有 1/20；稀有要花 1 个宝物买，一般和特殊免费。
    // 保底：第 3、4 层必出一个特殊；第 5 层必出一个稀有，并白送 1 个宝物。
    // 已选过的增益不会再出现；刷新只影响候选，不改变保底规则。
    const auto OfferRogueBuffs = [&]()
        {
            rogue.offered.clear();
            rogue.chosen.clear();
            rogue.cursor = 0;

            const int f = (rogue.floor >= 0 && rogue.floor < ROGUE_FLOOR_COUNT)
                ? rogue.floor : (ROGUE_FLOOR_COUNT - 1);
            rogue.pickCount = kRoguePickN[f];
            const int wantN = kRogueOptN[f];

            std::vector<Buff> general, special, rare;
            for (int i = BUFF_GENERAL_FIRST; i <= BUFF_GENERAL_LAST; ++i)
            {
                const Buff b = (Buff)i;
                if (!rogue.Has(b)) general.push_back(b);
            }
            for (int i = BUFF_SPECIAL_FIRST; i <= BUFF_SPECIAL_LAST; ++i)
            {
                const Buff b = (Buff)i;
                if (rogue.Has(b)) continue;
                if (b == Buff::COPY_MAP && rogue.floor == 0) continue;  // 第一层没有可复刻的地图
                special.push_back(b);
            }
            for (int i = BUFF_RARE_FIRST; i <= BUFF_RARE_LAST; ++i)
            {
                const Buff b = (Buff)i;
                if (!rogue.Has(b)) rare.push_back(b);
            }

            // 第 5 层的白送宝物：只在第一次开这一窗时给（刷新不会重复发）
            if (f == ROGUE_RARE_GUARANTEE_FLOOR && !rogue.giftGiven)
            {
                rogue.giftGiven = true;
                rogue.treasureCoins += ROGUE_RARE_GUARANTEE_GIFT;
            }

            // 保底先占位：第 3、4 层一个特殊；第 5 层一个稀有
            if ((f == ROGUE_SPECIAL_GUARANTEE_FLOOR_A || f == ROGUE_SPECIAL_GUARANTEE_FLOOR_B) &&
                !special.empty())
                rogue.offered.push_back(TakeRandomFrom(&special));
            if (f == ROGUE_RARE_GUARANTEE_FLOOR && !rare.empty())
                rogue.offered.push_back(TakeRandomFrom(&rare));

            // 剩下的位置按概率抽：稀有 1/20、特殊 1/10、其余一般
            while ((int)rogue.offered.size() < wantN)
            {
                const float roll = (float)GetRandomValue(0, 9999) / 10000.0f;
                std::vector<Buff>* pool = &general;
                if (roll < ROGUE_RARE_RATE)                            pool = &rare;
                else if (roll < ROGUE_RARE_RATE + ROGUE_SPECIAL_RATE)  pool = &special;

                if (pool->empty())                   // 这一档已经抽空：退回到还有货的档位
                {
                    if (!general.empty())      pool = &general;
                    else if (!special.empty()) pool = &special;
                    else if (!rare.empty())    pool = &rare;
                    else break;                      // 三档全空：这窗就这么几个
                }
                rogue.offered.push_back(TakeRandomFrom(pool));
            }

            // 买不起的稀有增益排在后面，免得挡住能选的一般增益（不改变内容，只改顺序）
            std::stable_sort(rogue.offered.begin(), rogue.offered.end(),
                [&](Buff a, Buff b)
                {
                    const bool aLocked = BuffCost(a) > rogue.treasureCoins;
                    const bool bLocked = BuffCost(b) > rogue.treasureCoins;
                    return aLocked < bLocked;
                });
        };

    // ---- 肉鸽：带着选好的增益开这一层 ----
    // inheritStats：层间要继承血量 / 弹药 / 分数 / 饱腹；第一层从零开始。
    // 注意：这里必须由调用方明确给出，别再拿 rogue.floor 猜——开局那一层
    // 就是 floor 0，猜错就会"继承"一个全新的全 0 状态（血 / 饱腹 / 弹药全 0）。
    const auto StartRogueFloor = [&](bool inheritStats)
        {
            ResetRun(true, inheritStats);
            // 伙伴跟着换层：位置 / 距离场全部重算，
            // 避免拿上一层的旧坐标去寻路 / 开火（上一版就是在这里出的事）
            g_companionDist.clear();
            rogue.companionFieldTimer = 0.0f;
            rogue.companionFireTimer = ROGUE_COMPANION_FIRE_SECONDS;
            if (player != nullptr)
            {
                rogue.companionPos = player->GetFeetPosition();
                rogue.companionPos.y = GROUND_Y + 0.3f;
            }
            DisableCursor();
            state = GameState::PLAYING;
        };

    // ---- RL 开局姿态随机化：roll / pitch / yaw 欧拉角 ----
    // 目的：机器人（以及训练桥）在任意初始朝向下都必须能求解，而不是只在
    // "正对通道"时好使。水平移动由走路层按方向驱动，与朝向无关；随机角度
    // 只影响视角，走路层会把它平滑收回 0（player.LevelOut）。
    std::mt19937 rlRng{ std::random_device{}() };

    const auto RandomizeSpawnOrientation = [&]()
        {
            if (!player) return;
            std::uniform_real_distribution<float> uni01(0.0f, 1.0f);
            const float yawRad = uni01(rlRng) * 2.0f * PI;
            const float pitchRad = (uni01(rlRng) * 2.0f - 1.0f) * PITCH_LIM;
            const float rollRad = (uni01(rlRng) * 2.0f - 1.0f) * PI;
            player->SetOrientation(yawRad, pitchRad, rollRad);
            rl::terminal::Note("spawn euler yaw=%.0f pitch=%.0f roll=%.0f deg",
                yawRad * RAD2DEG, pitchRad * RAD2DEG, rollRad * RAD2DEG);
        };

    // 把真实游戏里才有的信息喂给 RL 智能体（训练时这些同样来自环境）：
    // 敌人当前格（观测 plane4）、玩家真实体力比例、是否正在冲刺。
    const auto SyncRlLiveState = [&]()
        {
            if (!enemyManager || !maze) return;
            // 连续格坐标（世界坐标 / 格边长）：敌人平面按 floor 落格，最近敌人
            // 距离标量用精确值——两边必须和训练环境 spec.encode_obs 完全一样。
            std::vector<float> enemyGrid;
            int alive = 0;
            for (const EnemyRenderState& st : enemyManager->GetRenderStates())
            {
                if (!st.active || st.health <= 0.0f) continue;
                ++alive;
                enemyGrid.push_back(st.position.x / rl::kCellSize);
                enemyGrid.push_back(st.position.z / rl::kCellSize);
            }
            rlAgent.SetEnemyGridPositions(enemyGrid);
            // 活着的敌人数变少 = 光球打死了它（游戏里敌人只有这一个死法）。
            // 但刚 ResetRun 时敌人线程还没 tick，快照会是空的——那种"0 个敌人"绝不能
            // 记成"场上 6 个都被我打死了"（实测开局白送 +6 击杀 +6 奖励，还会让观测的
            // 敌人平面短暂为空，机器人"看不见敌人"）。必须先见过活敌才开始计数。
            if (alive > 0) rlEnemyCountReady = true;
            if (rlEnemyCountReady && rlLastEnemyAlive >= 0 && alive < rlLastEnemyAlive)
                rlAgent.NotifyKill(rlLastEnemyAlive - alive);
            rlLastEnemyAlive = rlEnemyCountReady ? alive : -1;

            if (player)
            {
                rlAgent.SetSprintState(player->GetStaminaRatio(), player->IsSprinting());
                // v3：HP / 弹药以真实游戏为准（RL 不再锁血，被咬真的掉血）
                rlAgent.SetHealth(playerHealth, playerMaxHealth);
                rlAgent.SetAmmo(playerAttacksRemaining);
            }
        };

    // RL 开火：自动瞄准视野内最近的敌人（与训练环境的 FIRE 动作一致）。
    // 打不打得中是游戏的事，击杀奖励由 SyncRlLiveState 的 NotifyKill 补上。
    const auto DoRlFire = [&]() -> bool
        {
            if (!player || !enemyManager || !maze) return false;
            if (playerAttacksRemaining <= 0) return false;
            const Vector3 eye = player->GetPosition();
            const float aimY = player->GetFeetPosition().y + 0.55f;
            float best = -1.0f;
            Vector3 target{ 0.0f, 0.0f, 0.0f };
            for (const EnemyRenderState& st : enemyManager->GetRenderStates())
            {
                if (!st.active || st.health <= 0.0f) continue;
                const Vector3 aim{ st.position.x, aimY, st.position.z };
                const Vector3 d = Vector3Subtract(aim, eye);
                const float dist = sqrtf(d.x * d.x + d.z * d.z);
                if (dist > rl::kFireRange) continue;
                if (best >= 0.0f && dist >= best) continue;
                bool clear = true;
                for (int si = 1; si <= 7 && clear; ++si)
                {
                    const float t = (float)si / 8.0f;
                    if (maze->IsWorldPosBlocked(Vector3{ eye.x + d.x * t,
                                                         eye.y + d.y * t,
                                                         eye.z + d.z * t }))
                        clear = false;
                }
                if (!clear) continue;
                best = dist;
                target = aim;
            }
            if (best < 0.0f) return false;
            player->LookAt(target);
            const Vector3 direction = player->GetForward();
            const Vector3 origin = Vector3Add(player->GetPosition(),
                                              Vector3Scale(direction, 0.45f));
            enemyManager->FireOrb(origin, direction);
            --playerAttacksRemaining;
            playerAttackCooldown = EnemyConfig::ORB_FIRE_COOLDOWN;
            return true;
        };

    GameState loggedState = state;
    while (!WindowShouldClose())
    {
        web::SyncCanvasToWindow();      // Web：让渲染分辨率跟住浏览器窗口，不然画面被拉伸成一片糊（桌面版空实现）

        const float dt = GetFrameTime();

        // 踩雷反馈计时衰减
        mineFlashTimer = max(0.0f, mineFlashTimer - dt);
        mineShakeTimer = max(0.0f, mineShakeTimer - dt);
        mineBlindTimer = max(0.0f, mineBlindTimer - dt);
        holyBreakFlashTimer = max(0.0f, holyBreakFlashTimer - dt);
        laughTimer = max(0.0f, laughTimer - dt);                     // 奶龙大笑（增益「狂笑」用）
        rogue.darkFreeTimer = max(0.0f, rogue.darkFreeTimer - dt);   // 增益「暗视」剩余时间

        // ---- 饥饿 / 肉鸽：饱腹值结算（掉饱腹 -> 耗尽致盲 3s -> 饥饿掉血）----
        if (state == GameState::PLAYING && hunger.enabled && playerHealth > 0 && !escaped)
        {
            const bool sprintingNow = player && player->IsSprinting();
            if (!hunger.infinite)
            {
                const float drainPeriod = (sprintingNow ? HUNGER_SPRINT_DRAIN_SECONDS
                                                        : HUNGER_DRAIN_SECONDS) * hunger.drainScale;
                hunger.satiety = max(0.0f, hunger.satiety - dt / drainPeriod);
            }

            if (hunger.satiety <= 0.0f)
            {
                if (!hunger.starving)          // 刚耗尽：当场致盲 3 秒，转成饥饿掉血态
                {
                    hunger.starving = true;
                    hunger.starveTick = 0.0f;
                    hunger.blindTimer = max(hunger.blindTimer, HUNGER_BLIND_SECONDS);
                }

                const float hurtPeriod = sprintingNow ? HUNGER_STARVE_SPRINT_HP_SECONDS
                                                     : HUNGER_STARVE_HP_SECONDS;
                hunger.starveTick += dt;
                while (hunger.starveTick >= hurtPeriod && playerHealth > 0)
                {
                    hunger.starveTick -= hurtPeriod;
                    --playerHealth;                        // 饿到掉血：4s -1（冲刺 2s -1）
                    if (playerHealth > 0) sfxAudio.PlayPlayerDamage();
                    else                  sfxAudio.PlayPlayerDeath();
                }
            }
            else
            {
                hunger.starving = false;                    // 补给回饱腹后退出掉血态
                hunger.starveTick = 0.0f;
            }
        }
        hunger.blindTimer = max(0.0f, hunger.blindTimer - dt);

        // 背景音乐：只在游戏进行中播放；暂停 / 菜单 / 排名 / 结算都挂起
        if (state == GameState::PLAYING && !overMenuShown)
        {
            sfxAudio.ResumeBackground();
        }
        else
        {
            sfxAudio.PauseBackground();
        }
        sfxAudio.UpdateBackground();
        MenuAction action = MenuAction::NONE;

        // 非游戏状态不播放脚步，并清空后台可能堆积的事件，避免恢复游戏时集中播放。
        if (state != GameState::PLAYING)
        {
            footstepAudio.StopAll();
            // 只停止游戏内音效，保留菜单 UI 点击音效自然播完。
            sfxAudio.StopGameplaySounds();
            if (enemyManager != nullptr)
            {
                enemyManager->ConsumeFootstepEvents();
            }
        }

        if (state != loggedState)
        {
            rl::terminal::Note("state %s -> %s mode=%s",
                GameStateName(loggedState), GameStateName(state),
                rlMode ? "auto" : (rlTrainMode ? "train"
                    : (adminMode ? "admin" : "play")));
            loggedState = state;
        }

    // ---- 一局 RL 会话的完整初始化 ----
    // 真实体力 / 满血 / 权重与迷宫绑定 / 路线回放 / 走路层复位 / 随机开局姿态。
    // 进入自动模式（RL_LOGIN）和局间按 R 重开都走这里：否则 rlAgent 与 rlWalker
    // 会带着上一局的格子、步数和目标格继续走，机器人只会原地打转（stall 死循环）。
    // ---- RL 门禁：只允许「简单 / 普通 / 困难」+ 正常模式第一人称 ----
    const auto RlEntryAllowed = [&]()
        {
            return gameMode == GameMode::NORMAL && !thirdPerson &&
                   IsRlSupportedDifficulty(EffectiveDifficulty());
        };

    const auto BeginRlEpisode = [&]()
        {
            if (player)
            {
                player->flyEnabled = false;
                player->flying = false;
                // v2：冲刺用真实体力（5 s 冲刺 / 15 s 回满），与真实玩家一致
                player->infiniteStamina = false;
            }
            playerHealth = playerMaxHealth;
            rlSprint = false;
            rlHalted = false;
            rlFinishing = false;

            if (EnsureRlWeights())
            {
                rlAgent.SetNet(&rlNet);
                rlAgent.BindMaze(*maze);
                if (rlStatus.find("自动通关中") == std::string::npos) rlStatus += "  自动通关中";
            }
            else
            {
                rlAgent.SetNet(nullptr);          // 没权重就退回距离贪心
                rlAgent.BindMaze(*maze);
                if (rlStatus.find("退回距离贪心") == std::string::npos) rlStatus += "  退回距离贪心";
            }
            // 路线回放：把迷宫 + 起点交给遥测层，之后每一步都会记一格
            rl::terminal::SetRouteDumpDir(RlRouteDir().c_str());
            rl::terminal::RouteBegin(rlAgent.Field().Rows(),
                rlAgent.Gx(), rlAgent.Gy(), maze->exitGx, maze->exitGy);
            rlWalker.Reset(*maze, rlAgent.Gx(), rlAgent.Gy());
            rlWalkerStall = 0.0f;
            RandomizeSpawnOrientation();
            rlLastAction = -1;

            // 本局会话信息导出到终端（之后每一步也会打一行）
            rl::terminal::SetMode("auto");
            rl::terminal::Session("auto", maze->grid.width, maze->grid.height,
                maze->entryGx, maze->entryGy, maze->exitGx, maze->exitGy,
                rlAgent.OptimalSteps(), rlAgent.MaxSteps(),
                rlNet.Ready() ? 1 : 0);
            rlLastEnemyAlive = -1;
            rlEnemyCountReady = false;
            rlLastReward = 0.0f;
            rlSteps = 0;
            rlEpisodeReturn = 0.0;
        };

    // 通关收尾：等机器人真正走进出口格之后再调用（见下面的 rlFinishing 分支）。
    const auto FinishRlEpisode = [&]()
        {
            rlFinishing = false;
            rlHalted = true;
            rlStatus = "RL: 已通关，按 R 重开";
            const double rlSps = (rlAgent.Seconds() > 0.0)
                ? (double)rlSteps / rlAgent.Seconds() : 0.0;
            rl::terminal::EpisodeEnd((int)rlEpisodeCount, rlSteps,
                rlAgent.OptimalSteps(), rlEpisodeReturn, true, rlSps);
            rl::terminal::Note("episode totals %s", rlAgent.Describe().c_str());
            rl::terminal::Halt("reached-exit", rlSteps,
                rlAgent.MaxSteps(), rlAgent.Gx(), rlAgent.Gy(), 0);
            rlEpisodeReturn = 0.0;
            rlSprint = false;
            ReportRlRoute(true);
        };

        // ---- 1. 状态更新 ----
        switch (state)
        {
        case GameState::MENU:
            action = mainMenu.Update();
            if (IsKeyPressed(KEY_ESCAPE))                        // 改成先弹确认框
            {
                confirmExitMenu.Reset();
                state = GameState::CONFIRM_EXIT;
            }
            break;

        case GameState::HELP:
            action = helpMenu.Update();
            if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
            break;

        case GameState::DIFFICULTY:
            action = difficultyMenu.Update();
            if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
            break;

        case GameState::LOAD_MENU:                        // ← 加这段
            action = loadMenu.Update();
            if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
            break;

        case GameState::LOGIN:
        {
            const TextPageLayout layout = MakeTextPageLayout(true);
            const bool enter = nameInput.Update(RANK_NAME_MAX_CHARS);

            if (enter || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(GetMousePosition(), layout.btnA)))
            {
                sfxAudio.PlayUiClick();
                if (nameInput.IsEmpty())
                {
                    loginError = true;
                }
                else
                {
                    playerName = nameInput.GetText();
                    EnsureGlyphs(playerName);
                    loginError = false;
                    modeMenu.Reset();
                    state = GameState::MODE_SELECT;
                }
            }
            else if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                     CheckCollisionPointRec(GetMousePosition(), layout.btnB))
            {
                sfxAudio.PlayUiClick();
                playerName = Utf8("游客");
                loginError = false;
                modeMenu.Reset();
                state = GameState::MODE_SELECT;
            }

            if (IsKeyPressed(KEY_ESCAPE))
            {
                sfxAudio.PlayUiClick();
                mainMenu.Reset();
                state = GameState::MENU;
            }
            break;
        }

        case GameState::MODE_SELECT:                      // 模式选择页
            action = modeMenu.Update();
            if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
            break;

        case GameState::RANKING:
        {
            const RankLayout layout = MakeRankLayout();
            const Vector2 mouse = GetMousePosition();

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                // 先检查排行榜类型切换。
                for (int i = 0; i < 2; ++i)
                {
                    if (CheckCollisionPointRec(mouse, layout.boardTabs[i]))
                    {
                        sfxAudio.PlayUiClick();
                        rankView = (i == 0) ? RankView::Score : RankView::Time;
                        LoadRank(rankTab);
                    }
                }

                // 再检查难度切换。
                for (int i = 0; i < 7; ++i)
                {
                    if (CheckCollisionPointRec(mouse, layout.tabs[i]))
                    {
                        sfxAudio.PlayUiClick();
                        LoadRank(i);
                    }
                }

                if (CheckCollisionPointRec(mouse, layout.back)) action = MenuAction::BACK;
            }

            if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
            break;
        }

        case GameState::DIFF_INFO:
            action = diffInfoMenu.Update();
            if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
            break;

        case GameState::ROGUE_REWARD:               // 肉鸽：层间增益选择
        {
            const int count = (int)rogue.offered.size();
            if (count <= 0)                          // 兜底：没得选就直接开打
            {
                StartRogueFloor(rogue.floor > 0);
                break;
            }

            if (IsKeyPressed(KEY_ESCAPE))            // ESC：放弃本局回主菜单（通关前不计成绩）
            {
                sfxAudio.PlayUiClick();
                rogue.Clear();
                rogueMap.valid = false;
                hunger.enabled = false;
                hunger.infinite = false;
                hunger.drainScale = 1.0f;
                hunger.Reset(0.0f, 1.0f);
                laughTimer = 0.0f;
                if (player)
                {
                    player->walkSpeed = WALKSPEED;
                    player->sprintSpeed = SPRINTSPEED;
                    player->infiniteStamina = false;
                }
                timer.Reset();
                sfxAudio.StopBackground();
                mainMenu.Reset();
                EnableCursor();
                state = GameState::MENU;
                break;
            }

            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
            {
                rogue.cursor = (rogue.cursor + 1) % count;
                sfxAudio.PlayUiClick();
            }
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
            {
                rogue.cursor = (rogue.cursor + count - 1) % count;
                sfxAudio.PlayUiClick();
            }
            // 鼠标：悬停到哪一行就选中哪一行
            for (int i = 0; i < count; ++i)
                if (CheckCollisionPointRec(GetMousePosition(), RogueOfferRow(i)))
                    rogue.cursor = i;

            // 刷新：全局只有 ROGUE_REFRESH_TIMES 次（R 键或右下角按钮），用完就没了
            const Rectangle refreshBtn = RogueRefreshRect();
            bool refreshClicked = false;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                CheckCollisionPointRec(GetMousePosition(), refreshBtn))
                refreshClicked = true;

            // 点刷新按钮：本帧就到此为止（不当作"选了某一行"）
            if (refreshClicked || IsKeyPressed(KEY_R))
            {
                if (rogue.refreshLeft > 0)
                {
                    --rogue.refreshLeft;
                    sfxAudio.PlayUiClick();
                    OfferRogueBuffs();               // 重抽候选（保底规则照旧，白送宝物不重复发）
                }
                else
                {
                    sfxAudio.PlayUiClick();          // 没次数了：只给个反馈，不刷新
                }
                break;
            }

            const bool confirm = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

            if (confirm)
            {
                bool already = false;
                for (int c : rogue.chosen) if (c == rogue.cursor) already = true;
                if (already)
                {
                    for (size_t k = 0; k < rogue.chosen.size(); ++k)
                        if (rogue.chosen[k] == rogue.cursor)
                        {
                            rogue.chosen.erase(rogue.chosen.begin() + (long)k);
                            break;
                        }
                    sfxAudio.PlayUiClick();
                }
                else if ((int)rogue.chosen.size() < rogue.pickCount)
                {
                    // 稀有增益要花宝物：买不起就勾不上（弹窗里那行会显示灰字）
                    if (BuffCost(rogue.offered[rogue.cursor]) > rogue.treasureCoins)
                    {
                        sfxAudio.PlayUiClick();
                    }
                    else
                    {
                        rogue.chosen.push_back(rogue.cursor);
                        sfxAudio.PlayUiClick();
                    }
                }
                else
                {
                    sfxAudio.PlayUiClick();
                }

                if ((int)rogue.chosen.size() >= rogue.pickCount)
                {
                    bool boughtPhase = false;
                    bool boughtSkip = false;
                    for (int idx : rogue.chosen)
                    {
                        const Buff b = rogue.offered[idx];
                        rogue.treasureCoins -= BuffCost(b);   // 稀有扣宝物，一般 / 特殊免费
                        rogue.owned.push_back(b);             // 本局剩余各层都生效
                        if (b == Buff::PHASE_THROUGH) boughtPhase = true;
                        if (b == Buff::SKIP_FLOOR)    boughtSkip = true;
                        ApplyBuffInstant(b);                  // 立刻生效型（+血 / +分 / +饱腹 / 复刻地图 …）
                    }
                    rogue.offered.clear();
                    rogue.chosen.clear();

                    // 这个窗口就是"即将进入的那一层"，所以基准就是 rogue.floor 本身
                    // （层号前进已经在"通关那一帧"做过了，这里再加一就会少打一层）。
                    // 穿墙的代价是回到第一层重新开始；跳层再多跳一层并罚时。
                    int nextFloor = rogue.floor;
                    if (boughtPhase) nextFloor = 0;
                    if (boughtSkip)
                    {
                        timer.AddElapsed(ROGUE_SKIP_FLOOR_PENALTY);
                        ++nextFloor;
                    }

                    if (nextFloor >= ROGUE_FLOOR_COUNT)
                    {
                        // 跳层把最后一层也跳掉了：这一局直接算通关（下一帧走正常结算 / 写 log）
                        sfxAudio.PlayGameClear();
                        rogue.floor = ROGUE_FLOOR_COUNT - 1;
                        escaped = true;
                        DisableCursor();
                        state = GameState::PLAYING;
                    }
                    else
                    {
                        rogue.floor = nextFloor;
                        // 穿墙回第一层也要继承（代价只是"层数倒退"）；第一层才从零开始
                        StartRogueFloor(nextFloor > 0 || boughtPhase);
                    }
                }
            }
            break;
        }

        case GameState::RL_LOGIN:
        {
            if (rlPasswordInput.Update(16))
            {
                if (rlPasswordInput.GetText() == RL_PASSWORD)
                {
                    rlPasswordError = false;
                    rlPasswordInput.Clear();
                    rlUnlocked = true;

                    // 保险：难度 / 视角不合格就别开（例如登录途中被改掉）
                    if (!RlEntryAllowed())
                    {
                        rlStatus = "RL: 仅支持 简单/普通/困难 的第一人称视角";
                        rlPasswordInput.Clear();
                        DisableCursor();
                        state = GameState::PLAYING;
                        break;
                    }

                    // 开一局干净的：不刷敌人，满血，计时从零
                    adminMode = true;
                    rlMode = true;
                    rlTrainMode = false;
                    ResetRun(true);              // RL 自动通关：要有敌人和宝物
                    BeginRlEpisode();
                    timer.Start();

                    // RL 模式由机器人接管，鼠标保持锁定（避免视角失灵）
                    DisableCursor();
                    state = GameState::PLAYING;
                }
                else
                {
                    rlPasswordError = true;
                    rlPasswordInput.Clear();
                }
            }

            if (IsKeyPressed(KEY_ESCAPE))
            {
                rlPasswordInput.Clear();
                rlPasswordError = false;
                DisableCursor();
                state = GameState::PLAYING;
            }
            break;
        }

        case GameState::ADMIN_LOGIN:
            if (passwordInput.Update(16))
            {
                sfxAudio.PlayUiClick();
                if (passwordInput.GetText() == ADMIN_PASSWORD)
                {
                    passwordError = false;
                    passwordInput.Clear();
                    adminMode = true;
                    timer.Resume();
                    DisableCursor();
                    state = GameState::PLAYING;
                }
                else
                {
                    passwordError = true;
                    passwordInput.Clear();
                }
            }

            if (IsKeyPressed(KEY_ESCAPE))
            {
                sfxAudio.PlayUiClick();
                passwordInput.Clear();
                passwordError = false;
                pauseMenu.Reset();
                state = GameState::PAUSED;
            }
            break;

        case GameState::ADMIN_BUFFS:                // 管理员：增益选择页
        {
            const int total = BUFF_ALL_COUNT;
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
            {
                adminBuffCursor = (adminBuffCursor + 1) % total;
                sfxAudio.PlayUiClick();
            }
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W))
            {
                adminBuffCursor = (adminBuffCursor + total - 1) % total;
                sfxAudio.PlayUiClick();
            }
            if (IsKeyPressed(KEY_PAGE_DOWN))
                adminBuffCursor = min(total - 1, adminBuffCursor + ADMIN_BUFF_ROWS);
            if (IsKeyPressed(KEY_PAGE_UP))
                adminBuffCursor = max(0, adminBuffCursor - ADMIN_BUFF_ROWS);

            const int scroll = AdminBuffScroll(adminBuffCursor);
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            {
                for (int i = scroll; i < min(total, scroll + ADMIN_BUFF_ROWS); ++i)
                {
                    if (CheckCollisionPointRec(GetMousePosition(), AdminBuffRow(i, scroll)))
                    {
                        adminBuffCursor = i;
                        const Buff picked = (Buff)i;
                        if (rogue.Has(picked)) RevokeBuff(picked);
                        else                   GrantBuff(picked);
                    }
                }
            }

            if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))
            {
                const Buff picked = (Buff)adminBuffCursor;
                if (rogue.Has(picked)) RevokeBuff(picked);   // 再按一次 = 关掉
                else                   GrantBuff(picked);    // 选中即开启
                sfxAudio.PlayUiClick();
            }

            if (IsKeyPressed(KEY_ESCAPE))
            {
                sfxAudio.PlayUiClick();
                pauseMenu.Reset();
                state = GameState::PAUSED;
            }
            break;
        }


        case GameState::PLAYING:
        {
            const bool alive = playerHealth > 0;
            const bool finished = escaped || !alive;
            int mineDamagePending = 0;   // 本帧地雷伤害，和敌人伤害一起结算
            bool mineHitThisFrame = false;   // 本帧是否踩到地雷（用来屏蔽普通受伤音）
            // 地狱模式：取消加速跑 / 跳跃音效，做安静压抑的恐怖氛围
            // 地狱 / 神圣都走地狱那一套：恐怖氛围、不能跳 / 不能冲刺
            const bool horrorMode = IsHellLike(EffectiveDifficulty());

            if (finished)
            {
                wasSprinting = false;
            }

            // 推进受伤音效等短音效的冷却计时
            sfxAudio.Update(dt);

            // RL 只在正常模式可用：奶龙模式下一律关掉 RL（入口也不显示）
            if (gameMode != GameMode::NORMAL && (rlMode || rlTrainMode))
            {
                rlMode = false;
                rlTrainMode = false;
            }

            // 管理员模式：能量无限 + 长按 Q 起飞（Space 永远只负责跳跃）
            // 只有纯管理员模式才无限冲刺；RL（自动通关 / 训练）必须消耗真实体力
            // 管理员：能量无限；饥饿 / 肉鸽：整条能量换成饱腹值，冲刺同样不受能量限制
            player->infiniteStamina =
                (adminMode && !rlMode && !rlTrainMode) || hunger.enabled;
            player->flyEnabled = adminMode && !rlMode && !rlTrainMode;

            // RL 的车道偏移（+-0.68 m）与咬合判定都按「玩家半宽 0.30 m」设计
            // （RLSpec.h / spec.py 同一套几何）。普通模式的 HALFWIDTH = 0.64 是给
            // 角色模型用的碰撞代理：半宽大一倍，1 格走廊里侧向只剩 0.36 m 可用，
            // 车道目标根本够不到，于是每换一次道就卡死 1.5 s 触发重居中。
            // RL 里换成与训练模型一致的半宽，步频才追得上 MDP 的 0.5 s / 0.25 s。
            player->halfWidth = (rlMode || rlTrainMode)
                ? rl::kPlayerHalfWidth : HALFWIDTH;

            // 管理员模式：血量 / 子弹 / 饱腹 / 宝物 全部无限
            // （RL 自动通关 / 训练器除外：那两个模式要拿真实数值喂网络）
            if (adminMode && rogue.active && !rlMode && !rlTrainMode)
            {
                playerHealth = playerMaxHealth;                  // 血量锁满
                playerAttacksRemaining = runAttackLimit;         // 子弹不消耗
                hunger.infinite = true;                          // 饱腹不掉
                if (rogue.active && rogue.treasureCoins < ROGUE_ADMIN_COINS)
                    rogue.treasureCoins = ROGUE_ADMIN_COINS;     // 宝物币无限
            }

            // 注意：ADMIN / RL 模式下不要在这里 EnableCursor()。
            // 以前是为了点右下角那两个浮动按钮才一直解锁鼠标，导致视角转不动。
            // 现在入口在 ESC 暂停菜单里，游戏中鼠标保持锁定，F1 / F2 仍可当快捷键。
            // F3：RL 终端日志开关（自动通关 / 训练桥都能用）
            if (IsKeyPressed(KEY_F3)) rl::terminal::Toggle();

            if (adminMode && gameMode == GameMode::NORMAL &&
                !rlMode && !rlTrainMode && state == GameState::PLAYING)
            {
                if (IsKeyPressed(KEY_F1))
                {
                    if (!RlEntryAllowed())
                    {
                        rlStatus = "RL: 仅支持 简单/普通/困难 的第一人称视角";
                    }
                    else
                    {
                        rlPasswordInput.Clear();
                        rlPasswordError = false;
                        EnableCursor();
                        state = GameState::RL_LOGIN;
                    }
                }
                else if (IsKeyPressed(KEY_F2))
                {
                    EnsureRlWeights();
                    rl::terminal::SetMode("bridge");
                    if (rlBridge.Start())
                    {
                        rlTrainMode = true;
                        rlMode = false;
                        ResetRun(false);
                        RandomizeSpawnOrientation();
                        if (player)
                        {
                            player->flyEnabled = false;
                            player->flying = false;
                        }
                        rlBridge.PublishMaze(*maze);
                        rlStatus = "RL: 训练桥 listening 127.0.0.1:45871";
                    }
                    else
                    {
                        rlStatus = "RL: 训练桥启动失败（端口被占用？）";
                    }
                }
            }

            // 本帧玩家是否在水平移动（脚步声 / 喘气 / 减速带发黏声都要求移动中）
            const Vector3 playerVelocity = player->GetVelocity();
            const bool playerMoving = player->IsGrounded() && !player->flying &&
                (playerVelocity.x * playerVelocity.x + playerVelocity.z * playerVelocity.z) > 0.01f;

            // ---- 肉鸽增益：每帧重算移速（疾行 x2 / 伙伴 x1/4 / 受伤提速 x4，可叠乘）----
            if (rogue.active && player)
            {
                rogue.hurtSpeedTimer = max(0.0f, rogue.hurtSpeedTimer - dt);
                float moveMul = 1.0f;
                if (RogueHas(Buff::FAST_MOVE))   moveMul *= 2.0f;
                if (RogueHas(Buff::COMPANION))   moveMul /= ROGUE_COMPANION_SPEED_DIV;
                if (rogue.hurtSpeedTimer > 0.0f) moveMul *= ROGUE_HURT_SPEED_MUL;
                player->walkSpeed = WALKSPEED * moveMul;
                player->sprintSpeed = SPRINTSPEED * moveMul;
            }

            if (!finished)
            {
                const bool jumpPressed = IsKeyPressed(KEY_SPACE);
                const bool wasGrounded = player->IsGrounded();
                const bool wasFlying = player->flying;
                const float fallSpeedBefore = player->GetVelocity().y;   // 落地前竖直速度（下落为负）
                if (rlMode && rlAgent.Ready() && !rlHalted)
                {
                    // 走到当前目标格中心 -> 网络决策 -> 换下一个目标格
                    if (!rlWalker.HasTarget())
                        rlWalker.SetTarget(rlAgent.Gx(), rlAgent.Gy());

                    // 每帧同步"只有真实游戏才知道"的信息：敌人当前格、体力比例、
                    // 是否正在冲刺（观测的敌人平面 / 体力 / 冲刺三个输入）。
                    SyncRlLiveState();

                    if (rlWalker.Drive(*player, *maze, dt, rlSprint))
                    {
                        // 通关最后一段：agent 在踏进出口格那一刻就算成功，而机器人
                        // 还停在前一格里。先把这一脚真正走完（走到出口格中心），再收尾，
                        // 否则它会停在出口前一格就退出 RL 模式。
                        if (rlFinishing)
                        {
                            FinishRlEpisode();
                        }
                        else
                        {
                            rlWalkerStall = 0.0f;

                            float value = 0.0f;
                            const int act = rlAgent.PolicyAction(&value);
                            rlLastAction = act;
                            rlSprint = rl::ActSprint(act);   // 这一步是否按住 Shift
                            if (rl::ActIsFire(act))
                            {
                                // 真开一枪；打空也占满一个决策周期（0.5 s），
                                // 与训练环境 spec.FIRE_STEP_TIME 的记账一致。
                                if (!DoRlFire()) rl::terminal::Note("fire: no target");
                                rlWalker.HoldFor(rl::kFireStepTime);
                            }
                            bool rlIllegal = false;
                            rlAgent.ApplyAction(act, &rlIllegal);
                            // 撞墙这一步在训练里也要花掉整整一个决策周期（0.5 / 0.25 s），
                            // 游戏里同样停下来等，别让机器人"贴墙蹭时间"
                            if (rlIllegal) rlWalker.HoldFor(rlAgent.LastDt());
                            rlLastReward = rlAgent.LastReward();
                            rlEpisodeReturn += rlLastReward;
                            rlSteps = rlAgent.Steps();
                            if (rl::ActIsFire(act))
                            {
                                rlWalker.SetTarget(rlAgent.Gx(), rlAgent.Gy());
                            }
                            else
                            {
                                // 车道偏移：贴着通道一侧走，别一直压中线（才可能侧身绕过敌人）
                                const int laneDir = rl::ActDir(act);
                                const float laneOff = rl::ActLaneOffset(act);
                                rlWalker.SetTarget(rlAgent.Gx(), rlAgent.Gy(),
                                    (float)rl::LanePerpX(laneDir) * laneOff,
                                    (float)rl::LanePerpY(laneDir) * laneOff);
                            }

                            // 每一步都导出到终端（动作 / 格子 / 距出口距离 / 奖励）
                            rl::terminal::Step((int)rlEpisodeCount + 1, rlSteps,
                                rlAgent.MaxSteps(), act, rlAgent.Gx(), rlAgent.Gy(),
                                rlAgent.Field().At(rlAgent.Gx(), rlAgent.Gy()),
                                rlLastReward, rlEpisodeReturn,
                                rlAgent.Done(), rlAgent.Success(),
                                rlAgent.TreasuresCollected(), rlAgent.EnemyHits(),
                                rlAgent.Seconds());

                            // 路线回放：记下这一步落在哪一格（撞墙原地也记，便于看偏离）
                            rl::terminal::RoutePush(rlAgent.Gx(), rlAgent.Gy());

                            if (rlAgent.Done())
                            {
                                ++rlEpisodeCount;
                                rlReturnSum += rlEpisodeReturn;

                                if (rlAgent.Died())
                                {
                                    rlHalted = true;          // 缺血阵亡：停下
                                    rlStatus = "RL: 被敌人击杀（HP 归零），按 F1 重开";
                                    rl::terminal::Halt("killed-by-enemy", rlSteps,
                                        rlAgent.MaxSteps(), rlAgent.Gx(), rlAgent.Gy(),
                                        rlAgent.Field().At(rlAgent.Gx(), rlAgent.Gy()));
                                }
                                else if (!rlAgent.Success())
                                {
                                    rlHalted = true;          // 超时：停下，别乱走
                                    rlStatus = "RL: 本局超时，按 F1 重开";
                                    rl::terminal::Halt("timeout", rlSteps,
                                        rlAgent.MaxSteps(), rlAgent.Gx(), rlAgent.Gy(),
                                        rlAgent.Field().At(rlAgent.Gx(), rlAgent.Gy()));
                                }
                                else
                                {
                                    // 通关：agent 踏进出口格就算成功，但机器人还在前一格里。
                                    // 这里只登记，交给上面的 rlFinishing 分支把最后一段真正
                                    // 走完（走到出口格中心）再收尾，否则它会停在离出口一格
                                    // 的地方提前结束 RL。
                                    rlFinishing = true;
                                    rlStatus = "RL: 正在走进出口...";
                                }
                                if (!rlFinishing)
                                {
                                    rlEpisodeReturn = 0.0;
                                    rlSprint = false;
                                    ReportRlRoute(rlAgent.Success());
                                }
                            }
                        }
                    }
                    else
                    {
                        // 走不到目标格（撞墙 / 被卡住）：每 2 秒打一行，直接看出卡在哪
                        rlWalkerStall += dt;
                        if (rlFinishing && rlWalkerStall >= 3.0f)
                        {
                            // 最后一段卡住了：直接把机器人放进出口格中心，保证游戏判定"已逃离"。
                            rlWalker.SetTarget(rlAgent.Gx(), rlAgent.Gy());
                            rlWalker.PlaceAt(*player, *maze, rlAgent.Gx(), rlAgent.Gy());
                            rlWalkerStall = 0.0f;
                            rl::terminal::Note("finish: snapped into exit cell");
                        }
                        else if (rlWalkerStall >= 2.0f)
                        {
                            int pgx = 0, pgy = 0;
                            maze->WorldToGrid(player->GetFeetPosition(), pgx, pgy);
                            rl::terminal::WalkerStall(rlWalker.TargetGx(), rlWalker.TargetGy(),
                                pgx, pgy, rlWalkerStall);
                            rlWalkerStall = 0.0f;
                        }
                    }
                }
                else if (rlTrainMode)
                {
                    player->UpdateAutoMove(dt, *maze, Vector3{ 0.0f, 0.0f, 0.0f });
                }
                else
                {
                    player->Update(dt, *maze);
                }

                // 从地面起跳时播放一次跳跃音效；管理员飞行不算跳跃，减速带禁跳不播
                // 地狱模式刻意不放跳跃音，安静的氛围更压抑
                if (!horrorMode && jumpPressed && wasGrounded && !wasFlying &&
                    !player->flying && !player->IsOnSlowZone())
                {
                    sfxAudio.PlayPlayerJump();
                    // 奶龙叫只在奶龙模式响，正常模式保持原版跳跃音
                    if (gameMode == GameMode::NAILONG) sfxAudio.PlayNailongChirp();   // 奶龙起跳时叫一声
                }

                // 落地音效：按落地前的竖直速度决定轻重
                if (!wasGrounded && player->IsGrounded() &&
                    fallSpeedBefore <= -PLAYER_LAND_MIN_FALL_SPEED)
                {
                    sfxAudio.PlayPlayerLand(fallSpeedBefore);
                }

                // 玩家脚步声：按实际移动距离累计，走/跑步幅不同；减速带上换成发黏的脚步
                {
                    const Vector3 feetNow = player->GetFeetPosition();
                    if (playerMoving)
                    {
                        if (lastFootstepPosValid)
                        {
                            const float stepDx = feetNow.x - lastFootstepPos.x;
                            const float stepDz = feetNow.z - lastFootstepPos.z;
                            playerFootstepAccum += sqrtf(stepDx * stepDx + stepDz * stepDz);
                        }

                        const float stride = player->IsSprinting()
                            ? PLAYER_SPRINT_STRIDE : PLAYER_WALK_STRIDE;
                        const bool onSlowZone = player->IsOnSlowZone();
                        if (stride > 0.01f)
                        {
                            while (playerFootstepAccum >= stride)
                            {
                                playerFootstepAccum -= stride;
                                // 减速带上的发黏声交给循环播放，这里只处理普通脚步
                                if (!onSlowZone)
                                {
                                    sfxAudio.PlayPlayerFootstep();
                                }
                            }
                        }
                    }
                    else
                    {
                        // 停步（或离地）时立刻收住正在播放的脚步，避免停下来还拖着一拍
                        playerFootstepAccum = 0.0f;
                        sfxAudio.StopPlayerFootsteps();
                    }

                    lastFootstepPos = feetNow;
                    lastFootstepPosValid = true;
                }


                // 进入减速带的瞬间播放一次提示音，持续踩在上面不重复播放
                const bool onSlowZoneNow = player->IsOnSlowZone();
                if (onSlowZoneNow && !wasOnSlowZone)
                {
                    sfxAudio.PlaySlowZoneEnter();
                }
                wasOnSlowZone = onSlowZoneNow;

                // 加速跑音效：从普通移动切到加速跑时播放一次，持续加速不重复播放
                const bool sprintingNow = player->IsSprinting();
                if (!horrorMode && sprintingNow && !wasSprinting)
                {
                    sfxAudio.PlaySprintStart();
                }
                wasSprinting = sprintingNow;
                // 增益「计时减慢」：计时速度 -50%（1 秒只记 0.5 秒，排行榜按这个算）
                timer.Update(dt * (RogueHas(Buff::SLOW_TIMER) ? ROGUE_TIMER_SLOW_SCALE : 1.0f));

                playerAttackCooldown = max(0.0f, playerAttackCooldown - dt);
                bool  firedThisFrame = false;   // 本帧是否开火（喂给奶龙的 Shoot 动画）
                float fireYaw = modelYaw;       // 本帧朝哪打（开火时角色转向目标）

                // 神圣模式：视线里的可破墙也能打（一发子弹打碎；玻璃墙挡路但打不碎）
                int holyAimGx = -1;
                int holyAimGy = -1;
                float holyAimDist = 0.0f;
                const Vector3 holyRayDir =
                    thirdPerson ? tpCam.GetAimDir() : player->GetForward();
                const bool holyBreakAimed = FindBreakWallInSight(*maze,
                    player->GetPosition(), holyRayDir, HOLY_BREAK_SHOOT_RANGE,
                    holyAimGx, holyAimGy, holyAimDist);

                // 先判墙：墙挡在前面时这一发打给墙，打完就进冷却，
                // 下面那段索敌开火本帧自然不会再多打一枪。
                if (!rlMode && !rlTrainMode && holyBreakAimed &&
                    IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                    playerAttacksRemaining > 0 &&
                    playerAttackCooldown <= 0.0f)
                {
                    const Vector3 origin = player->GetPosition();
                    const Vector3 wallPos = maze->GridToWorld(holyAimGx, holyAimGy);
                    const Vector3 toward = { wallPos.x, origin.y, wallPos.z };
                    const Vector3 direction =
                        Vector3Normalize(Vector3Subtract(toward, origin));

                    enemyManager->FireOrb(origin, direction);   // 光球当视觉反馈
                    sfxAudio.PlayOrbFire();
                    sfxAudio.PlayMineExplode();
                    maze->BreakWall(holyAimGx, holyAimGy);      // 那一格当场变通路

                    holyBreakFlashPos = Vector3{ wallPos.x,
                                                 WALL_HEIGHT * 0.5f, wallPos.z };
                    holyBreakFlashTimer = HOLY_BREAK_FLASH_SECONDS;
                    firedThisFrame = true;
                    fireYaw = atan2f(direction.x, direction.z);
                    --playerAttacksRemaining;
                    playerAttackCooldown = EnemyConfig::ORB_FIRE_COOLDOWN;
                }

                // 自动索敌：锁定范围内最近、有视线的敌人（索敌标记和开火都用它）
                const Camera3D aimCam = (thirdPerson && nailong)
                    ? tpCam.GetCamera() : player->GetCamera();   // 按当前视角判断目标在不在窗口内
                aimLocked = FindAutoAimTarget(*maze, player->GetFeetPosition(),
                    enemyManager->GetRenderStates(), PLAYER_AUTO_AIM_RANGE, aimCam, aimLockPos);
                // 锁到敌人后按住左键自动开火，不需要自己瞄准；RL 自动通关 / 训练桥走位时交给机器人
                if (!rlMode && !rlTrainMode &&
                    IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
                    playerAttacksRemaining > 0 &&
                    playerAttackCooldown <= 0.0f &&
                    aimLocked)
                {
                    const Vector3 origin = player->GetPosition();
                    const Vector3 hitPoint = {
                        aimLockPos.x,
                        GROUND_Y + EnemyConfig::ENEMY_HEIGHT * 0.6f,
                        aimLockPos.z
                    };
                    const Vector3 direction =
                        Vector3Normalize(Vector3Subtract(hitPoint, origin));
                    enemyManager->FireOrb(origin, direction);
                    sfxAudio.PlayOrbFire();
                    firedThisFrame = true;
                    fireYaw = atan2f(direction.x, direction.z);
                    --playerAttacksRemaining;
                    playerAttackCooldown = EnemyConfig::ORB_FIRE_COOLDOWN;
                }

                // ---- 奶龙：鼠标锁定 + 视角切换 + 动画状态机 ----
                // 不能用 IsCursorHidden()：Web 后端的 DisableCursor() 只请求 pointer lock，
                // 从不动 cursorHidden，浏览器里它恒为 false —— 视角就永远拖不动。
                cursorLocked = web::CursorLocked();       // 跟随真实锁定状态（暂停/结算时鼠标是解锁的）
                if (IsKeyPressed(KEY_TAB))                // Tab：锁定 / 解锁鼠标
                {
                    if (cursorLocked) EnableCursor();
                    else              DisableCursor();
                    cursorLocked = !cursorLocked;
                }
                if (IsKeyPressed(KEY_V) && gameMode == GameMode::NAILONG)   // 只有奶龙模式能切视角
                {
                    thirdPerson = !thirdPerson;
                    if (thirdPerson) tpCam.Reset(player->GetFeetPosition(), modelYaw * RAD2DEG);
                }

                player->pitchLimit = PITCH_LIM;
                if (thirdPerson)
                {
                    // 第三人称：鼠标交给环绕相机，移动以相机朝向为基准，瞄准沿相机视线
                    player->mouseLookEnabled = false;
                    // 鼠标滚轮：推近 / 拉远相机（贴墙时仍会被自动挤近）
                    const float wheelMove = GetMouseWheelMove();
                    if (wheelMove != 0.0f) tpCam.Zoom(wheelMove);
                    tpCam.Update(dt, player->GetFeetPosition(), *maze, cursorLocked);


                    player->SetMoveYawFromCamera(true, tpCam.GetYawDeg() * DEG2RAD);
                    player->SetAimDir(tpCam.GetAimDir());
                    player->SetYaw(tpCam.GetYawDeg() * DEG2RAD);
                }
                else
                {
                    player->mouseLookEnabled = cursorLocked;
                    player->SetMoveYawFromCamera(false, 0.0f);
                    player->ClearAimDir();
                    modelYaw = player->GetYaw();
                }

                if (nailong)
                {
                    // 角色朝向：开火时朝目标，移动时朝移动方向，站着不动就保持原朝向
                    const Vector3 moveInput = player->GetMoveDir();   // 本帧方向键输入（撞墙也不算停）
                    if (thirdPerson)
                    {
                        if (firedThisFrame) modelYaw = fireYaw;          // 索敌开火时转身看目标
                        else if (Vector3Length(moveInput) > 0.01f)        // 朝向平滑跟方向键（360 度任意方向）
                            modelYaw = atan2f(moveInput.x, moveInput.z);
                        // 没有方向键输入 -> 保持原朝向不动
                    }
                    else
                    {
                        modelYaw = player->GetYaw();
                    }

                    NailongState ns;
                    ns.feet = player->GetFeetPosition();
                    ns.yaw = modelYaw;
                    ns.grounded = player->IsGrounded();
                    ns.flying = player->flying;
                    ns.verticalSpeed = player->GetVelocity().y;
                    ns.moving = playerMoving;
                    ns.sprinting = player->IsSprinting();
                    ns.shootTriggered = firedThisFrame;
                    // F 大笑只在奶龙模式生效
                    ns.laughTriggered = (gameMode == GameMode::NAILONG) && IsKeyPressed(KEY_F);   // F：大笑
                    if (ns.laughTriggered)
                    {
                        sfxAudio.PlayNailongLaugh();
                        laughTimer = 1.6f;               // 与奶龙 Laugh 动画时长一致
                    }
                    nailong->Update(dt, ns);
                }
                // RL 模式不让玩家死：训练用的 MDP 里没有"死亡"，被咬的代价统一
                // 走 reward（-5/次，见 RlAgent::NotifyEnemyHit），游戏内表现与
                // 训练奖励定义因此完全一致。
                // v3：RL 自动通关不再锁血——被咬真的扣血，HP 归零本局终止
                // （reward -10，与 spec.compute_reward 的 died 分支一致）。
                // 训练桥只是拿真游戏做对拍、不练策略，仍然保持锁血。
                if (rlTrainMode) playerHealth = playerMaxHealth;

                const Treasure::PickupResult picked = Treasure::Collect(*maze, *player);
                score += picked.score * 10;
                playerAttacksRemaining += picked.attack * 3;
                playerHealth = min(playerMaxHealth,
                    playerHealth + picked.heal * 10);

                // 饥饿 / 肉鸽：吃到任意宝物（金 / 蓝 / 红）都回 5 点饱腹值
                if (hunger.enabled && (picked.score + picked.attack + picked.heal) > 0)
                    hunger.Add(HUNGER_REFILL_PER_PICKUP);

                // 肉鸽：金色宝物既是分数也是"宝物币"，稀有增益就用它买
                if (rogue.active && picked.score > 0)
                {
                    rogue.goldPicked += picked.score;
                    rogue.treasureCoins += picked.score;
                }

                // 同一帧可能拾取多种宝物，分别播放对应音效
                if (picked.score > 0) sfxAudio.PlayGoldPickup();
                if (picked.attack > 0) sfxAudio.PlayAttackPickup();
                if (picked.heal > 0) sfxAudio.PlayHealPickup();

                // 地雷：脚底进到触发半径内、且没跳过去才算踩到；踩到即消失
                {
                    const Vector3 feetNow = player->GetFeetPosition();
                    const float mineTopY = GROUND_Y + MINE_HEIGHT;
                    for (int my = 0; my < maze->grid.height && mineDamagePending == 0; ++my)
                        for (int mx = 0; mx < maze->grid.width && mineDamagePending == 0; ++mx)
                        {
                            if (maze->grid.at(mx, my) != CellType::MINE) continue;
                            const Vector3 minePos = maze->GridToWorld(mx, my);
                            const float mdx = feetNow.x - minePos.x;
                            const float mdz = feetNow.z - minePos.z;
                            if (mdx * mdx + mdz * mdz >
                                MINE_TRIGGER_RADIUS * MINE_TRIGGER_RADIUS) continue;
                            // 脚底高于顶面一定高度即视为跳过去，不触发
                            if (feetNow.y > mineTopY + MINE_TRIGGER_HEIGHT_TOLERANCE) continue;

                            maze->grid.at(mx, my) = CellType::PATH;
                            mineDamagePending = MINE_DAMAGE;
                        }

                    if (mineDamagePending > 0)
                    {
                        mineHitThisFrame = true;
                        mineFlashTimer = MINE_FLASH_SECONDS;
                        mineShakeTimer = MINE_SHAKE_SECONDS;
                        // 增益「防眩」：踩雷不再致盲（伤害不变）
                        mineBlindTimer = RogueHas(Buff::MINE_NO_BLIND)
                            ? 0.0f : MINE_BLIND_SECONDS;
                        sfxAudio.PlayMineExplode();
                    }
                }

                if (IsAtExit(*maze, *player))
                {
                    // 玩家踏上终点格就算过关：普通肉鸽走结算，
                    // 管理员+肉鸽在下面直接进下一层（管理员永远不写 log.csv）。
                    escaped = true;
                    timer.Pause();
                    sfxAudio.PlayGameClear();
                }
            }

            // 喘气：能量低于一半且正在移动时才循环播放（暂停 / 结算会自动停）
            sfxAudio.SetPlayerPantLoop(
                !finished && playerMoving &&
                player->GetStaminaRatio() < PLAYER_PANT_STAMINA_RATIO);
            // 减速带发黏声：踩在减速带上并且正在移动时才循环播放
            sfxAudio.SetPlayerStickyLoop(
                !finished && playerMoving && player->IsOnSlowZone());

            playerSnapshot.feetPosition = player->GetFeetPosition();
            playerSnapshot.bounds = player->GetBoundingBox();
            playerSnapshot.alive = !finished && playerHealth > 0;
            enemyManager->PublishPlayerSnapshot(playerSnapshot);

            const int healthBeforeDamage = playerHealth;
            // 管理员（肉鸽）免伤：血量锁定；RL 自动通关 / 训练器要真实掉血，
            // 那两个模式虽然是 adminMode 但必须放行伤害。
            const bool adminNoDamage =
                adminMode && rogue.active && !rlMode && !rlTrainMode;
            bool playerTookDamage = false;
            for (const DamageEvent& event : enemyManager->ConsumeDamageEvents())
            {
                if (event.amount <= 0)
                {
                    continue;
                }

                // 增益「锁血」：血一点都不掉（代价是整局不能跑步）
                if (adminNoDamage || RogueHas(Buff::LOCK_HP))
                {
                    continue;
                }

                // 增益「怪力削弱」：怪物每下最多打掉 5 点血
                const int hitDamage = RogueHas(Buff::ENEMY_WEAK)
                    ? min(event.amount, 5) : event.amount;
                playerHealth = max(0, playerHealth - hitDamage);
                playerTookDamage = true;
                // 被咬一次 = 下一步 reward 里 -5.0（与训练环境一致）
                if (rlMode || rlTrainMode) rlAgent.NotifyEnemyHit(1);
            }

            // 踩到地雷的伤害和敌人伤害一起结算，致命仍然优先播放死亡音效
            // （「锁血」时不掉血，但爆炸的动静照旧）
            if (mineDamagePending > 0)
            {
                if (!adminNoDamage && !RogueHas(Buff::LOCK_HP))
                {
                    playerHealth = max(0, playerHealth - mineDamagePending);
                    playerTookDamage = true;
                }
                mineDamagePending = 0;
            }

            // 增益「受伤提速」：挨了一下之后 5 秒内移速 x4
            if (playerTookDamage && RogueHas(Buff::HURT_SPEED))
                rogue.hurtSpeedTimer = ROGUE_HURT_SPEED_SECONDS;

            // 致命伤害优先播放死亡音效，避免普通受伤和死亡音效重叠
            if (healthBeforeDamage > 0 && playerHealth <= 0)
            {
                sfxAudio.PlayPlayerDeath();
            }
            else if (playerTookDamage && !mineHitThisFrame)   // 地雷有独立爆炸声，不再叠受伤音
            {
                sfxAudio.PlayPlayerDamage();
            }

            const int defeatedEnemies =
                enemyManager->ConsumeDefeatedEnemyCount();
            if (defeatedEnemies > 0)
            {
                sfxAudio.PlayEnemyDefeat();

                // 增益「击杀回饱腹」：每击杀一只回 10 点饱腹值
                if (RogueHas(Buff::KILL_SATIETY) && hunger.enabled)
                {
                    hunger.Add(ROGUE_KILL_SATIETY * (float)defeatedEnemies);
                    rogue.killSatietyScore += (int)ROGUE_KILL_SATIETY * defeatedEnemies;
                }
            }

            // 玩家还活着时，根据后台产生的脚步事件播放远小近大的脚步声
            if (!escaped && playerHealth > 0)
            {
                const vector<FootstepEvent> footstepEvents =
                    enemyManager->ConsumeFootstepEvents();
                footstepAudio.Update(
                    footstepEvents,
                    player->GetFeetPosition(),
                    player->GetCamera(),
                    *maze
                );
            }
            else
            {
                enemyManager->ConsumeFootstepEvents();
                footstepAudio.StopAll();
            }

            if (rlMode && playerHealth <= 0 && !rlHalted)
            {
                // HP 归零 = 本局失败终止（训练环境同一分支：不给 R_EXIT，给 R_DEATH）
                rlHalted = true;
                rlStatus = "RL: 被敌人击杀（HP 归零），按 F1 重开";
                rl::terminal::Note("episode totals %s", rlAgent.Describe().c_str());
                rl::terminal::Halt("killed-by-enemy", rlSteps,
                    rlAgent.MaxSteps(), rlAgent.Gx(), rlAgent.Gy(),
                    rlAgent.Field().At(rlAgent.Gx(), rlAgent.Gy()));
                ReportRlRoute(false);
            }

            if (playerHealth <= 0 || escaped) timer.Pause();

            // 边界情况：游戏这一帧刚判定"已逃离"，walker 还没报"走到出口格"。
            // 兜底收尾，避免 RL 会话停在"正在走进出口"。
            if (finished && rlMode && rlFinishing) FinishRlEpisode();

            if (finished)
            {
                bool showOverMenu = true;      // 肉鸽层间通关：这一帧不弹结算、也不吃结算键
                if (!overMenuShown)
                {
                    overMenu.SetTitle(escaped ? "YOU ESCAPED!" : "GAME OVER");

                    if (escaped && rogue.active && adminMode && !rlMode && !rlTrainMode &&
                        rogue.floor + 1 < ROGUE_FLOOR_COUNT)
                    {
                        // 管理员 + 肉鸽：碰到终点 = 直接进下一层（不弹增益窗、不收成绩、不写 log），
                        // 想回上一层就用 ESC 菜单里的「回到上一层」，不会再把本局"结束掉"。
                        ++rogue.floor;
                        escaped = false;
                        showOverMenu = false;
                        StartRogueFloor(true);       // 血量 / 弹药 / 分数 / 饱腹 / 用时全继承
                    }
                    else if (escaped && rogue.active && !adminMode &&
                        rogue.floor + 1 < ROGUE_FLOOR_COUNT)
                    {
                        // 肉鸽前 4 层通关：先弹增益窗，选完直接进下一层（总时间继续累计）
                        // 增益窗口期间不计时：总用时只统计在各层里跑的时间
                        ++rogue.floor;
                        OfferRogueBuffs();
                        escaped = false;
                        EnableCursor();
                        state = GameState::ROGUE_REWARD;
                        sfxAudio.PlayUiClick();
                        showOverMenu = false;
                    }
                    else if (escaped && !adminMode)
                    {
                        // 只有通关才记录：本场得分 + 用时写进 log.csv
                        // （同名 + 同难度只留成绩最好的一次，不如旧成绩就不写）
                        ScoreRecord record;
                        record.name = playerName;
                        record.difficulty = currentDifficulty;
                        record.mode = gameMode;
                        record.score = score;
                        record.elapsed = timer.GetElapsed();
                        record.date = Account::NowText();

                        const bool updated = Account::SubmitRecord(record);
                        if (rogue.active)
                        {
                            // 肉鸽：5 层全部通关，记录本局总分与总时间
                            overMenu.SetSubtitle("肉鸽 5 层全部通关\n总分 " + to_string(score) +
                                "    总用时 " + string(timer.GetText()) +
                                (updated ? "\n总成绩已记入 log.csv" : "\n未记入 log.csv（不如旧成绩）"));
                        }
                        else
                        {
                            overMenu.SetSubtitle("用时 " + string(timer.GetText()) +
                                (updated ? "\n成绩已记入 log.csv" : "\n未记入 log.csv（不如旧成绩）"));
                        }
                    }
                    else if (escaped && rlMode)
                    {
                        overMenu.SetSubtitle("RL 机器人用时 " + string(timer.GetText()) +
                            "\n步数 " + to_string(rlSteps) +
                            "（最优 " + to_string(rlAgent.OptimalSteps()) + "）" +
                            "\n不计入 log.csv");
                    }
                    else if (escaped)
                    {
                        // 管理员模式：通关但不计成绩
                        overMenu.SetSubtitle("用时 " + string(timer.GetText()) +
                            "\n管理员模式：成绩不计入 log.csv");
                    }
                    else
                    {
                        overMenu.SetSubtitle("用时 " + string(timer.GetText()));
                    }

                    if (showOverMenu)
                    {
                        // 管理员 + 肉鸽：多给一个「继续游戏」，免得碰到终点就以为游戏退出了
                        if (adminMode && rogue.active && !rlMode && !rlTrainMode)
                            overMenu.SetItems({ { "继续游戏", MenuAction::ADMIN_CONTINUE },
                                                { "重新开始", MenuAction::RESTART },
                                                { "记录排名", MenuAction::OPEN_RANKING },
                                                { "返回主菜单", MenuAction::BACK } });
                        else
                            overMenu.SetItems({ { "重新开始", MenuAction::RESTART },
                                                { "记录排名", MenuAction::OPEN_RANKING },
                                                { "返回主菜单", MenuAction::BACK } });
                        overMenu.Reset();
                        EnableCursor();
                        overMenuShown = true;
                        sfxAudio.StopBackground();   // 结算：背景音乐停下
                    }
                }

                if (!showOverMenu)
                {
                    // 刚从"通关"切到肉鸽增益弹窗：本帧不处理结算键
                }
                else if (IsKeyPressed(KEY_R))      action = MenuAction::RESTART;
                else if (IsKeyPressed(KEY_ESCAPE)) action = MenuAction::BACK;
                else                               action = overMenu.Update();
            }
            else if (IsKeyPressed(KEY_ESCAPE))
            {
                sfxAudio.PlayUiClick();
                timer.Pause();
                string pauseSub = "当前用时 " + string(timer.GetText());
                if (rogue.active)
                {
                    pauseSub += "\n肉鸽 第 " + to_string(rogue.floor + 1) + "/" +
                        to_string(ROGUE_FLOOR_COUNT) + " 层";
                    pauseSub += "\n宝物币 " + to_string(rogue.treasureCoins) +
                        "（本局已捡 " + to_string(rogue.goldPicked) + " 个金宝物）";
                    pauseSub += "\n已选增益:";
                    for (Buff b : rogue.owned) pauseSub += string("\n  ") + BuffName(b);
                    if (rogue.owned.empty()) pauseSub += "\n  （无）";
                }
                pauseMenu.SetSubtitle(pauseSub);
                pauseMenu.SetItems(PauseMenuItems(adminMode, rlMode, rlTrainMode,
                    RlEntryAllowed(), rogue.active));
                pauseMenu.Reset();
                EnableCursor();
                state = GameState::PAUSED;
            }
            break;
        }

        case GameState::CONFIRM_EXIT:               // 退出游戏确认框（只有这里能真正退出）
        {
            const MenuAction pick = confirmExitMenu.Update();
            if (pick == MenuAction::EXIT_GAME)
            {
                sfxAudio.PlayUiClick();
                shouldExit = true;                  // 用户确认了，才结束进程
            }
            else if (pick == MenuAction::BACK || IsKeyPressed(KEY_ESCAPE))
            {
                sfxAudio.PlayUiClick();
                mainMenu.Reset();
                state = GameState::MENU;
            }
            break;
        }


        case GameState::PAUSED:
            if (IsKeyPressed(KEY_ESCAPE))           // 直接继续
            {
                sfxAudio.PlayUiClick();
                timer.Resume();
                DisableCursor();
                state = GameState::PLAYING;
            }
            else
            {
                action = pauseMenu.Update();

                // 右下角"管理员"按钮 -> 输入密码（肉鸽模式不给进）
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
                    CheckCollisionPointRec(GetMousePosition(), AdminButtonRect()))
                {
                    sfxAudio.PlayUiClick();
                    passwordInput.Clear();
                    passwordError = false;
                    state = GameState::ADMIN_LOGIN;
                }
            }
            break;
        }

        // UI 交互音效：任意菜单动作，无论是鼠标点击还是回车确认，都播放一次
        if (action != MenuAction::NONE)
        {
            sfxAudio.PlayUiClick();
        }

        // ---- 2. 菜单动作 ----
        switch (action)
        {
        case MenuAction::START_GAME:                // 开始游戏 -> 先登录 / 跳过
            nameInput.Clear();
            loginError = false;
            EnableCursor();
            state = GameState::LOGIN;
            break;

        case MenuAction::RESTART:                   // 重开
            if (currentDifficulty == Difficulty::ROGUE)
            {
                // 肉鸽重开：增益全部清空，回到第一层，先抽第一窗增益
                rogue.Clear();
                rogue.active = true;
                rogueMap.valid = false;
                rogue.floor = 0;
                OfferRogueBuffs();
                EnableCursor();
                state = GameState::ROGUE_REWARD;
                break;
            }
            ResetRun();
            // RL 自动模式下重开要整局重新初始化（agent / walker 不能带着旧局面走）
            if (rlMode) BeginRlEpisode();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::RL_AUTO:                   // 管理员暂停菜单 -> RL 机器人自动通关
            if (!RlEntryAllowed()) { rlStatus = "RL: 仅支持 简单/普通/困难 的第一人称视角"; break; }   // 奶龙模式没有 RL
            rlPasswordInput.Clear();
            rlPasswordError = false;
            EnableCursor();
            state = GameState::RL_LOGIN;
            break;

        case MenuAction::RL_TRAIN:                  // 管理员暂停菜单 -> 开训练桥
            if (!RlEntryAllowed()) { rlStatus = "RL: 仅支持 简单/普通/困难 的第一人称视角"; break; }   // 奶龙模式没有 RL
            EnsureRlWeights();
            rl::terminal::SetMode("bridge");
            if (rlBridge.Start())
            {
                rlTrainMode = true;
                rlMode = false;
                ResetRun(false);
                RandomizeSpawnOrientation();
                if (player)
                {
                    player->flyEnabled = false;
                    player->flying = false;
                }
                rlBridge.PublishMaze(*maze);
                rlStatus = "RL: 训练桥 listening 127.0.0.1:45871";
            }
            else
            {
                rlStatus = "RL: 训练桥启动失败（端口被占用？）";
            }
            timer.Resume();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::RL_STOP:                   // 管理员暂停菜单 -> 停掉机器人，手动接管
            rlMode = false;
            rlHalted = false;
            rlFinishing = false;
            rlWalker.ClearTarget();
            rlAgent.SetNet(nullptr);
            rl::terminal::Halt("stopped-by-user", rlSteps, rlAgent.MaxSteps(),
                rlAgent.Gx(), rlAgent.Gy(),
                rlAgent.Field().At(rlAgent.Gx(), rlAgent.Gy()));
            ReportRlRoute(false);
            rlStatus = "RL: 已停止（手动接管）";
            timer.Resume();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::SAVE_SLOT_1:
        case MenuAction::SAVE_SLOT_2:
        case MenuAction::SAVE_SLOT_3:
        {
            const int slot = (action == MenuAction::SAVE_SLOT_1) ? 1
                : (action == MenuAction::SAVE_SLOT_2) ? 2 : 3;
            const bool ok = DoSave(maze, player, playerHealth, playerMaxHealth,
                playerAttacksRemaining, score, timer.GetElapsed(),
                slot);
            TraceLog(LOG_INFO, ok ? "Save slot %d OK" : "Save slot %d FAILED", slot);
            break;
        }

        case MenuAction::LOAD_SLOT_1:
        case MenuAction::LOAD_SLOT_2:
        case MenuAction::LOAD_SLOT_3:
        {
            const int slot = (action == MenuAction::LOAD_SLOT_1) ? 1
                : (action == MenuAction::LOAD_SLOT_2) ? 2 : 3;
            GameSave::Data data;
            if (!DoLoad(maze, player, enemyManager, data, slot))
            {
                TraceLog(LOG_WARNING, "Load slot %d FAILED", slot);
                break;
            }

            playerHealth = data.playerHealth;
            playerMaxHealth = data.playerMaxHealth;
            playerAttacksRemaining = data.playerAttacksRemaining;
            score = data.score;
            escaped = false;
            overMenuShown = false;
            wasSprinting = false;
            wasOnSlowZone = false;

            timer.Start();
            timer.Update(data.elapsed);

            if (enemyManager) { enemyManager->Stop(); enemyManager.reset(); }
            enemyManager = std::make_unique<EnemyManager>(*maze);

            playerSnapshot.feetPosition = player->GetFeetPosition();
            playerSnapshot.bounds = player->GetBoundingBox();
            playerSnapshot.alive = true;
            const DifficultyConfig cfg = GetDifficultyConfig(currentDifficulty);
            maze->slowZoneFactor = cfg.slowZoneFactor;   // 读档后减速倍率按当前难度
            enemyManager->BornInitialEnemies(playerSnapshot, cfg.enemyCount);
            enemyManager->Start();
            sfxAudio.PlayGameStart();
            sfxAudio.PlayBackground(BgmIndex(currentDifficulty));   // 读档后按当前难度续上背景音乐

            thirdPerson = (gameMode == GameMode::NAILONG);
            modelYaw = player->GetYaw();
            tpCam.Reset(player->GetFeetPosition(), modelYaw * RAD2DEG);   // 读档后相机跟上新朝向

            DisableCursor();
            state = GameState::PLAYING;
            break;
        }

        case MenuAction::HELP:
            helpMenu.Reset();
            state = GameState::HELP;
            break;

        case MenuAction::OPEN_DIFFICULTY:
            difficultyMenu.Reset();
            state = GameState::DIFFICULTY;
            break;

        case MenuAction::OPEN_RANKING:
            rankReturn = (state == GameState::PAUSED) ? GameState::PAUSED
                : ((state == GameState::PLAYING) ? GameState::PLAYING : GameState::MENU);
            LoadRank(rankTab);
            EnableCursor();
            state = GameState::RANKING;
            break;

        case MenuAction::INFO_EASY:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::EASY));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::INFO_NORMAL:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::NORMAL));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::INFO_HARD:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::HARD));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::INFO_HELL:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::HELL));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::INFO_HOLY:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::HOLY));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::INFO_HUNGER:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::HUNGER));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::INFO_ROGUE:
            diffInfoMenu.SetSubtitle(DifficultyInfoText(Difficulty::ROGUE));
            diffInfoMenu.Reset();
            state = GameState::DIFF_INFO;
            break;

        case MenuAction::OPEN_LOAD_MENU:                  // ← 加这段
            loadMenu.Reset();
            state = GameState::LOAD_MENU;
            break;

        case MenuAction::MODE_NORMAL:
        case MenuAction::MODE_NAILONG:
            gameMode = (action == MenuAction::MODE_NAILONG) ? GameMode::NAILONG : GameMode::NORMAL;
            thirdPerson = (gameMode == GameMode::NAILONG);   // 正常模式锁死第一人称
            difficultyMenu.Reset();
            state = GameState::DIFFICULTY;
            break;

        case MenuAction::DIFF_EASY:
            currentDifficulty = Difficulty::EASY;
            ResetRun();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::DIFF_NORMAL:
            currentDifficulty = Difficulty::NORMAL;
            ResetRun();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::DIFF_HARD:
            currentDifficulty = Difficulty::HARD;
            ResetRun();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::DIFF_HELL:
            currentDifficulty = Difficulty::HELL;
            ResetRun();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::DIFF_HOLY:
            currentDifficulty = Difficulty::HOLY;
            ResetRun();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::DIFF_HUNGER:               // 饥饿：神圣的墙 + 饱腹值机制
            currentDifficulty = Difficulty::HUNGER;
            ResetRun();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::DIFF_ROGUE:                // 肉鸽：5 层连打，先抽第一层增益
            currentDifficulty = Difficulty::ROGUE;
            rogue.Clear();
            rogue.active = true;
            rogueMap.valid = false;
            rogue.floor = 0;
            hunger.enabled = true;
            OfferRogueBuffs();
            EnableCursor();
            state = GameState::ROGUE_REWARD;
            break;

        case MenuAction::ADMIN_CONTINUE:            // 管理员：结算弹窗里继续玩
            escaped = false;
            overMenuShown = false;
            if (rogue.active)
            {
                rogue.floor = min(rogue.floor, ROGUE_FLOOR_COUNT - 1);
                StartRogueFloor(true);              // 重开本层，血量 / 用时都留着
            }
            else
            {
                timer.Resume();
                DisableCursor();
                state = GameState::PLAYING;
            }
            break;


        case MenuAction::ADMIN_BUFFS:               // 管理员：打开增益选择页
            adminBuffCursor = 0;
            EnableCursor();
            state = GameState::ADMIN_BUFFS;
            break;

        case MenuAction::ADMIN_NEXT_FLOOR:          // 管理员：直接跳到下一层
            if (rogue.active)
            {
                if (rogue.floor + 1 >= ROGUE_FLOOR_COUNT)
                {
                    // 已经在最后一层：直接算通关（管理员模式不写 log.csv）
                    rogue.floor = ROGUE_FLOOR_COUNT - 1;
                    escaped = true;
                    DisableCursor();
                    state = GameState::PLAYING;
                }
                else
                {
                    ++rogue.floor;
                    StartRogueFloor(true);          // 继承血量 / 弹药 / 分数 / 饱腹
                }
            }
            else
            {
                DisableCursor();
                state = GameState::PLAYING;
            }
            break;

        case MenuAction::ADMIN_PREV_FLOOR:          // 管理员：回到上一层
            if (rogue.active && rogue.floor > 0)
            {
                --rogue.floor;
                StartRogueFloor(true);
            }
            else
            {
                timer.Resume();
                DisableCursor();
                state = GameState::PLAYING;
            }
            break;


        case MenuAction::RESUME:
            timer.Resume();
            DisableCursor();
            state = GameState::PLAYING;
            break;

        case MenuAction::BACK:
            // 排名页 / 难度详情是"叠加"打开的，先返回上一层
            if (state == GameState::RANKING)
            {
                state = rankReturn;
                if (rankReturn == GameState::PLAYING) EnableCursor();      // 结算弹窗还在，鼠标要能用
                else if (rankReturn == GameState::PAUSED) pauseMenu.Reset();
                else mainMenu.Reset();
                break;
            }
            if (state == GameState::DIFF_INFO)
            {
                difficultyMenu.Reset();
                state = GameState::DIFFICULTY;
                break;
            }

            if (state == GameState::MODE_SELECT)
            {
                state = GameState::LOGIN;
                break;
            }
            if (state == GameState::DIFFICULTY)
            {
                modeMenu.Reset();
                state = GameState::MODE_SELECT;
                break;
            }

            // 返回主菜单：肉鸽本局结束（增益随局失效），饥饿状态一起复位
            rogue.Clear();
            rogueMap.valid = false;
            hunger.enabled = false;
            hunger.infinite = false;
            hunger.drainScale = 1.0f;
            hunger.Reset(0.0f, 1.0f);
            laughTimer = 0.0f;

            timer.Reset();
            if (rlMode) ReportRlRoute(rlAgent.Success());
            adminMode = false;
            rlMode = false;
            rlHalted = false;
            if (rlTrainMode)
            {
                rlBridge.Stop();
                rlTrainMode = false;
            }
            rlWalker.ClearTarget();
            rlAgent.SetNet(nullptr);
            if (player)
            {
                player->flyEnabled = false;
                player->infiniteStamina = false;
                player->hoverUnlocked = false;
                player->flying = false;
            }
            sfxAudio.StopBackground();   // 返回主菜单：背景音乐停下
            mainMenu.Reset();
            EnableCursor();
            state = GameState::MENU;
            break;

        case MenuAction::EXIT_GAME:
            sfxAudio.StopBackground();
            confirmExitMenu.Reset();
            state = GameState::CONFIRM_EXIT;   // 不再直接 shouldExit
            break;

        case MenuAction::NONE:
        default:
            break;
        }
        if (shouldExit) break;


        // ---- RL 训练桥：把当前这一局当作环境交给 Python PPO ----
        if (rlTrainMode && rlBridge.Running() && maze != nullptr)
        {
            if (rlBridge.TakeResetRequest())
            {
                ResetRun(false);
                RandomizeSpawnOrientation();
                rlBridge.PublishMaze(*maze);
            }
            const rl::BridgeView view = rlBridge.Snapshot();
            if (player != nullptr && view.mazeWidth == maze->grid.width &&
                view.mazeHeight == maze->grid.height)
            {
                // 训练速度远高于帧率，直接把玩家吸附到当前逻辑格（当光标看）
                const Vector3 centre = maze->GridToWorld(view.dispGx, view.dispGy);
                player->SetFeetPosition(Vector3{ centre.x, GROUND_Y, centre.z });
            }
        }

        // ---- 3. 绘制 ----


        BeginDrawing();
        ClearBackground(DARKBLUE);

        if (state == GameState::PLAYING || state == GameState::PAUSED ||
            state == GameState::ADMIN_LOGIN || state == GameState::RL_LOGIN)
        {
            // 第三人称：相机挂在奶龙后上方并做避墙；第一人称：沿用玩家自带相机
            Camera3D cam = (thirdPerson && nailong)
                ? tpCam.GetCamera()
                : player->GetCamera();
            // 踩雷屏幕震动：只偏移渲染相机，不影响玩家物理、瞄准和碰撞
            if (mineShakeTimer > 0.0f)
            {
                const float shakeK = mineShakeTimer / MINE_SHAKE_SECONDS;
                const float shakeAmp = MINE_SHAKE_STRENGTH * shakeK;
                const float shakeX = (float)GetRandomValue(-1000, 1000) / 1000.0f * shakeAmp;
                const float shakeY = (float)GetRandomValue(-1000, 1000) / 1000.0f * shakeAmp;
                const float shakeZ = (float)GetRandomValue(-1000, 1000) / 1000.0f * shakeAmp;
                cam.position.x += shakeX;
                cam.position.y += shakeY;
                cam.position.z += shakeZ;
                cam.target.x += shakeX;
                cam.target.y += shakeY;
                cam.target.z += shakeZ;
            }
            const vector<EnemyRenderState> enemyStates =
                enemyManager->GetRenderStates();

            // ---- 伙伴：沿“玩家 -> 终点”的可行走路径，始终贴在玩家身前 3~5m ----
            // 这版伙伴是“纯装饰 + 开火”：只读地图 / 玩家状态，
            // 不碰 escaped / finished / state / playerHealth，也不写地图，
            // 所以它不可能让本局结束，更不可能让进程退出。
            if (rogue.companionActive && player != nullptr && maze != nullptr &&
                state == GameState::PLAYING && playerHealth > 0 && !escaped)
            {
                // (1) 距离场：每 0.5s 重算（拆墙 / 开洞后会变）
                rogue.companionFieldTimer -= GetFrameTime();
                if (rogue.companionFieldTimer <= 0.0f || g_companionDist.empty())
                {
                    rogue.companionFieldTimer = 0.5f;
                    BuildCompanionDist(*maze, g_companionDist);
                }

                const int cw = maze->grid.width;
                const int ch = maze->grid.height;
                const Vector3 feet = player->GetFeetPosition();
                const Vector3 fwd = player->GetForward();

                // (2) 位置自检：NaN / 跑到地图外 -> 拉回玩家身后
                const bool posBad =
                    !(rogue.companionPos.x == rogue.companionPos.x) ||
                    !(rogue.companionPos.z == rogue.companionPos.z) ||
                    rogue.companionPos.x < -1.0f || rogue.companionPos.z < -1.0f ||
                    rogue.companionPos.x > (float)cw * CELL_SIZE + 1.0f ||
                    rogue.companionPos.z > (float)ch * CELL_SIZE + 1.0f;
                if (posBad)
                    rogue.companionPos = Vector3{ feet.x - fwd.x * 2.0f, GROUND_Y + 0.3f,
                                                 feet.z - fwd.z * 2.0f };
                rogue.companionPos.y = GROUND_Y + 0.3f;

                // (3) 目标：从玩家所在格沿距离场梯度往终点方向走 3~5m 的那一格
                int pgx = 0;
                int pgy = 0;
                maze->WorldToGrid(feet, pgx, pgy);
                Vector3 goal{ feet.x, GROUND_Y + 0.3f, feet.z };
                bool goalOk = false;
                if (!g_companionDist.empty() && maze->grid.isInBounds(pgx, pgy))
                {
                    const size_t pidx = ((size_t)pgy * (size_t)cw) + (size_t)pgx;
                    if (pidx < g_companionDist.size() && g_companionDist[pidx] > 0)
                    {
                        int gx = pgx;
                        int gy = pgy;
                        float walked = 0.0f;
                        while (walked + CELL_SIZE <= ROGUE_COMPANION_AHEAD)
                        {
                            const int d = CompanionDistAt(g_companionDist, cw, ch, gx, gy);
                            if (d <= 1) break;                  // 已经站在终点格上了
                            int nx = gx;
                            int ny = gy;
                            if (CompanionDistAt(g_companionDist, cw, ch, gx + 1, gy) == d - 1)      nx = gx + 1;
                            else if (CompanionDistAt(g_companionDist, cw, ch, gx - 1, gy) == d - 1) nx = gx - 1;
                            else if (CompanionDistAt(g_companionDist, cw, ch, gx, gy + 1) == d - 1) ny = gy + 1;
                            else if (CompanionDistAt(g_companionDist, cw, ch, gx, gy - 1) == d - 1) ny = gy - 1;
                            else break;
                            gx = nx;
                            gy = ny;
                            walked += CELL_SIZE;
                        }
                        const Vector3 cell = maze->GridToWorld(gx, gy);
                        goal = Vector3{ cell.x, GROUND_Y + 0.3f, cell.z };
                        goalOk = true;
                    }
                }
                if (!goalOk)                                     // 兵底：直线贴在玩家身前 3m
                    goal = Vector3{ feet.x + fwd.x * 3.0f, GROUND_Y + 0.3f,
                                    feet.z + fwd.z * 3.0f };

                // (4) 平滑挪向目标（略快于玩家，保证一直在前面）
                const Vector3 cdelta = Vector3Subtract(goal, rogue.companionPos);
                const float cdist = sqrtf(cdelta.x * cdelta.x + cdelta.z * cdelta.z);
                if (cdist > 0.05f)
                {
                    const float maxStep = WALKSPEED * 1.6f * GetFrameTime();
                    const float k = (cdist > maxStep) ? (maxStep / cdist) : 1.0f;
                    rogue.companionPos.x += cdelta.x * k;
                    rogue.companionPos.z += cdelta.z * k;
                }

                // (5) 每 ROGUE_COMPANION_FIRE_SECONDS 秒朝 15m 内最近的怪打一发（不花玩家子弹）
                rogue.companionFireTimer -= GetFrameTime();
                if (rogue.companionFireTimer <= 0.0f)
                {
                    rogue.companionFireTimer = ROGUE_COMPANION_FIRE_SECONDS;
                    Vector3 best{ 0.0f, 0.0f, 0.0f };
                    float bestDist = ROGUE_COMPANION_FIRE_RANGE;
                    for (const EnemyRenderState& st : enemyStates)
                    {
                        if (!st.active || st.health <= 0.0f) continue;
                        const float dx = st.position.x - rogue.companionPos.x;
                        const float dz = st.position.z - rogue.companionPos.z;
                        const float d = sqrtf(dx * dx + dz * dz);
                        if (d >= 0.2f && d < bestDist)
                        {
                            bestDist = d;
                            best = st.position;
                        }
                    }
                    if (bestDist < ROGUE_COMPANION_FIRE_RANGE)
                    {
                        const Vector3 toward = { best.x, rogue.companionPos.y, best.z };
                        const Vector3 dir =
                            Vector3Normalize(Vector3Subtract(toward, rogue.companionPos));
                        if (dir.x == dir.x && dir.y == dir.y && dir.z == dir.z)   // 防 NaN
                        {
                            enemyManager->FireOrb(rogue.companionPos, dir);
                            sfxAudio.PlayOrbFire();
                        }
                    }
                }
            }

            // 地狱难度：视野受限（只看得清光源半径内）；其他难度直接画，零额外开销
            GroundRect(*maze, groundPos, groundSize);
            // 增益「暗视」：开局 30s 内不受光源限制；「狂笑」：奶龙大笑期间不受光源限制
            const bool darkFree = rogue.active && rogue.darkFreeTimer > 0.0f;
            const bool laughFree = RogueHas(Buff::NO_LIGHT_LIMIT);   // 常亮视野：不用大笑也生效
            // Web: the window can be resized/rescaled at any time -> keep the capture RT in sync
            light.EnsureSize(GetScreenWidth(), GetScreenHeight());
            const bool lightOn = IsLightLimited(EffectiveDifficulty()) && light.IsReady() &&
                !adminMode && !darkFree && !laughFree;
            if (lightOn)
            {
                light.BeginCapture(player->GetPosition());
                DrawScene3D(cam, groundPos, groundSize, *maze, *enemyManager, enemyStates,
                    holyBreakFlashPos, holyBreakFlashTimer);
                light.EndCapture();
                light.Draw();
            }
            else
            {
                DrawScene3D(cam, groundPos, groundSize, *maze, *enemyManager, enemyStates,
                    holyBreakFlashPos, holyBreakFlashTimer);
            }
            // 画奶龙本体（放在光照 pass 之后：地狱难度里角色本身就在光源中心，始终可见）
            if (thirdPerson && nailong)
            {
                BeginMode3D(cam);
                nailong->Draw();
                EndMode3D();
            }

            // 肉鸽稀有增益「伙伴」：绿色方块（同样放在光照 pass 之后，暗处也看得见）
            if (rogue.companionActive)
            {
                const float box = 0.6f;
                BeginMode3D(cam);   // 必须在 3D 模式里画：否则世界坐标会被当成 2D 屏幕坐标，等于没画
                DrawCube(rogue.companionPos, box, box, box, ROGUE_COMPANION_COLOR);
                DrawCubeWires(rogue.companionPos, box, box, box, BLACK);
                EndMode3D();        // 结果就是「伙伴一直不显示」
            }

            DrawEnemyHealthBars2D(cam, *maze, enemyStates);
            // 增益「雷达」：把终点方向画成绿点
            DrawEnemyRadar(*player, enemyStates,
                maze->GridToWorld(maze->exitGx, maze->exitGy), RogueHas(Buff::SHOW_EXIT));

            // 自动索敌锁定标记：锁到敌人就圈出来，提示按左键就能打中
            if (state == GameState::PLAYING && aimLocked)
            {
                const Vector2 lockScreen = GetWorldToScreen(
                    Vector3{ aimLockPos.x,
                             GROUND_Y + EnemyConfig::ENEMY_HEIGHT * 0.6f,
                             aimLockPos.z }, cam);
                if (lockScreen.x > 0.0f && lockScreen.y > 0.0f)
                {
                    DrawCircleLines((int)lockScreen.x, (int)lockScreen.y, 17.0f, RED);
                    DrawCircleLines((int)lockScreen.x, (int)lockScreen.y, 16.0f, RED);
                }
            }

            const float crosshairX = (float)GetScreenWidth() * 0.5f;
            const float crosshairY = (float)GetScreenHeight() * 0.5f;
            DrawCircle((int)crosshairX, (int)crosshairY, 3, GOLD);
            DrawCircleLines((int)crosshairX, (int)crosshairY, 9, GOLD);

            timer.DrawHUD(20, 16, UI_TEXT_SIZE, RAYWHITE);

            const int scoreLeft = CountScoreTreasures(*maze);
            const string status = "宝物 " +
                to_string((scoreLeft < runTreasureTarget) ? (runTreasureTarget - scoreLeft) : 0) +
                "/" + to_string(runTreasureTarget) +
                "    得分 " + to_string(score);
            DrawTextEx(font, Utf8(status).c_str(), Vector2{ 20.0f, 56.0f },
                (float)UI_TEXT_SIZE, 1.0f, RAYWHITE);

            const string hpText = "HP: " + to_string(playerHealth);
            const int hudRightX = GetScreenWidth() - 150;
            const int hudStatusY =
                (int)(RADAR_MARGIN + RADAR_RADIUS * 2.0f + 10.0f);
            DrawText(hpText.c_str(), hudRightX, hudStatusY, 26, RAYWHITE);

            const string attackText =
                "ATK: " + to_string(playerAttacksRemaining) +
                "/" + to_string(runAttackLimit);
            DrawText(attackText.c_str(), hudRightX, hudStatusY + 32, 24, RAYWHITE);

            // 状态条：普通难度是冲刺能量条；饥饿 / 肉鸽 整条换成红色饱腹值条
            const float barW = 260.0f;
            const float barH = 18.0f;
            const float barX = ((float)GetScreenWidth() - barW) * 0.5f;
            const float barY = 16.0f;
            const float barRatio = hunger.enabled ? hunger.Ratio() : player->GetStaminaRatio();
            Color barColor = LIME;
            if (hunger.enabled)
                barColor = (barRatio > 0.3f) ? HUNGER_BAR_COLOR : HUNGER_BAR_LOW_COLOR;
            else
                barColor = (barRatio > 0.5f) ? LIME : (barRatio > 0.2f ? GOLD : RED);

            DrawTextEx(font, Utf8(hunger.enabled ? "饱腹" : "能量").c_str(),
                Vector2{ barX - 58.0f, barY - 3.0f }, (float)UI_HINT_SIZE, 1.0f, RAYWHITE);
            DrawRectangle((int)barX, (int)barY, (int)barW, (int)barH, Fade(BLACK, 0.45f));
            DrawRectangle((int)barX, (int)barY, (int)(barW * barRatio), (int)barH, barColor);
            DrawRectangleLines((int)barX, (int)barY, (int)barW, (int)barH, RAYWHITE);

            // 饥饿 / 肉鸽：把饱腹值点数也写出来，饿到底时标红
            if (hunger.enabled)
            {
                const string satText = hunger.infinite
                    ? string("饱腹值 无限")
                    : ("饱腹值 " + to_string((int)ceilf(hunger.satiety)) + " / " +
                       to_string((int)hunger.satietyMax));
                DrawTextEx(font, Utf8(satText).c_str(),
                    Vector2{ barX + barW + 12.0f, barY - 3.0f },
                    (float)UI_HINT_SIZE, 1.0f, hunger.starving ? RED : RAYWHITE);
            }

            DrawTextEx(font, (gameMode == GameMode::NAILONG ? hudHintNailong
                : hudHintNormal).c_str(),
                Vector2{ 20.0f, (float)GetScreenHeight() - 34.0f },
                (float)UI_HINT_SIZE, 1.0f, Fade(RAYWHITE, 0.75f));

            // 奶龙当前动画状态（左上角）+ 视角/按键提示
            if (nailong && gameMode == GameMode::NAILONG)
            {
                const string animText = string("动画: ") + nailong->CurrentAnimName();
                DrawTextEx(font, Utf8(animText).c_str(), Vector2{ 20.0f, 88.0f },
                    (float)UI_TEXT_SIZE, 1.0f, SKYBLUE);
                const string viewText = thirdPerson
                    ? string("[第三人称] V 切换视角   F 大笑   Tab 锁定鼠标")
                    : string("[第一人称] V 切换视角   F 大笑   Tab 锁定鼠标");
                DrawTextEx(font, Utf8(viewText).c_str(), Vector2{ 20.0f, 118.0f },
                    (float)UI_HINT_SIZE, 1.0f, Fade(SKYBLUE, 0.9f));
            }

            // 帧率读数（诊断卡顿：RL 与普通模式都能看到同一行）
            DrawTextEx(font, TextFormat("FPS %d  %.1f ms", GetFPS(), GetFrameTime() * 1000.0f),
                Vector2{ 20.0f, 6.0f }, 20.0f, 1.0f, Color{ 190, 200, 220, 220 });

            // 难度提示（右上角，HP 上方）
            const string diffText = rogue.active
                ? ("肉鸽 第 " + to_string(rogue.floor + 1) + "/" + to_string(ROGUE_FLOOR_COUNT) +
                   " 层   " + DifficultyName(EffectiveDifficulty()) +
                   "   宝物币 " + to_string(rogue.treasureCoins))
                : (string("难度: ") + DifficultyName(currentDifficulty));
            DrawTextEx(font, Utf8(diffText).c_str(),
                Vector2{ (float)hudRightX, (float)(hudStatusY - 34) },
                24.0f, 1.0f, GOLD);

            // 肉鸽：已选增益数 + 受伤提速剩余时间
            if (rogue.active && !rogue.owned.empty())
            {
                const string buffLine = "增益 " + to_string((int)rogue.owned.size()) + " 个" +
                    (rogue.hurtSpeedTimer > 0.0f ? "   受伤提速中" : "");
                DrawTextEx(font, Utf8(buffLine).c_str(),
                    Vector2{ (float)hudRightX, (float)(hudStatusY - 64) }, 20.0f, 1.0f,
                    Color{ 150, 220, 160, 255 });
            }

            if (adminMode)
            {
                const string adminHint = rogue.active
                    ? "[ADMIN] 长按 Q 起飞  Z/X 升降  血量 / 子弹 / 饱腹 / 宝物 无限  ESC 菜单可拿增益 / 跳层（终点不结算）"
                    : "[ADMIN] 长按 Q 起飞  Z/X 升降  能量无限  光源无限  RL 入口在 ESC 菜单（F1 / F2）";
                DrawTextEx(font, Utf8(adminHint).c_str(),
                    Vector2{ 20.0f, (float)GetScreenHeight() - 62.0f },
                    (float)UI_HINT_SIZE, 1.0f, ORANGE);
            }

            // ---- RL 状态面板（自动通关 / 训练桥）----
            if (rlMode || rlTrainMode)
            {
                const float panelW = 430.0f;
                const float panelH = 192.0f;
                const Rectangle panel{
                    (float)GetScreenWidth() - panelW - 20.0f,
                    (float)GetScreenHeight() - panelH - 20.0f, panelW, panelH };
                DrawRectangleRounded(panel, 0.18f, 8, Color{ 12, 18, 34, 225 });
                DrawRectangleRoundedLinesEx(panel, 0.18f, 8, 1.5f,
                    Color{ 90, 200, 220, 255 });

                const float x = panel.x + 14.0f;
                float y = panel.y + 10.0f;
                const float fs = (float)UI_HINT_SIZE;

                DrawTextEx(font, rlMode ? "RL 自动通关" : "RL 训练桥",
                    Vector2{ x, y }, fs + 4.0f, 1.0f, Color{ 140, 230, 240, 255 });
                DrawTextEx(font, rl::terminal::Enabled() ? "F3 LOG:ON" : "F3 LOG:OFF",
                    Vector2{ x + 200.0f, y + 4.0f }, fs, 1.0f,
                    rl::terminal::Enabled()
                        ? Color{ 170, 220, 170, 255 } : Color{ 160, 160, 175, 255 });
                DrawTextEx(font, TextFormat("FPS %d  %.1fms", GetFPS(), GetFrameTime() * 1000.0f),
                    Vector2{ x + 300.0f, y + 4.0f }, fs, 1.0f,
                    Color{ 200, 206, 222, 255 });
                y += fs + 10.0f;

                auto line = [&](const std::string& text, Color c)
                    {
                        DrawTextEx(font, text.c_str(), Vector2{ x, y }, fs, 1.0f, c);
                        y += fs + 5.0f;
                    };

                if (rlTrainMode)
                {
                    const rl::BridgeView view = rlBridge.Snapshot();
                    line(std::string("桥: ") + view.status, RAYWHITE);
                    line("连接: " + std::string(view.connected ? "已连接" : "等待连接") +
                        "    局数: " + to_string(view.episodes), RAYWHITE);
                    line("动作: " + to_string(view.lastAction) +
                        "    步数: " + to_string(view.steps) + "/" + to_string(view.maxSteps), RAYWHITE);
                    char rewardBuf[32];
                    snprintf(rewardBuf, sizeof(rewardBuf), "%.2f", (double)view.lastReward);
                    line(std::string("奖励: ") + rewardBuf
                        + "    最优: " + to_string(view.optimalSteps), RAYWHITE);
                    line("速度: " + to_string((long long)view.stepsPerSecond) + " 步/秒"
                        + "    总步数: " + to_string(view.totalSteps), Color{ 170, 220, 170, 255 });
                }
                else
                {
                    const char* actName = (rlLastAction >= 0)
                        ? rl::kActName[rlLastAction] : "-";
                    line(std::string("权重: ") + (rlNet.Ready() ? rlNet.Shape()
                        : std::string("缺失（退回贪心）")),
                        rlNet.Ready() ? Color{ 170, 220, 170, 255 } : Color{ 240, 190, 120, 255 });
                    line(std::string("动作: ") + actName +
                        "    步数: " + to_string(rlSteps) + "/" + to_string(rlAgent.MaxSteps()), RAYWHITE);
                    char rewardBuf[64];
                    snprintf(rewardBuf, sizeof(rewardBuf), "%.2f    本局累计 %.0f",
                             (double)rlLastReward, rlEpisodeReturn);
                    line(std::string("奖励: ") + rewardBuf, RAYWHITE);
                    line("最优步数: " + to_string(rlAgent.OptimalSteps()) +
                        "    局数: " + to_string(rlEpisodeCount), RAYWHITE);
                    char statBuf[96];
                    snprintf(statBuf, sizeof(statBuf),
                             "宝物 %d  被咬 %d  击杀 %d  体力 %.2f  敌人 %d",
                             rlAgent.TreasuresCollected(), rlAgent.EnemyHits(),
                             rlAgent.Kills(),
                             player ? (double)player->GetStaminaRatio() : 0.0,
                             rlAgent.EnemyCount());
                    line(std::string(statBuf), Color{ 200, 220, 250, 255 });
                    line(rlStatus, rlHalted ? Color{ 250, 170, 120, 255 } : Color{ 170, 220, 170, 255 });
                }
            }

            const bool over = (playerHealth <= 0) || escaped;

            // 旧"先收集全部宝物"提示已删（现在不强制宝物）

            if (over) overMenu.Draw();
        }

        if (state == GameState::MENU)             mainMenu.Draw();
        else if (state == GameState::HELP)        helpMenu.Draw();
        else if (state == GameState::DIFFICULTY)  difficultyMenu.Draw();
        else if (state == GameState::MODE_SELECT) modeMenu.Draw();
        else if (state == GameState::CONFIRM_EXIT) confirmExitMenu.Draw();
        else if (state == GameState::LOAD_MENU)   loadMenu.Draw();
        else if (state == GameState::DIFF_INFO)   diffInfoMenu.Draw();
        else if (state == GameState::PAUSED)
        {
            pauseMenu.Draw();
            DrawFlatButton(font, AdminButtonRect(), "管理员", false);   // 肉鸽里也画（管理员新功能是肉鸽专属）
        }
        else if (state == GameState::LOGIN)
        {
            const TextPageLayout layout = MakeTextPageLayout(true);
            const float cx = layout.panel.x + layout.panel.width * 0.5f;

            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                Color{ 10, 14, 28, 255 }, Color{ 34, 48, 92, 255 });
            DrawRectangleRounded(layout.panel, UI_ROUND, 12, Color{ 18, 24, 44, 235 });
            DrawRectangleRoundedLinesEx(layout.panel, UI_ROUND, 12, 2.0f,
                Color{ 90, 130, 220, 255 });

            DrawUtf8Centered(font, Utf8("登录"), cx, layout.titleY, UI_TITLE_SIZE, RAYWHITE);
            DrawUtf8Centered(font, Utf8("输入用户名即可登录（最多 12 字），也可以直接跳过"),
                cx, layout.subY, UI_HINT_SIZE + 4, Color{ 185, 195, 215, 255 });

            nameInput.Draw(font, layout.input, "请输入用户名", UI_TEXT_SIZE);

            if (loginError)
                DrawUtf8Centered(font, Utf8("用户名不能为空"), cx, layout.msgY,
                    UI_HINT_SIZE, RED);

            DrawFlatButton(font, layout.btnA, "登录", false);
            DrawFlatButton(font, layout.btnB, "跳过", false);
            DrawUtf8Centered(font, Utf8("Enter 登录    ESC 返回主菜单"), cx, layout.hintY,
                UI_HINT_SIZE, Color{ 140, 150, 175, 255 });
        }
        else if (state == GameState::RL_LOGIN)
        {
            const TextPageLayout layout = MakeTextPageLayout(false);
            const float cx = layout.panel.x + layout.panel.width * 0.5f;

            DrawRectangleRounded(layout.panel, UI_ROUND, 12, Color{ 12, 26, 34, 245 });
            DrawRectangleRoundedLinesEx(layout.panel, UI_ROUND, 12, 2.0f,
                Color{ 90, 210, 220, 255 });

            DrawUtf8Centered(font, "RL 机器人", cx, layout.titleY,
                UI_TITLE_SIZE, Color{ 150, 235, 245, 255 });
            DrawUtf8Centered(font, "输入 RL 模式密码", cx, layout.subY,
                UI_TEXT_SIZE - 4, Color{ 185, 195, 215, 255 });

            rlPasswordInput.Draw(font, layout.input, "请输入密码", UI_TEXT_SIZE);

            if (rlPasswordError)
                DrawUtf8Centered(font, "密码错误", cx, layout.msgY, UI_HINT_SIZE, RED);

            DrawUtf8Centered(font, "Enter 确认    ESC 返回", cx, layout.hintY,
                UI_HINT_SIZE, Color{ 140, 150, 175, 255 });
        }
        else if (state == GameState::ADMIN_LOGIN)
        {
            const TextPageLayout layout = MakeTextPageLayout(false);
            const float cx = layout.panel.x + layout.panel.width * 0.5f;

            DrawRectangleRounded(layout.panel, UI_ROUND, 12, Color{ 18, 24, 44, 245 });
            DrawRectangleRoundedLinesEx(layout.panel, UI_ROUND, 12, 2.0f,
                Color{ 220, 130, 90, 255 });

            DrawUtf8Centered(font, Utf8("管理员模式"), cx, layout.titleY,
                UI_TITLE_SIZE, Color{ 255, 200, 130, 255 });
            DrawUtf8Centered(font, Utf8("请输入管理员密码"), cx, layout.subY,
                UI_TEXT_SIZE - 4, Color{ 185, 195, 215, 255 });

            passwordInput.Draw(font, layout.input, "请输入密码", UI_TEXT_SIZE);

            if (passwordError)
                DrawUtf8Centered(font, Utf8("密码错误"), cx, layout.msgY, UI_HINT_SIZE, RED);

            DrawUtf8Centered(font, Utf8("Enter 确认    ESC 返回"), cx, layout.hintY,
                UI_HINT_SIZE, Color{ 140, 150, 175, 255 });
        }
        else if (state == GameState::ADMIN_BUFFS)   // 管理员：任意增益选中即得
        {
            const int total = BUFF_ALL_COUNT;
            const int scroll = AdminBuffScroll(adminBuffCursor);
            const float cx = (float)GetScreenWidth() * 0.5f;

            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                Color{ 10, 14, 28, 255 }, Color{ 34, 48, 92, 255 });
            DrawUtf8Centered(font, Utf8("管理员增益"), cx, 12.0f, UI_TITLE_SIZE,
                Color{ 255, 200, 130, 255 });
            DrawUtf8Centered(font, Utf8("一般 / 特殊 / 稀有 三档都能直接拿，不用宝物，选中即生效"),
                cx, 58.0f, UI_HINT_SIZE + 2, Color{ 185, 195, 215, 255 });
            DrawUtf8Centered(font, Utf8("W/S 或 上下键 选择    空格 / Enter / 鼠标左键 开-关    ESC 回到暂停菜单"),
                cx, 84.0f, UI_HINT_SIZE, Color{ 150, 160, 190, 255 });

            for (int i = scroll; i < min(total, scroll + ADMIN_BUFF_ROWS); ++i)
            {
                const Buff b = (Buff)i;
                const Rectangle row = AdminBuffRow(i, scroll);
                const bool hot = CheckCollisionPointRec(GetMousePosition(), row);
                const bool owned = rogue.Has(b);
                const bool focus = (i == adminBuffCursor) || hot;
                const int tier = BuffTier(b);
                const Color edge = owned ? Color{ 90, 200, 130, 255 }
                    : (tier == 2 ? ROGUE_RARE_TAG_COLOR
                        : (tier == 1 ? GOLD : Color{ 90, 130, 220, 255 }));

                DrawRectangleRounded(row, 0.3f, 8,
                    owned ? Color{ 26, 70, 50, 235 }
                    : (focus ? Color{ 58, 96, 190, 235 } : Color{ 24, 34, 60, 235 }));
                DrawRectangleRoundedLinesEx(row, 0.3f, 8, 1.5f, edge);

                const string title = string(owned ? "[已启用] " : "[未启用] ") + BuffName(b);
                DrawTextEx(font, Utf8(title).c_str(),
                    Vector2{ row.x + 16.0f, row.y + 2.0f }, (float)UI_TEXT_SIZE - 6, 1.0f,
                    owned ? Color{ 150, 235, 180, 255 } : RAYWHITE);
                DrawTextEx(font, Utf8(tier == 2 ? "稀有" : (tier == 1 ? "特殊" : "一般")).c_str(),
                    Vector2{ row.x + row.width - 70.0f, row.y + 5.0f },
                    (float)UI_HINT_SIZE, 1.0f, edge);
                DrawTextEx(font, Utf8(BuffDesc(b)).c_str(),
                    Vector2{ row.x + 16.0f, row.y + 24.0f }, (float)UI_HINT_SIZE - 2, 1.0f,
                    Color{ 175, 190, 215, 255 });
            }

            const string page = "第 " + to_string(adminBuffCursor + 1) + " / " +
                to_string(total) + " 个增益（已启用 "
                " 个）    宝物币 " + to_string(rogue.treasureCoins);
            DrawUtf8Centered(font, Utf8(page), cx, (float)GetScreenHeight() - 32.0f,
                UI_HINT_SIZE, Color{ 140, 150, 175, 255 });
        }

        else if (state == GameState::RANKING)
        {
            const RankLayout layout = MakeRankLayout();
            const char* tabLabel[7] = { "简单", "普通", "困难", "地狱", "神圣", "饥饿", "肉鸽" };
            const float cx = (float)GetScreenWidth() * 0.5f;
            const float rowTop = layout.tableTop + 44.0f;

            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                Color{ 10, 14, 28, 255 }, Color{ 34, 48, 92, 255 });

            DrawUtf8Centered(font, Utf8("记录排名"), cx, layout.titleY, UI_TITLE_SIZE, RAYWHITE);

            // 两个排行榜类型按钮：最高分榜 / 最快用时榜。
            const char* boardLabel[2] = { "最高分榜", "最快用时榜" };
            for (int i = 0; i < 2; ++i)
            {
                const bool selected =
                    (i == 0 && rankView == RankView::Score) ||
                    (i == 1 && rankView == RankView::Time);
                DrawFlatButton(font, layout.boardTabs[i], boardLabel[i], selected);
            }

            const char* hint = (rankView == RankView::Time)
                ? "按最快用时排名：用时短的在前，同用时分高的在前"
                : "按最高分排名：分数高的在前，同分用时短的在前";
            DrawUtf8Centered(font, Utf8(hint),
                cx, layout.subtitleY, UI_HINT_SIZE, Color{ 150, 160, 190, 255 });

            for (int i = 0; i < 7; ++i)
                DrawFlatButton(font, layout.tabs[i], tabLabel[i], rankTab == i);

            // 表头
            DrawTextEx(font, Utf8("名次").c_str(), Vector2{ layout.colX[0], layout.tableTop },
                (float)UI_TEXT_SIZE, 1.0f, GOLD);
            DrawTextEx(font, Utf8("用户名").c_str(), Vector2{ layout.colX[1], layout.tableTop },
                (float)UI_TEXT_SIZE, 1.0f, GOLD);
            DrawTextEx(font, Utf8("分数").c_str(), Vector2{ layout.colX[2], layout.tableTop },
                (float)UI_TEXT_SIZE, 1.0f, GOLD);
            DrawTextEx(font, Utf8("用时").c_str(), Vector2{ layout.colX[3], layout.tableTop },
                (float)UI_TEXT_SIZE, 1.0f, GOLD);
            DrawTextEx(font, Utf8("模式").c_str(), Vector2{ layout.colX[4], layout.tableTop },
                (float)UI_TEXT_SIZE, 1.0f, GOLD);

            DrawLine((int)layout.tableX, (int)(layout.tableTop + 38.0f),
                     (int)(layout.tableX + layout.tableW), (int)(layout.tableTop + 38.0f),
                     Fade(RAYWHITE, 0.35f));

            if (rankRows.empty())
            {
                DrawUtf8Centered(font, Utf8("暂无记录（通关一次就会记在这里）"), cx,
                    rowTop + 40.0f, UI_TEXT_SIZE - 4, Color{ 150, 160, 190, 255 });
            }
            else
            {
                const int shown = (int)rankRows.size();
                for (int i = 0; i < shown; ++i)
                {
                    const ScoreRecord& row = rankRows[i];
                    const float y = rowTop + (float)i * layout.rowH;
                    const Color color = (i == 0) ? GOLD : Color{ 210, 220, 235, 255 };

                    DrawTextEx(font, to_string(i + 1).c_str(),
                        Vector2{ layout.colX[0], y }, (float)UI_TEXT_SIZE - 2.0f, 1.0f, color);
                    DrawTextEx(font, row.name.c_str(),
                        Vector2{ layout.colX[1], y }, (float)UI_TEXT_SIZE - 2.0f, 1.0f, color);
                    DrawTextEx(font, to_string(row.score).c_str(),
                        Vector2{ layout.colX[2], y }, (float)UI_TEXT_SIZE - 2.0f, 1.0f, color);

                    char timeText[32];
                    GameTimer::Format(row.elapsed, timeText, sizeof(timeText));
                    DrawTextEx(font, timeText,
                        Vector2{ layout.colX[3], y }, (float)UI_TEXT_SIZE - 2.0f, 1.0f, color);

                    // 模式列：正常 / 奶龙
                    DrawTextEx(font,
                        (row.mode == GameMode::NAILONG ? Utf8("奶龙") : Utf8("正常")).c_str(),
                        Vector2{ layout.colX[4], y }, (float)UI_TEXT_SIZE - 2.0f, 1.0f, color);
                }
            }

            DrawFlatButton(font, layout.back, "返回", false);
        }

        else if (state == GameState::ROGUE_REWARD)
        {
            // ---- 肉鸽：层间增益选择弹窗 ----
            const int count = (int)rogue.offered.size();
            const float cx = (float)GetScreenWidth() * 0.5f;

            DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                Color{ 10, 14, 28, 255 }, Color{ 34, 48, 92, 255 });
            DrawUtf8Centered(font, Utf8("选择增益"), cx, 16.0f, UI_TITLE_SIZE, RAYWHITE);

            const string sub = "肉鸽 第 " + to_string(rogue.floor + 1) + "/" +
                to_string(ROGUE_FLOOR_COUNT) + " 层    " +
                "选 " + to_string(rogue.pickCount) + " 个（Enter 勾选，选满即生效）" +
                "    宝物币 " + to_string(rogue.treasureCoins) +
                "（稀有增益 1 个 / 个）";
            DrawUtf8Centered(font, Utf8(sub), cx, 66.0f, UI_HINT_SIZE + 2,
                Color{ 160, 175, 205, 255 });

            // 保底 / 说明那一行
            string rule;
            if (rogue.floor == ROGUE_SPECIAL_GUARANTEE_FLOOR_A ||
                rogue.floor == ROGUE_SPECIAL_GUARANTEE_FLOOR_B)
                rule = "本层必出一个特殊增益";
            else if (rogue.floor == ROGUE_RARE_GUARANTEE_FLOOR)
                rule = "本层必出一个稀有增益，并白送 1 个宝物";
            else
                rule = "一般增益免费，稀有增益要花 1 个宝物";
            DrawUtf8Centered(font, Utf8(rule), cx, 92.0f, UI_HINT_SIZE,
                Color{ 135, 148, 178, 255 });

            for (int i = 0; i < count; ++i)
            {
                const Buff b = rogue.offered[i];
                const Rectangle row = RogueOfferRow(i);
                const bool hot = CheckCollisionPointRec(GetMousePosition(), row);
                bool picked = false;
                for (int c : rogue.chosen) if (c == i) picked = true;
                const bool focus = (i == rogue.cursor) || hot;
                const int  tier = BuffTier(b);
                const int  cost = BuffCost(b);
                const bool affordable = (cost <= rogue.treasureCoins);
                // 稀有增益：金色描边；买不起的稀有：灰掉，勾不上
                const Color edge = (tier == 2)
                    ? (affordable ? ROGUE_RARE_TAG_COLOR : ROGUE_LOCKED_TAG_COLOR)
                    : (tier == 1 ? GOLD : Color{ 90, 130, 220, 255 });

                DrawRectangleRounded(row, 0.3f, 8,
                    picked ? Color{ 40, 120, 70, 235 }
                    : (focus ? Color{ 58, 96, 190, 235 } : Color{ 24, 34, 60, 235 }));
                DrawRectangleRoundedLinesEx(row, 0.3f, 8, 1.5f, edge);

                const string title = string(picked ? "[已选] " : "") + BuffName(b);
                DrawTextEx(font, Utf8(title).c_str(),
                    Vector2{ row.x + 18.0f, row.y + 2.0f }, (float)UI_TEXT_SIZE, 1.0f,
                    affordable ? RAYWHITE : ROGUE_LOCKED_TAG_COLOR);
                DrawTextEx(font, Utf8(tier == 2 ? "稀有" : (tier == 1 ? "特殊" : "一般")).c_str(),
                    Vector2{ row.x + row.width - 78.0f, row.y + 6.0f },
                    (float)UI_HINT_SIZE, 1.0f, edge);
                // 稀有增益标价：宝物 1 个（买不起就写「宝物不足」）
                if (cost > 0)
                {
                    const string price = "宝物 " + to_string(cost) +
                        (affordable ? "" : "（不足）");
                    DrawTextEx(font, Utf8(price).c_str(),
                        Vector2{ row.x + row.width - 178.0f, row.y + 6.0f },
                        (float)UI_HINT_SIZE, 1.0f,
                        affordable ? ROGUE_RARE_TAG_COLOR : ROGUE_LOCKED_TAG_COLOR);
                }
                DrawTextEx(font, Utf8(picked ? "已勾选，再按一次取消" : BuffDesc(b)).c_str(),
                    Vector2{ row.x + 18.0f, row.y + 26.0f }, (float)UI_HINT_SIZE, 1.0f,
                    Color{ 175, 190, 215, 255 });
            }

            // 刷新按钮：全局只有 ROGUE_REFRESH_TIMES 次
            const bool canRefresh = rogue.refreshLeft > 0;
            DrawFlatButton(font, RogueRefreshRect(),
                canRefresh ? ("刷新（剩 " + to_string(rogue.refreshLeft) + " 次）").c_str()
                           : "刷新（已用完）", false);
            DrawUtf8Centered(font, Utf8("刷新会重抽本层候选；已选过 / 已拥有的增益不会再出现"),
                (float)GetScreenWidth() - 125.0f, 142.0f, UI_HINT_SIZE - 4,
                Color{ 135, 148, 178, 255 });

            DrawUtf8Centered(font, Utf8("W/S 或 上下键 选择    Enter / 左键 勾选    R 刷新    ESC 放弃本局"),
                cx, (float)GetScreenHeight() - 40.0f, UI_HINT_SIZE,
                Color{ 140, 150, 175, 255 });
        }

        // 踩雷红色全屏闪烁（时长参数 MINE_FLASH_SECONDS，后期可调）
        if (mineFlashTimer > 0.0f)
        {
            const float flashAlpha = (mineFlashTimer / MINE_FLASH_SECONDS) * 0.45f;
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(RED, flashAlpha));
        }

        // 致盲（踩雷 / 饱腹耗尽）：整块窗口全黑，音效照常播放；
        // 死亡结算 / 暂停菜单时不遮挡
        const auto BlindAlpha = [](float left, float total) -> float
            {
                if (left <= 0.0f || total <= 0.0f) return 0.0f;
                if (left > total - MINE_BLIND_FADE) return (total - left) / MINE_BLIND_FADE;
                if (left < MINE_BLIND_FADE)         return left / MINE_BLIND_FADE;
                return 1.0f;
            };
        const float blindAlpha = max(BlindAlpha(mineBlindTimer, MINE_BLIND_SECONDS),
                                     BlindAlpha(hunger.blindTimer, HUNGER_BLIND_SECONDS));
        if (blindAlpha > 0.0f && state == GameState::PLAYING &&
            playerHealth > 0 && !escaped)
        {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                Fade(BLACK, blindAlpha));
        }

        EndDrawing();
    }

    light.Unload();

    // 退出前停止后台线程
    if (enemyManager) enemyManager->Stop();

    // 先卸载普通音效，再卸载脚步声音；脚步模块负责最后关闭音频设备。
    sfxAudio.Unload();
    footstepAudio.Unload();

    nailong.reset();          // 卸载奶龙（模型 / 程序化都一样）

    CloseWindow();
    return 0;
}
