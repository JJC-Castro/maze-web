#pragma once
#include "raylib.h"
#include "Config.h"
#include "NailongRig.h"    // 软件层骨架（标定 + CPU 蒙皮）

class Maze;
class Player;

// ============================================================
//  奶龙（第三人称主角）
//  · 模型：优先加载 NAILONG_MODEL_PATH（glb）；文件不存在/加载失败时
//          自动回落到程序化建模（黄身白肚 + 深色四肢，球体拼装）。
//  · 动画：Idle / Walk / Run / Jump / Shoot / Laugh
//          优先级 Shoot > Jump > Run > Walk > Idle，Laugh 为一次性覆盖。
//  · 真机模型动画名可能带前缀（如 Armature|Idle），加载时按“子串匹配”，
//    名字用 Config.h 里的 NAILONG_ANIM_* 宏替换即可。
// ============================================================

enum class NailongAnim
{
    Idle = 0,
    Walk,
    Run,
    Jump,
    Shoot,
    Laugh,
    Count
};

// 每帧从 main.cpp 喂进来的角色状态
struct NailongState
{
    Vector3 feet{ 0.0f, 0.0f, 0.0f };   // 脚底世界坐标
    float   yaw = 0.0f;                 // 角色朝向（弧度，与 Player 的 yaw 一致）
    bool    grounded = true;            // 是否踩地
    bool    flying = false;             // 管理员飞行中
    bool    moving = false;             // 是否有水平移动输入
    float   verticalSpeed = 0.0f;      // 竖直速度（m/s，上升为正，用于区分起跳/下落）
    bool    sprinting = false;          // 是否在奔跑
    bool    shootTriggered = false;     // 本帧开火
    bool    laughTriggered = false;     // 本帧按了 F
};

class Nailong
{
public:
    Nailong();
    ~Nailong();

    Nailong(const Nailong&) = delete;
    Nailong& operator=(const Nailong&) = delete;

    void Load();                        // 加载 glb（失败则程序化建模）
    void Unload();

    void Update(float dt, const NailongState& state);
    void Draw() const;                  // 在世界坐标处画当前帧角色

    const char* CurrentAnimName() const;
    NailongAnim CurrentAnim() const { return anim; }
    bool        UsingModel() const { return useModel; }
    void        PrintDebugInfo() const; // 打印动画清单 / 是否回落
    void        ApplySidecarTexture();  // 把独立 PNG 贴图挂到模型材质上（glb 里不带贴图）

private:
    // ---- 关节姿态：定义在 NailongRig.h，程序化奶龙与软件骨架共用 ----
    using Pose = NailongPose;

    void        UpdateModelAnim(float dt);          // glb 骨骼动画推进
    static Pose MakeIdle(float t);
    static Pose MakeWalk(float t);
    static Pose MakeRun(float t);
    static Pose MakeJump(const NailongState& s);
    static Pose MakeShoot(float t);
    static Pose MakeLaugh(float t);
    static void BlendPose(Pose& cur, const Pose& target, float k);
    void        DrawProcedural() const;
    static void DrawLegProcedural(float side, float swing, float knee);
    static void DrawArmProcedural(float side, float swing, float fore);

    // ---- glb 资源 ----
    Model model{ 0 };
    Texture2D sideTexture{ 0 };              // 独立贴图，需随模型一起卸载
    bool  useModel = false;
    ModelAnimation* animations = nullptr;    // 本版 raylib 的动画是单独加载的，不挂在 Model 上
    int   animationCount = 0;
    int   animIndex[(int)NailongAnim::Count] = { -1, -1, -1, -1, -1, -1 };
    float animFrame = 0.0f;                  // 播放进度（帧，带小数 -> 支持插值）

    // 换动作时的交叉淡入淡出（UpdateModelAnimationEx 双动画混合）
    int   blendFrom = -1;                    // 上一段动画下标
    float blendTimer = 0.0f;
    float blendFrame = 0.0f;

    // ---- 状态机 ----
    NailongAnim anim = NailongAnim::Idle;
    float       animTime = 0.0f;        // 当前状态已经播了多久
    float       shootTimer = 0.0f;      // Shoot 剩余时间
    float       laughTimer = 0.0f;      // Laugh 剩余时间
    Pose        pose{};                 // 当前姿态（插值结果）
    // ---- 软件层骨架（静态 glb 也能走跑跳）----
    SoftRig     rig;

    Vector3     drawFeet{ 0.0f, 0.0f, 0.0f };
    float       modelFeetLift = 0.0f;    // 按模型静态 bbox 算出的脚底补偿（m），见 Load
    float       drawYaw = 0.0f;
};