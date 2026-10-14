#pragma once
#include "raylib.h"
#include "Maze.h"

class Player;   // 前向声明

namespace Treasure
{
    // 本帧拾取结果（分类计数）
    struct PickupResult
    {
        int score = 0;
        int attack = 0;
        int heal = 0;
    };

    // 拾取玩家脚下的宝物；返回分类计数
    PickupResult Collect(Maze& maze, const Player& player);

    // 重新投放（清空上一局宝物，再按数量重撒）
    void Reset(Maze& maze, int scoreCount, int attackCount, int healCount);

    // 绘制所有宝物（三种颜色 + 上下浮动）
    void Draw(const Maze& maze);

    // 辅助：判断某类型是不是宝物
    inline bool IsTreasure(CellType t)
    {
        return t == CellType::TREASURE_SCORE ||
            t == CellType::TREASURE_ATTACK ||
            t == CellType::TREASURE_HEAL;
    }
}