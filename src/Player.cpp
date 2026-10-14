#include "Player.h"
#include "raymath.h"
#include "Maze.h"   
#include <cmath>

Player::Player(Vector3 spawnPos)
    : pos(spawnPos)
{
}

void Player::Update(float dt, const Maze& maze)
{
    if (mouseLookEnabled)               // Tab 解锁鼠标时不再转视角
        UpdateRotation(GetMouseDelta());

    // 减速带判定：按脚底所在格（管理员飞行模式不受影响）
    onSlowZone = !flying && maze.IsSlowZoneAt(GetFeetPosition());
    slowZoneFactor = onSlowZone ? maze.slowZoneFactor : 1.0f;

    UpdateMovement(dt);
    UpdateJumpAndGravity(dt);

    // 分轴移动 + 碰撞；管理员飞行模式无视墙壁，可直接飞越墙顶
    if (flying)
    {
        pos.x += velocity.x * dt;
        pos.z += velocity.z * dt;
    }
    else if (phaseThroughWalls)
    {
        // 肉鸽增益「穿墙」：无视墙体碰撞，直接平移（墙顶飞不过去，只是穿过去）
        pos.x += velocity.x * dt;
        pos.z += velocity.z * dt;
    }
    else
    {
        if (!TryMoveAxis(velocity.x * dt, 0.0f, maze)) velocity.x = 0.0f;
        if (!TryMoveAxis(0.0f, velocity.z * dt, maze)) velocity.z = 0.0f;
    }

    // Y 轴
    pos.y += velocity.y * dt;
    ResolveGroundCollision();

    // 管理员飞行：限高，别飞出地图外
    if (flying && pos.y > flyMaxHeight)
    {
        pos.y = flyMaxHeight;
        if (velocity.y > 0.0f) velocity.y = 0.0f;
    }

    // 管理员飞行无视墙体，落地时可能正好嵌在墙里 —— 自动挪到最近的通路格，别卡住
    if (!flying && !phaseThroughWalls) PushOutOfWall(maze);
}
void Player::UpdateAutoMove(float dt, const Maze& maze, Vector3 moveDir,
                            bool wantSprint)
{
    moveDir.y = 0.0f;
    const float len = sqrtf(moveDir.x * moveDir.x + moveDir.z * moveDir.z);

    if (len > 1e-4f)
    {
        moveDir.x /= len;
        moveDir.z /= len;
        yaw = atan2f(moveDir.x, moveDir.z);   // 朝向即前进方向
        pitch = 0.0f;                          // 保持水平视野
    }
    else
    {
        moveDir.x = moveDir.z = 0.0f;
    }

    // 冲刺/能量走同一套规则，速度与键盘玩家一致
    UpdateSprintState(dt, wantSprint, len > 1e-4f);

    const float moveSpeed = sprinting ? sprintSpeed : walkSpeed;
    velocity.x = moveDir.x * moveSpeed;
    velocity.z = moveDir.z * moveSpeed;

    UpdateJumpAndGravity(dt);

    if (phaseThroughWalls)
    {
        pos.x += velocity.x * dt;
        pos.z += velocity.z * dt;
    }
    else
    {
        if (!TryMoveAxis(velocity.x * dt, 0.0f, maze)) velocity.x = 0.0f;
        if (!TryMoveAxis(0.0f, velocity.z * dt, maze)) velocity.z = 0.0f;
    }

    pos.y += velocity.y * dt;
    ResolveGroundCollision();

    if (flying && pos.y > flyMaxHeight)
    {
        pos.y = flyMaxHeight;
        if (velocity.y > 0.0f) velocity.y = 0.0f;
    }
}

void Player::UpdateRotation(Vector2 mouseDelta)
{
    yaw -= mouseDelta.x * mouseSens;
    pitch -= mouseDelta.y * mouseSens;
    if (pitch > pitchLimit) pitch = pitchLimit;
    if (pitch < -pitchLimit) pitch = -pitchLimit;
}

void Player::BuildDirections(Vector3& forward, Vector3& right) const
{
    forward = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };
    right = {
        sinf(yaw - PI / 2.0f),
        0.0f,
        cosf(yaw - PI / 2.0f)
    };
}

// 用给定的水平朝向构造前进 / 右方向（第三人称：基准来自相机而不是角色自身）
void Player::BuildDirectionsFor(float y, Vector3& forward, Vector3& right) const
{
    forward = { sinf(y), 0.0f, cosf(y) };
    right = { sinf(y - PI / 2.0f), 0.0f, cosf(y - PI / 2.0f) };
}

void Player::UpdateMovement(float dt)
{
    Vector3 forward, right;
    if (moveFromCamera) BuildDirectionsFor(cameraMoveYaw, forward, right);
    else                BuildDirections(forward, right);

    Vector3 moveDir = { 0.0f, 0.0f, 0.0f };
    if (IsKeyDown(KEY_W)) moveDir = Vector3Add(moveDir, forward);
    if (IsKeyDown(KEY_S)) moveDir = Vector3Subtract(moveDir, forward);
    if (IsKeyDown(KEY_D)) moveDir = Vector3Add(moveDir, right);
    if (IsKeyDown(KEY_A)) moveDir = Vector3Subtract(moveDir, right);
    moveDir.y = 0.0f;

    const bool hasInput = Vector3Length(moveDir) > 0.01f;
    if (hasInput)
        moveDir = Vector3Normalize(moveDir);

    // 记下输入方向：撞墙时速度会被清零，但朝向仍然要跟着方向键转
    moveDirInput = hasInput ? moveDir : Vector3{ 0.0f, 0.0f, 0.0f };

    // 冲刺：Shift + 有移动输入 + 能量>0 才能加速，否则按走速
    // （与 UpdateAutoMove 共用 UpdateSprintState，RL 与真人速度一致）
    const bool wantSprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    UpdateSprintState(dt, wantSprint, hasInput);

    // 减速带上按倍率降速（普通难度 0.5 倍，倍率由难度配置）
    const float moveSpeed = (sprinting ? sprintSpeed : walkSpeed) * slowZoneFactor;
    velocity.x = moveDir.x * moveSpeed;
    velocity.z = moveDir.z * moveSpeed;
}

// 冲刺/能量结算（UpdateMovement 与 UpdateAutoMove 的唯一实现）
// 规则：按住 Shift 且有输入且能量>0 才加速；一旦耗尽，松开 Shift 前不再加速；
// 没在加速就按 sprintMax/sprintRecover 的速度回能（5 s 冲刺 / 15 s 回满）。
void Player::UpdateSprintState(float dt, bool wantSprint, bool hasInput)
{
    if (infiniteStamina)
    {
        stamina = sprintMax;
        sprintExhausted = false;
    }

    if (!wantSprint) sprintExhausted = false;
    if (wantSprint && stamina <= 0.0f) sprintExhausted = true;

    // 减速带上不允许加速：强制不进入冲刺状态
    // 「锁血」增益的代价：整局不能跑步（Shift 按住也只按走速走）
    sprinting = !sprintDisabled && wantSprint && hasInput && stamina > 0.0f &&
        !sprintExhausted && !onSlowZone;

    if (sprinting)
    {
        if (!infiniteStamina)
        {
            stamina -= dt;
            if (stamina < 0.0f) stamina = 0.0f;
        }
    }
    else
    {
        stamina += sprintMax / sprintRecover * dt;
        if (stamina > sprintMax) stamina = sprintMax;
    }
}

void Player::UpdateJumpAndGravity(float dt)
{
    // 管理员专属：长按 ADMIN_FLY_KEY(Q) 才起飞，轻点一下仍然是普通跳跃
    if (flyEnabled)
    {
        // 管理员：长按 Q 超过 ADMIN_FLY_HOLD 才起飞，轻点一下仍然是普通跳跃，
        // 所以走 / 跑 / 跳的手感和肉鸽正常模式完全一致。起飞期间 Z 上升 / X 下降，
        // 一松手立刻断掉飞行（脚下有重力，自然落回地面）。
        if (IsKeyDown(ADMIN_FLY_KEY))
        {
            flyHoldTime += dt;
            if (flyHoldTime >= ADMIN_FLY_HOLD) flying = true;
        }
        else
        {
            flyHoldTime = 0.0f;
            flying = false;
        }

        if (flying)
        {
            float rise = 0.0f;
            if (IsKeyDown(KEY_X)) rise += flySpeed;
            if (IsKeyDown(KEY_Z)) rise -= flySpeed;
            velocity.y = rise;
            isGrounded = false;
            return;
        }
    }
    else
    {
        flying = false;      // 退出管理员模式时自动取消飞行
        flyHoldTime = 0.0f;
    }

    // 肉鸽特殊增益「滞空」：人在空中、按住 Space 就在原地挂住（垂直速度清零），
    // 长按可以一直悬着；松手立刻恢复重力。只在"已经开始下落"时接管，
    // 所以轻点还是完整的普通跳跃，等跳到最高点才挂住。
    if (hoverUnlocked && !isGrounded && IsKeyDown(KEY_SPACE) && velocity.y <= 0.0f)
    {
        velocity.y = 0.0f;
        return;
    }


    if (isGrounded) airJumpsLeft = 0;      // 落地回满二段跳次数

    // 减速带上不允许起跳
    if (IsKeyPressed(KEY_SPACE) && !onSlowZone)
    {
        if (isGrounded)
        {
            velocity.y = jumpForce;
            isGrounded = false;
            // 「二段跳」增益：解锁后顺手留一次空中跳
            airJumpsLeft = doubleJumpUnlocked ? 1 : 0;
        }
        else if (airJumpsLeft > 0)          // 空中第二跳
        {
            velocity.y = jumpForce;
            --airJumpsLeft;
        }
    }
    velocity.y += gravity * dt;
}

void Player::Integrate(float dt)
{
    pos = Vector3Add(pos, Vector3Scale(velocity, dt));
}

void Player::ResolveGroundCollision()
{
    float feetY = pos.y - eyeHeight;
    if (feetY <= groundY && velocity.y <= 0.0f)
    {
        pos.y = groundY + eyeHeight;
        velocity.y = 0.0f;
        isGrounded = true;
    }
    else
    {
        isGrounded = (feetY <= groundY + 1e-4f);
    }
}

Camera3D Player::GetCamera() const
{
    Camera3D cam = { 0 };
    cam.position = pos;
    cam.up = { 0.0f, 1.0f, 0.0f };
    cam.fovy = 70.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    Vector3 fwd = GetForward();
    cam.target = Vector3Add(pos, fwd);

    // 滚转：把相机 up 绕视线轴转 roll（roll == 0 时与原来完全一致）
    if (roll != 0.0f)
    {
        const Vector3 up0 = { 0.0f, 1.0f, 0.0f };
        Vector3 xaxis = Vector3CrossProduct(fwd, up0);   // 与 raylib 内部一致的相机右轴
        const float xlen = sqrtf(xaxis.x * xaxis.x + xaxis.y * xaxis.y + xaxis.z * xaxis.z);
        if (xlen > 1e-4f)
        {
            xaxis = Vector3Scale(xaxis, 1.0f / xlen);
            const Vector3 yaxis = Vector3Normalize(Vector3CrossProduct(xaxis, fwd));
            cam.up = Vector3Normalize(Vector3Add(Vector3Scale(yaxis, cosf(roll)),
                                                 Vector3Scale(xaxis, sinf(roll))));
        }
    }
    return cam;
}

Vector3 Player::GetPosition() const { return pos; }
Vector3 Player::GetVelocity() const { return velocity; }
Vector3 Player::GetMoveDir() const { return moveDirInput; }
bool    Player::IsGrounded() const { return isGrounded; }

Vector3 Player::GetFeetPosition() const
{
    return { pos.x, pos.y - eyeHeight, pos.z };
}

Vector3 Player::GetForward() const
{
    if (aimOverride) return aimDir;      // 第三人称：视线交给环绕相机
    return {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };
}

Vector3 Player::GetRight() const
{
    return { sinf(yaw - PI / 2.0f), 0.0f, cosf(yaw - PI / 2.0f) };
}

BoundingBox Player::GetBoundingBox() const
{
    return BoundingBox{
        { pos.x - halfWidth, pos.y - eyeHeight, pos.z - halfWidth },
        { pos.x + halfWidth, pos.y,             pos.z + halfWidth }
    };
}

// SETTINGS
void Player::SetPosition(Vector3 p)
{
    pos = p;
}

void Player::SetFeetPosition(Vector3 feet)
{
    pos = { feet.x, feet.y + eyeHeight, feet.z };
}

void Player::SetVelocity(Vector3 v)
{
    velocity = v;
}

// ---- 第三人称接口 ----
void Player::SetMoveYawFromCamera(bool on, float yawRad)
{
    moveFromCamera = on;
    cameraMoveYaw = yawRad;
}

void Player::SetYaw(float y)
{
    yaw = y;
}


void Player::SetAimDir(Vector3 dir)
{
    const float len = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (len < 0.0001f) { ClearAimDir(); return; }
    aimDir = { dir.x / len, dir.y / len, dir.z / len };
    aimOverride = true;
}

void Player::ClearAimDir()
{
    aimOverride = false;
}

void Player::SetOrientation(float yawRad, float pitchRad, float rollRad)
{
    yaw = yawRad;
    pitch = pitchRad;
    if (pitch > pitchLimit) pitch = pitchLimit;
    if (pitch < -pitchLimit) pitch = -pitchLimit;
    roll = rollRad;
}

void Player::LevelOut(float dt, float rate)
{
    const float step = rate * dt;
    if (roll > step) roll -= step;
    else if (roll < -step) roll += step;
    else roll = 0.0f;

    if (pitch > step) pitch -= step;
    else if (pitch < -step) pitch += step;
    else pitch = 0.0f;
}

void Player::LookAt(Vector3 target)
{
    Vector3 d = Vector3Subtract(target, pos);
    yaw = atan2f(d.x, d.z);
    float horiz = sqrtf(d.x * d.x + d.z * d.z);
    pitch = atan2f(d.y, horiz);
    if (pitch > pitchLimit) pitch = pitchLimit;
    if (pitch < -pitchLimit) pitch = -pitchLimit;
}
float    Player::GetStamina() const { return stamina; }
float    Player::GetStaminaRatio() const { return (sprintMax > 0.0f) ? (stamina / sprintMax) : 1.0f; }
bool     Player::IsSprinting() const { return sprinting; }
bool     Player::IsOnSlowZone() const { return onSlowZone; }
void     Player::ResetStamina() { stamina = sprintMax; sprinting = false; sprintExhausted = false; }
// 和 TryMoveAxis 用同一套"四角判定"：四角只要有一个落在墙里，就算嵌住了
static bool PlayerBlockedAt(const Maze& maze, float x, float z, float y, float halfWidth)
{
    const float xs[2] = { x - halfWidth, x + halfWidth };
    const float zs[2] = { z - halfWidth, z + halfWidth };
    for (float px : xs)
        for (float pz : zs)
            if (maze.IsWorldPosBlocked(Vector3{ px, y, pz })) return true;
    return false;
}

// 卡在墙里自救：从脚下这一格往外一圈圈找最近一个"四角都站得住"的格子中心，
// 找到就直接平移过去（管理员飞行落地嵌进墙里、穿墙结束、被传送进墙里都能救回来）。
void Player::PushOutOfWall(const Maze& maze)
{
    if (!PlayerBlockedAt(maze, pos.x, pos.z, pos.y, halfWidth)) return;

    int gx = 0;
    int gy = 0;
    maze.WorldToGrid(Vector3{ pos.x, pos.y, pos.z }, gx, gy);

    // 就近原则：先找第 1 圈，找不到再找第 2 圈，绝不为了更近而跳过外圈
    for (int r = 1; r <= 6; ++r)
    {
        float bestDist = 0.0f;
        float bestX = pos.x;
        float bestZ = pos.z;
        bool found = false;

        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                if (dx > -r && dx < r && dy > -r && dy < r) continue;   // 只看最外圈

                const Vector3 c = maze.GridToWorld(gx + dx, gy + dy);
                if (PlayerBlockedAt(maze, c.x, c.z, pos.y, halfWidth)) continue;

                const float ox = c.x - pos.x;
                const float oz = c.z - pos.z;
                const float dist = ox * ox + oz * oz;
                if (!found || dist < bestDist)
                {
                    found = true;
                    bestDist = dist;
                    bestX = c.x;
                    bestZ = c.z;
                }
            }
        }

        if (found)
        {
            pos.x = bestX;
            pos.z = bestZ;
            velocity.x = 0.0f;
            velocity.z = 0.0f;
            return;
        }
    }
}


//撞墻代碼
bool Player::TryMoveAxis(float dx, float dz, const Maze& maze)
{
    float nx = pos.x + dx;
    float nz = pos.z + dz;

    float xs[2] = { nx - halfWidth, nx + halfWidth };
    float zs[2] = { nz - halfWidth, nz + halfWidth };

    for (float x : xs)
        for (float z : zs)
            if (maze.IsWorldPosBlocked({ x, pos.y, z }))
                return false;

    pos.x = nx;
    pos.z = nz;
    return true;
}