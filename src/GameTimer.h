#pragma once
#include "raylib.h"

// 游戏计时器：一局游戏的用时统计 + HUD 显示，支持暂停 / 继续
class GameTimer
{
public:
    GameTimer();

    void Start();                   // 归零并开始计时（开局 / 重开）
    void Pause();                   // 暂停（时间冻结）
    void Resume();                  // 继续计时
    void Reset();                   // 归零并停止
    void Update();                  // 每帧调用一次（内部按 GetFrameTime 累加）
    void Update(double dt);         // 手动指定步长（便于测试）

    bool   IsRunning() const { return running; }
    double GetElapsed() const { return elapsed; }   // 已用秒数
    void AddElapsed(double seconds);                // 直接加时间（肉鸽「跳层」的 +150s 罚时）
    const char* GetText() const;                    // "MM:SS.mmm"（内部缓冲，可直接用）
    static const char* Format(double seconds, char* buf, int bufSize);

    void SetPrefix(const char* gbkText);            // HUD 前缀（GBK），默认 "用时 "
    void SetFont(Font f) { font = f; }
    void DrawHUD(int x, int y, int fontSize, Color color) const;   // 绘制 "用时 MM:SS.mmm"

private:
    double elapsed = 0.0;
    bool   running = false;
    Font   font{};
    char   prefix[48] = "";                         // 已经是 UTF-8
    mutable char text[32] = "00:00.000";
};
