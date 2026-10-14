#include "GameTimer.h"
#include "UIFont.h"
#include <stdio.h>
#include <string.h>

static const double kMaxSeconds = 359999.999;   // 99:59:59.999 上限

GameTimer::GameTimer()
{
    SetPrefix("用时 ");
}

void GameTimer::Start()  { elapsed = 0.0; running = true; }
void GameTimer::Pause()  { running = false; }
void GameTimer::Resume() { running = true; }
void GameTimer::Reset()  { elapsed = 0.0; running = false; }

void GameTimer::Update() { Update((double)GetFrameTime()); }

// 直接往计时里加时间：肉鸽稀有增益「跳层」的罚时用（+150s），
// 加成之后仍然夹在 99:59:59.999 上限内，显示逻辑不用改。
void GameTimer::AddElapsed(double seconds)
{
    if (seconds <= 0.0) return;
    elapsed += seconds;
    if (elapsed > kMaxSeconds) elapsed = kMaxSeconds;
}

void GameTimer::Update(double dt)
{
    if (!running || dt <= 0.0) return;
    elapsed += dt;
    if (elapsed > kMaxSeconds) elapsed = kMaxSeconds;
}

const char* GameTimer::Format(double seconds, char* buf, int bufSize)
{
    if (!buf || bufSize < 10) return "";
    if (seconds < 0.0) seconds = 0.0;
    if (seconds > kMaxSeconds) seconds = kMaxSeconds;

    const int totalMs = (int)(seconds * 1000.0 + 0.5);
    const int totalSec = totalMs / 1000;
    snprintf(buf, (size_t)bufSize, "%02d:%02d.%03d", totalSec / 60, totalSec % 60, totalMs % 1000);
    return buf;
}

const char* GameTimer::GetText() const
{
    return Format(elapsed, text, (int)sizeof(text));
}

void GameTimer::SetPrefix(const char* gbkText)
{
    const std::string u = Utf8(gbkText);
    snprintf(prefix, sizeof(prefix), "%s", u.c_str());   // strncpy_s/_TRUNCATE 非 MSVC 没有
}

void GameTimer::DrawHUD(int x, int y, int fontSize, Color color) const
{
    const Font f = (font.texture.id != 0) ? font : GetFontDefault();
    char line[96];
    snprintf(line, sizeof(line), "%s%s", prefix, GetText());
    DrawTextEx(f, line, Vector2{ (float)x, (float)y }, (float)fontSize, 1.0f, color);
}
