#pragma once
#include "raylib.h"
#include <string>
#include <vector>

// 源码里的中文是 GBK（Visual Studio 默认编码），raylib 需要 UTF-8。
// 所有要显示的中文都先经过 Utf8() 转换，再交给 Menu / GameTimer / DrawTextEx。
std::string Utf8(const char* gbkText);
std::string Utf8(const std::string& gbkText);

// 中文界面字体：依次尝试系统字体，全部失败时退回 raylib 默认字体（中文会显示成 '?'）
class UIFont
{
public:
    // glyphs: 界面上出现的所有文字（GBK），可打印 ASCII 会自动补齐
    bool Load(int fontSize, const char* glyphs);
    bool LoadFrom(const char* filePath, int fontSize, const char* glyphs);
    void Unload();

    Font Get() const;                               // 未加载时返回默认字体

    // 字体里是否已经有这个码点（判断用户名等新字要不要补进字体）
    bool HasCodepoint(int codepoint) const;

    // 用原来的字体文件 + 额外码点重建字体；返回 false 表示重载失败（继续用旧字体）
    bool ReloadWith(const std::vector<int>& extraCodepoints);
    bool IsLoaded() const { return loaded; }
    const char* GetPath() const { return path; }    // 实际使用的字体文件

private:
    Font font{};
    bool loaded = false;
    int  loadSize = 0;                  // 加载时的字号（重载补字用）
    std::vector<int> baseCodepoints;    // 当前字体里已有的码点
    char path[260] = "";
};
