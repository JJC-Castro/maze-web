#pragma once
#include "raylib.h"
#include "Maze.h"
#include <string>

class Player;

// 一局游戏的完整存档（迷宫 + 玩家 + 分数 + 计时）
namespace GameSave
{
    // 存档结构：从当前游戏状态拷贝出来的数据
    struct Data
    {
        // 玩家
        Vector3 playerPos{ 0, 0, 0 };   // 眼睛位置
        float   playerYaw = 0.0f;
        float   playerPitch = 0.0f;

        // 游戏数据
        int   playerHealth = 0;
        int   playerMaxHealth = 0;
        int   playerAttacksRemaining = 0;
        int   score = 0;
        double elapsed = 0.0;          // 游戏已用时间（秒）
    };

    // 保存：迷宫 + 玩家 + 游戏数据
    bool Save(const char* path, const Maze& maze, const Player& player, const Data& data);

    // 加载：迷宫 + 玩家 + 游戏数据
    // @param maze / player / outData 都被填充
    bool Load(const char* path, Maze& maze, Player& player, Data& outData);
}