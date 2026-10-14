#pragma once
#include "Config.h"
#include "raylib.h"
#include <string>

// 单行文本输入框：支持中文（按 UTF-8 码点追加）、退格、回车确认、光标闪烁。
// 登录页的用户名和管理员密码都用它。
class TextInput
{
public:
    void Clear();

    // 处理本帧输入；返回 true 表示这一帧按下了回车
    bool Update(int maxChars = RANK_NAME_MAX_CHARS);

    // 画输入框 + 已输入文本 + 闪烁光标
    void Draw(Font font, Rectangle box, const char* gbkPlaceholder, int fontSize) const;

    const std::string& GetText() const { return text; }   // 已是 UTF-8
    bool IsEmpty() const { return text.empty(); }
    void SetText(const std::string& utf8Text) { text = utf8Text; }

private:
    std::string text;      // UTF-8
    float blink = 0.0f;    // 光标闪烁计时
};
