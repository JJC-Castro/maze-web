#pragma once
#include "raylib.h"
#include "Config.h"

class Maze;

// ============================================================
//  TPCamera —— 第三人称环绕相机（独立封装，可复用到别的 3D 场景）
//
//  · 鼠标左右 = 绕角色环绕，鼠标上下 = 俯仰（默认 52 度俯视）
//  · 角色被墙挡住时先把相机拉近；拉到底还是挡，就抬高机位翻过墙顶，
//    绝不会把镜头塞进角色身体里（避免"糊成一坨"看不清奶龙）
//  · 机位 / 注视点 / 距离三者各做指数平滑，与帧率无关，转动足够顺滑
//  · 相机高过墙顶后不再判定撞墙，所以俯视视角在窄走廊里也很顺
//
//  典型用法：
//      TPCamera cam;
//      cam.Reset(feet, 0.0f);                      // 进游戏时复位一次
//      cam.Update(dt, feet, maze, cursorLocked);   // 每帧更新
//      Camera3D c = cam.GetCamera();
//      // 移动方向以 cam.GetYawDeg() 为基准；射击方向取 cam.GetAimDir()
// ============================================================
class TPCamera
{
public:
    void    Reset(Vector3 feet, float yawDeg);      // 复位：机位立刻贴合，角度回到默认
    void    SnapTo(Vector3 feet);                   // 只贴合位置，保留当前角度/距离

    // 每帧更新；mouseLocked = false 时不吃鼠标输入（暂停 / Tab 解锁鼠标）
    void    Update(float dt, Vector3 feet, const Maze& maze, bool mouseLocked);
    void    Zoom(float steps);                     // 鼠标滚轮调距离：steps = 滚轮格数

    const Camera3D& GetCamera() const { return cam; }
    float   GetYawDeg() const { return yawDeg; }    // 相机水平朝向（度）
    float   GetPitchDeg() const { return pitchDeg; }// 俯仰角（度，正数=俯视）
    Vector3 GetFocus() const { return focus; }      // 注视点（角色胸口附近）
    Vector3 GetAimDir() const { return aimDir; }    // 视线方向（单位向量）
    float   GetDist() const { return curDist; }     // 当前相机距离（调试用）
    bool    IsReady() const { return inited; }      // 是否已经初始化过

private:
    static Vector3 FocusPoint(Vector3 feet);
    static Vector3 OffsetDir(float yawDeg, float pitchDeg);
    static float   SmoothK(float speed, float dt);
    static float   Clampf(float v, float lo, float hi);
    bool  Blocked(const Maze& maze, const Vector3& p) const;
    float FreeDistance(const Maze& maze, const Vector3& from, float yawDeg, float pitchDeg) const;

    float   yawDeg   = 0.0f;                    // 环绕角（度）
    float   pitchDeg = TP_CAM_PITCH_DEFAULT;    // 俯仰角（度），正数 = 机位在上方
    float   curDist  = TP_CAM_DIST;             // 平滑后的距离
    float   distMax  = TP_CAM_DIST;             // 期望距离上限（滚轮可调，夹在 ZOOM_MIN/MAX）
    Vector3 focus{ 0.0f, 0.0f, 0.0f };          // 平滑后的注视点
    Vector3 pos{ 0.0f, 0.0f, 0.0f };            // 平滑后的机位
    Vector3 aimDir{ 0.0f, 0.0f, 1.0f };         // 视线方向
    bool    inited = false;
    Camera3D cam{ 0 };
};
