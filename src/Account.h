#pragma once
#include "Config.h"
#include <string>
#include <vector>

// 排行榜类型：最高分榜 / 最快用时榜。
enum class RankBoard
{
    Score,
    Time,
    Legacy      // 旧版 log.csv 没有榜单列，读取时按旧记录处理
};

// 一条通关记录：只有"走到出口"才会写进 log.csv，中途退出 / 死亡都不记
struct ScoreRecord
{
    std::string name;                       // 用户名（UTF-8，未登录为 "游客"）
    Difficulty  difficulty = Difficulty::NORMAL;
    GameMode    mode = GameMode::NORMAL;    // 本场模式（正常 / 奶龙），两种模式记录同一张榜一起比
    int         score = 0;                  // 本场得分
    double      elapsed = 0.0;              // 本场用时（秒）
    std::string date;                       // 记录时间 "YYYY-MM-DD HH:MM:SS"
    RankBoard   board = RankBoard::Legacy;  // 这条记录属于哪个排行榜
};

// log.csv 读写 + 排名：四个难度分别排名
namespace Account
{
    const char* LogPath();                  // log.csv 绝对路径（固定在游戏根目录）
    std::string NowText();                  // 当前时间字符串

    // 提交一条通关记录。
    // 同名 + 同难度 只保留成绩最好的一条（分数高优先，同分用时短优先），
    // 新成绩不如旧的则什么都不写。返回 true 表示 log.csv 被更新。
    bool SubmitRecord(const ScoreRecord& rec);

    // 读取 log.csv 里的全部记录（文件不存在时返回空）
    std::vector<ScoreRecord> LoadAll();

    // 取某个难度的指定排行榜：Score 按最高分排序，Time 按最快用时排序，最多 maxCount 条
    // 排名不区分模式：正常模式与奶龙模式的记录同一张榜、同一套排序一起比较
    std::vector<ScoreRecord> RankByDifficulty(
        Difficulty d,
        RankBoard board,
        int maxCount
    );
}