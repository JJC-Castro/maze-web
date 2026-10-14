#pragma once
#include "raylib.h"
#include "Config.h"
#include <cmath>

class Maze;   // 前向声明

class Player
{
public:
    float walkSpeed = WALKSPEED; 
    float sprintSpeed = SPRINTSPEED;
    float sprintMax = SPRINT_MAX_SECONDS;      // 冲刺能量上限（秒），满能量可加速这么久
    float sprintRecover = SPRINT_RECOVER_SECONDS;  // 能量从空回满所需时间（秒）  
    float jumpForce = JUMPFORCE;  
    float gravity = GRAVITY;  
    float eyeHeight = EYE_HEIGHT;    
    float halfWidth = HALFWIDTH;  
    float groundY = GROUND_Y;     
    float mouseSens = MOUSESENS;  
    float pitchLimit = PITCH_LIM; 
    // ---- 管理员模式 ----
    bool  flyEnabled = false;                   // 开着才能飞（管理员模式）
    bool  infiniteStamina = false;              // 冲刺能量无限
bool  flying = false;                       // 是否处于飞行模式（管理员：长按 ADMIN_FLY_KEY 起飞）
    bool  mouseLookEnabled = true;              // 关掉后鼠标不再转动视角（Tab 解锁鼠标时用）
    // ---- 肉鸽增益：直接作用在玩家身上的三个开关 ----
    bool  hoverUnlocked = false;                // 肉鸽特殊增益「滞空」：空中长按 Space 悬停
    bool  doubleJumpUnlocked = false;           // 「二段跳」：空中还能再跳一次
    bool  sprintDisabled = false;               // 「锁血」的代价：不能跑步
    bool  phaseThroughWalls = false;            // 「穿墙」：无视墙体碰撞直接平移
    float flySpeed = ADMIN_FLY_SPEED;           // 飞行升降速度 (m/s)
    float flyMaxHeight = ADMIN_FLY_MAX_HEIGHT;  // 飞行最高高度 (m)
float flyHoldTime = 0.0f;                   // 起飞键已经按住多久（长按才起飞，见 ADMIN_FLY_HOLD）

    explicit Player(Vector3 spawnPos);
    ~Player() = default;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Update 改签名
    void Update(float dt, const Maze& maze);

    // RL 自动模式：不读鼠标/键盘，由外部给定水平移动方向（管理员模式下的机器人用）。
    // 仍然走 TryMoveAxis 撞墙检测与重力/落地，不绕过游戏物理。
    // wantSprint=true 时走与键盘玩家完全相同的冲刺规则（Shift）
    void UpdateAutoMove(float dt, const Maze& maze, Vector3 moveDir,
                        bool wantSprint = false);


    void UpdateRotation(Vector2 mouseDelta);                 // 仅更新 yaw/pitch
    void UpdateMovement(float dt);                          // 仅更新水平速度
    void UpdateJumpAndGravity(float dt);                    // 仅更新竖直速度
    void Integrate(float dt);                               // 仅积分位置
    void ResolveGroundCollision();                          // 仅做地面碰撞

    Camera3D GetCamera() const;             // 生成当前帧的 3D 相机
    Vector3 GetPosition() const;            // 眼睛位置（即相机位置）
    Vector3 GetFeetPosition() const;        // 脚部位置
    Vector3 GetVelocity() const;
    Vector3 GetMoveDir() const;             // 本帧移动输入方向（归一化；没有输入时为零向量）
    Vector3 GetForward() const;             // 视线方向（含俯仰）
    Vector3 GetRight() const;               // 水平右方向
    bool    IsGrounded() const;
    BoundingBox GetBoundingBox() const;     // 当前帧 AABB

    float GetStamina() const;               // 剩余可加速秒数
    float GetStaminaRatio() const;          // 能量比例 0~1
    bool  IsSprinting() const;              // 本帧是否在加速
    bool  IsOnSlowZone() const;             // 本帧是否踩在减速带上
    void  ResetStamina();                   // 能量回满（重开时用）

    void SetPosition(Vector3 pos);          // 设置眼睛位置
    void SetFeetPosition(Vector3 feet);     // 按脚部位置设置
    void PushOutOfWall(const Maze& maze);    // 落到墙里时挪回最近的通路格（管理员飞行用）
    void SetVelocity(Vector3 v);

    // ---- 姿态（欧拉角）----
    // roll 只影响相机 up 向量（视觉），不参与水平移动；pitch 受 pitchLimit 限制。
    void  SetOrientation(float yawRad, float pitchRad, float rollRad);
    float GetYaw() const { return yaw; }
    float GetPitch() const { return pitch; }
    float GetRoll() const { return roll; }
    // 以 rate(rad/s) 把 pitch/roll 平滑收回 0（RL 随机角度开局后的"起身"）
    void  LevelOut(float dt, float rate);
    void LookAt(Vector3 target);            // 让视线指向世界坐标点（自动解算 yaw/pitch）
    // ---- 第三人称：移动方向以相机水平朝向为基准，瞄准交给相机 ----
    void  SetMoveYawFromCamera(bool on, float yawRad);   // 移动基准换成相机朝向
    void  SetYaw(float y);                               // 设置角色朝向（弧度）
    void  SetAimDir(Vector3 dir);                        // GetForward 改用相机视线
    void  ClearAimDir();                                 // 恢复用自身 yaw/pitch

private:
    Vector3 pos{ 0.0f, 0.0f, 0.0f };  // 眼睛（相机）位置
    Vector3 velocity{ 0.0f, 0.0f, 0.0f };
    Vector3 moveDirInput{ 0.0f, 0.0f, 0.0f };   // 本帧方向键输入方向（不受撞墙影响，给角色转身用）
    float   yaw = 0.0f;                // 绕 Y 轴转角
    float   pitch = 0.0f;                // 绕右轴转角
    float   roll = 0.0f;               // 绕视线轴滚转（随机开局 / 视觉）
    bool    isGrounded = false;
    bool    onSlowZone = false;            // 本帧是否踩在减速带上（减速带内禁跳、禁加速）
    float   slowZoneFactor = 1.0f;         // 本帧减速带速度倍率（不在减速带为 1）
    int     airJumpsLeft = 0;              // 二段跳：空中还剩几次跳跃
    float   stamina = SPRINT_MAX_SECONDS;  // 冲刺能量（按秒计）
    bool    sprinting = false;             // 本帧是否正在加速
    bool    sprintExhausted = false;       // 本次按住 Shift 已耗尽，松开前不再加速

    void BuildDirections(Vector3& forward, Vector3& right) const;
    void BuildDirectionsFor(float y, Vector3& forward, Vector3& right) const;
    // ---- 第三人称相关状态 ----
    bool    moveFromCamera = false;         // 移动基准是否换成相机朝向
    float   cameraMoveYaw = 0.0f;           // 相机水平朝向（弧度）
    bool    aimOverride = false;            // GetForward 是否用相机视线
    Vector3 aimDir{ 0.0f, 0.0f, 1.0f };     // 相机视线方向

    // 冲刺/能量结算：UpdateMovement 与 UpdateAutoMove 共用同一套规则，
    // 保证 RL 机器人 4 m/s 走 / 8 m/s 跑 与真人完全一致。
    void UpdateSprintState(float dt, bool wantSprint, bool hasInput);
    bool TryMoveAxis(float dx, float dz, const Maze& maze);
};



