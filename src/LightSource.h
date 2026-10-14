#pragma once
#include "raylib.h"
#include "Config.h"

// ============================================================
// 光源 / 视野限制封装库（LightSource）
// ------------------------------------------------------------
// 作用：以玩家为光源中心，半径 LIGHT_RADIUS_METERS 米内正常可见，
//       超出后逐渐压黑，并随距离做径向模糊（越远越糊）。
//
// 用法（只在需要的难度启用，例如"地狱"）：
//     LightSource light;
//     light.Init(GetScreenWidth(), GetScreenHeight());
//     ...
//     if (light.IsReady())
//     {
//         light.BeginCapture(playerPos);   // 离屏 + 距离压黑
//         DrawScene3D(...);                // 内部照常 BeginMode3D / DrawCube
//         light.EndCapture();
//         light.Draw();                    // 后处理（模糊 + 暗角）画到屏幕
//     }
//     else
//     {
//         DrawScene3D(...);                // shader 不可用：退回普通渲染
//     }
//     ...
//     light.Unload();
//
// 注意：距离是在 shader 里按"世界坐标"算的，所以场景里请直接用世界坐标画
//       （不要用 rlTranslatef / rlRotatef 之类的模型变换），否则压黑位置会偏。
// ============================================================
class LightSource
{
public:
    void Init(int width, int height);   // 建离屏缓冲 + 编译 shader

    // Window/browser resize: rebuild the capture RT when the target size changes
    void EnsureSize(int w, int h);
    void Unload();                      // 释放资源

    // shader / 离屏缓冲是否可用；不可用时调用方应退回普通渲染
    bool IsReady() const { return ready; }

    // 光照半径（米），默认取 Config.h 的 LIGHT_RADIUS_METERS
    void  SetRadius(float meters);
    float GetRadius() const { return radius; }

    // 把 3D 场景画进离屏缓冲：BeginCapture -> 画场景 -> EndCapture
    void BeginCapture(Vector3 playerPos);
    void EndCapture();

    // 把离屏缓冲做后处理画满屏（在 BeginDrawing/EndDrawing 之间调用）
    void Draw();

private:
    RenderTexture2D target{};
    Shader fogShader{};      // 3D：按到玩家的世界距离压黑
    Shader blurShader{};     // 2D 后处理：越靠画面外侧越模糊 + 压暗
    bool  ready = false;
    float radius = LIGHT_RADIUS_METERS;
    int   width = 0;
    int   height = 0;

    // shader uniform 位置（-1 = 该 uniform 不存在）
    int fogLightPosLoc = -1;
    int fogRadiusLoc = -1;
    int fogInnerLoc = -1;
    int fogDarknessLoc = -1;
    int blurTexSizeLoc = -1;
    int blurAmountLoc = -1;
    int blurEdgeLoc = -1;
    int blurInnerLoc = -1;
};