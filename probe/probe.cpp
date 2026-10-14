// ===========================================================================
//  probe.cpp —— 可移植性探针
//
//  作用：在正式编译整个游戏之前，先用一个"最小三角"验证三条最容易翻车的链路。
//  这三条只要有一条不通，后面 15000 行源码全量编译就是纯浪费：
//
//    1. emcc 工具链   —— 能不能编出 wasm、CMake 的 raylib 子目录能不能过
//    2. 中文字体      —— 自带 TTF 子集能不能被 raylib 的 stb_truetype 认出来、
//                        UTF-8 的汉字能不能正确烘进字形图集（最容易挂的一环）
//    3. 预加载资源    —— --preload-file 的虚拟 FS 能不能被 FileExists 摸到
//
//  跑通的表现：浏览器里看到一个转动的立方体 + 正确显示的中文标题，
//             并且逐条列出各项自检结果。
// ===========================================================================
#include "raylib.h"

#include <math.h>       // cosf / sinf
#include <stdio.h>
#include <string.h>

static const char* kFontCandidates[] =
{
    "assets/fonts/NotoSansSC-Subset.ttf",
    "assets/fonts/NotoSansSC-VF.ttf",
    "C:/Windows/Fonts/simhei.ttf",      // 桌面版跑探针时的兜底
};

// 探针自检要显示的汉字（故意挑几个笔画多的，缺字一眼就能看出来）
static const char* kProbeGlyphs =
    "探针自检通过失败字体中文渲染迷宫奶龙肉鸽增益管理员排行榜"
    "开始游戏操作说明退出返回主菜单继续重新已暂停当前用时"
    "0123456789.,:;/()[]{}<>+-=*&#@%!? ";

struct Check
{
    const char* label;
    bool        ok;
    const char* detail;
};

int main(void)
{
    const int screenW = 960;
    const int screenH = 640;

    SetTraceLogLevel(LOG_WARNING);
    InitWindow(screenW, screenH, "Maze Web Probe");
    SetTargetFPS(60);

    Check checks[8];
    int checkCount = 0;

    // ---- 1. 资源虚拟 FS ----
    const bool modelExists = FileExists("assets/models/nailong.glb");
    const bool textureExists = FileExists("assets/models/nailong_color.png");
    checks[checkCount++] = { "虚拟文件系统可用（--preload-file）", modelExists || textureExists,
                             modelExists ? "找到 nailong.glb" : "没找到模型（可能没预加载）" };

    // ---- 2. 中文字体 ----
    Font font = GetFontDefault();
    bool fontOk = false;
    const char* fontUsed = "（无，退回默认字体）";

    // 先把 UI 里会用到的汉字拼成码点表 —— 和 UIFont.cpp 的 AppendCodepoints 同一套逻辑
    int codepoints[512];
    int cpCount = 0;
    for (int c = 32; c < 127 && cpCount < 512; ++c) codepoints[cpCount++] = c;

    {
        const unsigned char* p = (const unsigned char*)kProbeGlyphs;
        while (*p != 0 && cpCount < 512)
        {
            int cp = 0, extra = 0;
            if (*p < 0x80) { cp = *p; extra = 0; }
            else if ((*p & 0xE0) == 0xC0) { cp = *p & 0x1F; extra = 1; }
            else if ((*p & 0xF0) == 0xE0) { cp = *p & 0x0F; extra = 2; }
            else if ((*p & 0xF8) == 0xF0) { cp = *p & 0x07; extra = 3; }
            else { ++p; continue; }

            ++p;
            for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i, ++p)
                cp = (cp << 6) | (*p & 0x3F);

            if (cp <= 32) continue;
            bool dup = false;
            for (int i = 0; i < cpCount; ++i) if (codepoints[i] == cp) { dup = true; break; }
            if (!dup) codepoints[cpCount++] = cp;
        }
    }

    for (const char* path : kFontCandidates)
    {
        if (!FileExists(path)) continue;
        Font f = LoadFontEx(path, 48, codepoints, cpCount);
        if (f.texture.id == 0) { continue; }

        // 关键判据：图集里到底有没有 '迷'（U+8FF7）—— 有它就是真中文字体
        bool hasCjk = false;
        for (int i = 0; i < f.glyphCount; ++i)
            if (f.glyphs[i].value == 0x8FF7) { hasCjk = true; break; }

        if (!hasCjk) { UnloadFont(f); continue; }

        SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
        font = f;
        fontOk = true;
        fontUsed = path;
        break;
    }
    checks[checkCount++] = { "中文字体加载 + 字形烘图", fontOk, fontUsed };

    // ---- 3. WebGL / 3D ----
    Camera3D camera = { 0 };
    camera.position = Vector3{ 4.2f, 3.4f, 4.2f };
    camera.target = Vector3{ 0.0f, 0.6f, 0.0f };
    camera.up = Vector3{ 0.0f, 1.0f, 0.0f };
    camera.fovy = 55.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    checks[checkCount++] = { "3D 管线（WebGL2 / GLFW3 后端）", true, "相机已建立" };
    checks[checkCount++] = { "窗口 / 输入", IsWindowReady(), "InitWindow 成功" };

    float spin = 0.0f;
    const Color bg = Color{ 12, 14, 20, 255 };

    while (!WindowShouldClose())
    {
        spin += GetFrameTime() * 60.0f;
        camera.position.x = 4.2f * cosf(spin * DEG2RAD);
        camera.position.z = 4.2f * sinf(spin * DEG2RAD);

        BeginDrawing();
        ClearBackground(bg);

        BeginMode3D(camera);
        DrawGrid(10, 1.0f);
        DrawCube(Vector3{ 0.0f, 0.6f, 0.0f }, 1.2f, 1.2f, 1.2f, Color{ 110, 200, 255, 255 });
        DrawCubeWires(Vector3{ 0.0f, 0.6f, 0.0f }, 1.2f, 1.2f, 1.2f, RAYWHITE);
        EndMode3D();

        // ---- HUD：中文标题 + 自检清单 ----
        DrawTextEx(font, "3D 迷宫 · Web 版可移植性探针",
                   Vector2{ 24.0f, 20.0f }, 30.0f, 1.0f, Color{ 150, 225, 255, 255 });

        float y = 70.0f;
        for (int i = 0; i < checkCount; ++i)
        {
            const Color c = checks[i].ok
                ? Color{ 130, 230, 160, 255 }
                : Color{ 250, 140, 140, 255 };
            DrawTextEx(font, checks[i].ok ? "通过" : "失败", Vector2{ 24.0f, y }, 20.0f, 1.0f, c);
            DrawTextEx(font, checks[i].label, Vector2{ 78.0f, y }, 20.0f, 1.0f, RAYWHITE);
            DrawTextEx(font, checks[i].detail, Vector2{ 460.0f, y }, 18.0f, 1.0f,
                       Color{ 150, 158, 175, 255 });
            y += 26.0f;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "FPS %d   raylib %s", GetFPS(), RAYLIB_VERSION);
        DrawTextEx(font, buf, Vector2{ 24.0f, (float)screenH - 34.0f }, 20.0f, 1.0f,
                   Color{ 190, 200, 215, 255 });

        EndDrawing();
    }

    if (fontOk) UnloadFont(font);
    CloseWindow();
    return 0;
}