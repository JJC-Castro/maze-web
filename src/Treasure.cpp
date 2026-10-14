#include "Treasure.h"
#include "Player.h"     // Collect 要用 GetFeetPosition
#include "Config.h"

// ============================================================
// 拾取：吃掉脚下宝物，按类型分类计数
// ============================================================
Treasure::PickupResult Treasure::Collect(Maze& maze, const Player& player)
{
    PickupResult result;
    const Vector3 feet = player.GetFeetPosition();

    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
        {
            const CellType t = maze.grid.at(x, y);
            if (!IsTreasure(t)) continue;

            const Vector3 tPos = maze.GridToWorld(x, y);
            const float dx = feet.x - tPos.x;
            const float dz = feet.z - tPos.z;

            if (dx * dx + dz * dz >= 0.6f * 0.6f) continue;

            // 吃掉：还原成通路 + 分类计数
            maze.grid.at(x, y) = CellType::PATH;
            maze.treasureCount--;

            switch (t)
            {
            case CellType::TREASURE_SCORE:  ++result.score;  break;
            case CellType::TREASURE_ATTACK: ++result.attack; break;
            case CellType::TREASURE_HEAL:   ++result.heal;   break;
            default: break;
            }
        }

    return result;
}

// ============================================================
// 重置：先清空，再重撒
// ============================================================
void Treasure::Reset(Maze& maze, int scoreCount, int attackCount, int healCount)
{
    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
            if (IsTreasure(maze.grid.at(x, y)))
                maze.grid.at(x, y) = CellType::PATH;

    maze.SpawnTreasures(scoreCount, attackCount, healCount);
}

// ============================================================
// 绘制：三种颜色
// ============================================================
void Treasure::Draw(const Maze& maze)
{
    const float bob = sinf((float)GetTime() * 3.0f) * 0.08f;

    for (int y = 0; y < maze.grid.height; ++y)
        for (int x = 0; x < maze.grid.width; ++x)
        {
            const CellType t = maze.grid.at(x, y);
            if (!IsTreasure(t)) continue;

            Color color = GOLD;
            switch (t)
            {
            case CellType::TREASURE_SCORE:  color = GOLD;    break;  // 金：加分
            case CellType::TREASURE_ATTACK: color = SKYBLUE; break;  // 蓝：加攻击
            case CellType::TREASURE_HEAL:   color = RED;     break;  // 红：回血
            default: break;
            }

            const Vector3 pos = maze.GridToWorld(x, y);
            const Vector3 center = { pos.x, 0.6f + bob, pos.z };

            DrawCube(center, 0.4f, 0.4f, 0.4f, color);
            DrawCubeWires(center, 0.4f, 0.4f, 0.4f, DARKGRAY);
        }
}