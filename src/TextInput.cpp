#include "TextInput.h"
#include "UIFont.h"

#include <cmath>

namespace
{
    // 把一个 UTF-8 码点追加到字符串（自动编码成 1~4 字节）
    void AppendCodepoint(std::string& text, int cp)
    {
        if (cp < 32 || cp == 127) return;
        if (cp == ',') return;                  // 逗号会破坏 CSV，直接忽略

        if (cp < 0x80)
        {
            text.push_back((char)cp);
        }
        else if (cp < 0x800)
        {
            text.push_back((char)(0xC0 | (cp >> 6)));
            text.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else if (cp < 0x10000)
        {
            text.push_back((char)(0xE0 | (cp >> 12)));
            text.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            text.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            text.push_back((char)(0xF0 | (cp >> 18)));
            text.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            text.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            text.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    int CountChars(const std::string& text)
    {
        int count = 0;
        for (unsigned char c : text)
            if ((c & 0xC0) != 0x80) ++count;
        return count;
    }

    void PopCodepoint(std::string& text)
    {
        while (!text.empty())
        {
            const unsigned char c = (unsigned char)text.back();
            text.pop_back();
            if ((c & 0xC0) != 0x80) break;      // 抹到一个起始字节为止
        }
    }
}

void TextInput::Clear()
{
    text.clear();
    blink = 0.0f;
}

bool TextInput::Update(int maxChars)
{
    blink += GetFrameTime();

    int cp = GetCharPressed();
    while (cp > 0)
    {
        if (CountChars(text) < maxChars) AppendCodepoint(text, cp);
        cp = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE))
        PopCodepoint(text);

    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER);
}

void TextInput::Draw(Font font, Rectangle box, const char* gbkPlaceholder, int fontSize) const
{
    DrawRectangleRec(box, Fade(BLACK, 0.55f));
    DrawRectangleLinesEx(box, 2.0f, Color{ 90, 130, 220, 255 });

    const float padX = 14.0f;
    const float textY = box.y + (box.height - (float)fontSize) * 0.5f - 2.0f;
    const bool empty = text.empty();
    const std::string shown = empty ? Utf8(gbkPlaceholder) : text;

    DrawTextEx(font, shown.c_str(), Vector2{ box.x + padX, textY },
        (float)fontSize, 1.0f, empty ? Fade(RAYWHITE, 0.45f) : RAYWHITE);

    // 闪烁光标
    if (fmodf(blink, 1.0f) > 0.5f) return;

    const float caretX = (empty
        ? box.x + padX
        : box.x + padX + MeasureTextEx(font, shown.c_str(), (float)fontSize, 1.0f).x) + 3.0f;
    DrawRectangle((int)caretX, (int)(box.y + 10.0f), 2, (int)box.height - 20, RAYWHITE);
}