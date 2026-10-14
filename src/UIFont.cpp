// ===========================================================================
//  UIFont.cpp —— 中文界面字体 + 文本编码转换
//
//  编码约定：两个平台调用方写的是同一句话 Utf8("开始游戏")，差别只在这里。
//    · 桌面版：源码是 GBK（VS 中文版默认），用 MultiByteToWideChar 转 UTF-8
//    · Web 版：源码已被 tools/gbk2utf8.py 转成 UTF-8，Utf8() 原样返回
//
//  字体来源：两个平台不一样，这是移植时最容易踩的坑。
//    · 桌面版：探测系统字体（等线 / 黑体 / 雅黑 / 宋体），Windows 自带
//    · Web 版：浏览器里没有"系统字体文件"这种东西，必须自带一个 TTF，
//              否则中文全变 '?'。见 assets/fonts/ 与 tools/make_font_subset.py
// ===========================================================================
#include "UIFont.h"

#include <cstdio>
#include <string>
#include <string.h>
#include <vector>

#ifndef MAZE_WEB
// 桌面版：GBK -> UTF-8 要调 MultiByteToWideChar，需要 windows.h。
// 必须放在 raylib.h 之前并屏蔽 user32 / GDI，否则会和 raylib 的
// CloseWindow() / ShowCursor() / Rectangle 撞名。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
//  候选字体（按优先级）
// ---------------------------------------------------------------------------
static const char* kFontCandidates[] =
{
#ifdef MAZE_WEB
    // Web 版只能自带字体。NotoSansSC 先按 Config.h 的 UI_GLYPHS 裁过子集，
    // 只留界面真正会用到的汉字，体积从 10MB 降到几百 KB。
    "assets/fonts/NotoSansSC-Subset.ttf",
#else
    "C:/Windows/Fonts/Deng.ttf",     // 等线
    "C:/Windows/Fonts/simhei.ttf",   // 黑体
    "C:/Windows/Fonts/msyh.ttc",     // 微软雅黑
    "C:/Windows/Fonts/simsun.ttc",   // 宋体
#endif
};

// ---------------------------------------------------------------------------
//  Utf8()：把源码里的字面量转成 raylib 能吃的 UTF-8
// ---------------------------------------------------------------------------
#ifdef MAZE_WEB

// Web：源码就是 UTF-8，直接原样返回（零成本，也不会有转换失败的可能）
std::string Utf8(const char* text) { return std::string(text != nullptr ? text : ""); }
std::string Utf8(const std::string& text) { return text; }

// UTF-8 字节流 -> 码点序列（给 LoadFontEx 的码点表用，替代桌面版的 GBK 版本）
static void AppendCodepoints(std::vector<int>& cps, const char* text)
{
    if (text == nullptr) return;

    const unsigned char* p = (const unsigned char*)text;
    while (*p != 0)
    {
        int cp = 0;
        int extra = 0;
        if (*p < 0x80) { cp = *p; extra = 0; }
        else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
        else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
        else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
        else { ++p; continue; }                     // 非法起始字节：跳过，别死循环

        ++p;
        for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i, ++p)
            cp = (cp << 6) | (*p & 0x3F);

        if (cp <= 32 || cp == 127) continue;

        bool dup = false;
        for (int v : cps) if (v == cp) { dup = true; break; }
        if (!dup) cps.push_back(cp);
    }
}

#else   // ---------------- 桌面版：GBK ----------------

// GBK -> UTF-16
static std::wstring ToWide(const char* text)
{
    if (!text || !*text) return std::wstring();
    int len = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (len <= 1) return std::wstring();
    std::wstring w((size_t)len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, -1, &w[0], len);
    w.pop_back();                       // 去掉结尾的 '\0'
    return w;
}

std::string Utf8(const char* gbkText)
{
    const std::wstring w = ToWide(gbkText);
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string(gbkText);
    std::string u((size_t)len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &u[0], len, nullptr, nullptr);
    return u;
}

std::string Utf8(const std::string& gbkText) { return Utf8(gbkText.c_str()); }

// 收集 GBK 文本里出现的所有字符（码点），跳过 ASCII 与重复项
static void AppendCodepoints(std::vector<int>& cps, const char* gbkText)
{
    for (wchar_t wc : ToWide(gbkText))
    {
        const int cp = (int)wc;
        if (cp <= 32 || cp == 127) continue;
        bool dup = false;
        for (int v : cps) if (v == cp) { dup = true; break; }
        if (!dup) cps.push_back(cp);
    }
}

#endif

// ---------------------------------------------------------------------------
//  字体
// ---------------------------------------------------------------------------
static bool FontHasCodepoint(Font f, int cp)
{
    for (int i = 0; i < f.glyphCount; ++i)
        if (f.glyphs[i].value == cp) return true;
    return false;
}

bool UIFont::LoadFrom(const char* filePath, int fontSize, const char* glyphs)
{
    if (loaded) Unload();
    if (!filePath || !FileExists(filePath)) return false;

    std::vector<int> cps;
    cps.reserve(320);
    for (int c = 32; c < 127; ++c) cps.push_back(c);    // 可打印 ASCII
    AppendCodepoints(cps, glyphs);

    Font f = LoadFontEx(filePath, fontSize, cps.data(), (int)cps.size());
    if (f.texture.id == 0) return false;                // 加载失败
    if (!FontHasCodepoint(f, 0x5F00))                   // 没有 '开' -> 不是中文字体
    {
        UnloadFont(f);
        return false;
    }

    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    font = f;
    loaded = true;
    loadSize = fontSize;
    baseCodepoints = cps;
    snprintf(path, sizeof(path), "%s", filePath);       // 不用 strncpy_s，它在非 MSVC 上不存在
    return true;
}

// 字体里是否已经有这个码点
bool UIFont::HasCodepoint(int codepoint) const
{
    if (!loaded) return false;
    return FontHasCodepoint(font, codepoint);
}

// 用同一个字体文件 + 额外码点重建字体（显示用户名 / 历史记录里的中文名）
bool UIFont::ReloadWith(const std::vector<int>& extraCodepoints)
{
    if (!loaded || path[0] == 0 || loadSize <= 0) return false;

    std::vector<int> cps = baseCodepoints;
    bool added = false;

    for (int cp : extraCodepoints)
    {
        if (cp <= 32 || cp == 127) continue;
        if (FontHasCodepoint(font, cp)) continue;

        bool dup = false;
        for (int v : cps) if (v == cp) { dup = true; break; }
        if (dup) continue;

        cps.push_back(cp);
        added = true;
    }

    if (!added) return true;                        // 不缺字，不用重载

    Font f = LoadFontEx(path, loadSize, cps.data(), (int)cps.size());
    if (f.texture.id == 0) return false;

    SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
    UnloadFont(font);
    font = f;
    baseCodepoints = cps;
    return true;
}

bool UIFont::Load(int fontSize, const char* glyphs)
{
    for (const char* candidate : kFontCandidates)
        if (LoadFrom(candidate, fontSize, glyphs)) return true;

    TraceLog(LOG_WARNING, "UIFont: no CJK font found, fallback to raylib default font");
    return false;
}

void UIFont::Unload()
{
    if (!loaded) return;
    UnloadFont(font);
    font = Font{};
    loaded = false;
    path[0] = '\0';
}

Font UIFont::Get() const
{
    return loaded ? font : GetFontDefault();
}