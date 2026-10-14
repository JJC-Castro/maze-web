// ============================================================
//  TPCamera.cpp —— 第三人称环绕相机实现
//  坐标系：+Y 朝上，角色水平朝向 yaw 对应 forward = (sin yaw, 0, cos yaw)
// ============================================================
#include "TPCamera.h"
#include "Maze.h"
#include "raymath.h"

#include <cmath>

namespace
{
    constexpr float kDeg2Rad = 0.01745329f;

    // 角度归一到 [-180, 180]，避免长时间转圈后数值越来越大
    float WrapDeg(float deg)
    {
        while (deg >  180.0f) deg -= 360.0f;
        while (deg < -180.0f) deg += 360.0f;
        return deg;
    }
}

float TPCamera::Clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 注视点：脚底往上抬到胸口（太矮会像贴地，太高角色会跑出画面）
Vector3 TPCamera::FocusPoint(Vector3 feet)
{
    return Vector3{ feet.x, feet.y + TP_CAM_LOOK_Y, feet.z };
}

// 注视点 -> 机位的单位向量：水平分量落在角色后方，竖直分量随俯仰角抬升
Vector3 TPCamera::OffsetDir(float yaw, float pitch)
{
    const float y = yaw * kDeg2Rad;
    const float p = pitch * kDeg2Rad;
    return Vector3{ -sinf(y) * cosf(p), sinf(p), -cosf(y) * cosf(p) };
}

// 与帧率无关的指数平滑系数
float TPCamera::SmoothK(float speed, float dt)
{
    return 1.0f - expf(-speed * dt);
}

// 机位是否埋在墙里；高过墙顶就不算挡——俯视视角可以直接越过墙体
bool TPCamera::Blocked(const Maze& maze, const Vector3& p) const
{
    if (p.y > GROUND_Y + WALL_HEIGHT) return false;

    const float pad = TP_CAM_COLLIDE_PAD;
    return maze.IsWorldPosBlocked(p) ||
           maze.IsWorldPosBlocked(Vector3{ p.x + pad, p.y, p.z }) ||
           maze.IsWorldPosBlocked(Vector3{ p.x - pad, p.y, p.z }) ||
           maze.IsWorldPosBlocked(Vector3{ p.x, p.y, p.z + pad }) ||
           maze.IsWorldPosBlocked(Vector3{ p.x, p.y, p.z - pad });
}

// 沿当前角度从注视点往外逐点试探，返回最后一个不撞墙的距离
float TPCamera::FreeDistance(const Maze& maze, const Vector3& from,
                             float yaw, float pitch) const
{
    const Vector3 dir = OffsetDir(yaw, pitch);

    for (float d = TP_CAM_COLLIDE_STEP; d <= distMax; d += TP_CAM_COLLIDE_STEP)
    {
        if (Blocked(maze, Vector3Add(from, Vector3Scale(dir, d))))
            return fmaxf(0.0f, d - TP_CAM_COLLIDE_STEP);
    }
    return distMax;
}

// 滚轮缩放：只改「期望距离上限」，实际距离仍会被墙壁挤近
void TPCamera::Zoom(float steps)
{
    if (steps == 0.0f) return;
    distMax = Clampf(distMax + steps * TP_CAM_ZOOM_STEP, TP_CAM_ZOOM_MIN, TP_CAM_ZOOM_MAX);
}

void TPCamera::Reset(Vector3 feet, float yaw)
{
    yawDeg   = WrapDeg(yaw);
    pitchDeg = TP_CAM_PITCH_DEFAULT;
    curDist  = distMax;
    focus    = FocusPoint(feet);
    pos      = Vector3Add(focus, Vector3Scale(OffsetDir(yawDeg, pitchDeg), curDist));
    if (pos.y < GROUND_Y + 0.30f) pos.y = GROUND_Y + 0.30f;

    const Vector3 look = Vector3Subtract(focus, pos);
    aimDir = (Vector3Length(look) > 0.0001f) ? Vector3Normalize(look)
                                             : Vector3{ 0.0f, 0.0f, 1.0f };

    cam.position   = pos;
    cam.target     = focus;
    cam.up         = Vector3{ 0.0f, 1.0f, 0.0f };
    cam.fovy       = TP_CAM_FOVY;
    cam.projection = CAMERA_PERSPECTIVE;
    inited = true;
}

void TPCamera::SnapTo(Vector3 feet)
{
    if (!inited)
    {
        Reset(feet, yawDeg);
        return;
    }

    focus = FocusPoint(feet);
    pos   = Vector3Add(focus, Vector3Scale(OffsetDir(yawDeg, pitchDeg), curDist));
    if (pos.y < GROUND_Y + 0.30f) pos.y = GROUND_Y + 0.30f;

    cam.position = pos;
    cam.target   = focus;
}

void TPCamera::Update(float dt, Vector3 feet, const Maze& maze, bool mouseLocked)
{
    if (dt <= 0.0f) dt = 1.0f / 60.0f;
    if (!inited)
    {
        Reset(feet, yawDeg);
        return;
    }

    // ---- 1. 鼠标：左右环绕 + 上下俯仰 ----
    if (mouseLocked)
    {
        const Vector2 md = GetMouseDelta();
        yawDeg -= md.x * TP_CAM_SENS_DEG;
        pitchDeg += md.y * TP_CAM_SENS_DEG;     // 鼠标往下 -> 机位抬高，俯视角色
        yawDeg = WrapDeg(yawDeg);
        pitchDeg = Clampf(pitchDeg, TP_CAM_PITCH_MIN, TP_CAM_PITCH_MAX);
    }

    // ---- 2. 注视点平滑：跳跃时镜头不会猛点头 ----
    const Vector3 wantFocus = FocusPoint(feet);
    const float kFocus = SmoothK(TP_CAM_FOCUS_SMOOTH, dt);
    focus.x += (wantFocus.x - focus.x) * kFocus;
    focus.y += (wantFocus.y - focus.y) * kFocus;
    focus.z += (wantFocus.z - focus.z) * kFocus;

    // ---- 3. 避墙：先按当前角度求最远距离；被挤到极限就抬高机位翻过墙顶 ----
    float pitchUse = pitchDeg;
    float free = FreeDistance(maze, focus, yawDeg, pitchUse);
    for (int i = 0; i < TP_CAM_LIFT_TRIES &&
                    free < TP_CAM_MIN_DIST && pitchUse < TP_CAM_PITCH_MAX; ++i)
    {
        pitchUse = Clampf(pitchUse + TP_CAM_LIFT_STEP,
                          TP_CAM_PITCH_MIN, TP_CAM_PITCH_MAX);
        free = FreeDistance(maze, focus, yawDeg, pitchUse);
    }
    const float wantDist = Clampf(free, TP_CAM_MIN_DIST, distMax);

    // 回缩要快（防穿墙），伸出要慢（防止离墙时忽然弹开）
    const float distSpeed = (wantDist < curDist) ? TP_CAM_IN_SPEED : TP_CAM_OUT_SPEED;
    curDist += (wantDist - curDist) * SmoothK(distSpeed, dt);

    // ---- 4. 机位平滑 ----
    const Vector3 wantPos =
        Vector3Add(focus, Vector3Scale(OffsetDir(yawDeg, pitchUse), curDist));
    const float kPos = SmoothK(TP_CAM_POS_SMOOTH, dt);
    pos.x += (wantPos.x - pos.x) * kPos;
    pos.y += (wantPos.y - pos.y) * kPos;
    pos.z += (wantPos.z - pos.z) * kPos;
    if (pos.y < GROUND_Y + 0.30f) pos.y = GROUND_Y + 0.30f;   // 别钻地

    // ---- 5. 组装相机 ----
    cam.position   = pos;
    cam.target     = focus;
    cam.up         = Vector3{ 0.0f, 1.0f, 0.0f };
    cam.fovy       = TP_CAM_FOVY;
    cam.projection = CAMERA_PERSPECTIVE;

    const Vector3 look = Vector3Subtract(focus, pos);
    aimDir = (Vector3Length(look) > 0.0001f) ? Vector3Normalize(look)
                                             : Vector3{ 0.0f, 0.0f, 1.0f };
}
