// ============================================================
//  Nailong.cpp —— 第三人称主角“奶龙”
//  · 优先加载 NAILONG_MODEL_PATH（glb，骨骼动画）；文件不存在/加载失败时
//    自动回落到程序化建模（球体拼装的黄身白肚奶龙），保证一定能跑起来。
//  · 动画状态机：Idle / Walk / Run / Jump / Shoot / Laugh
//    优先级 Shoot > Jump > Run > Walk > Idle，Laugh 为一次性覆盖。
//  · 交叉淡入淡出：raylib 不支持骨骼动画混合，这里用“关节角插值”近似，
//    切换时关节角按指数曲线向目标姿态收敛，视觉上就是平滑过渡。
// ============================================================
#include "Nailong.h"
#include "Player.h"
#include "Maze.h"
#include "raymath.h"
#include "rlgl.h"        // 必须在 raylib.h 之后，否则 Matrix 未定义

#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    constexpr float kRad2Deg = 57.29578f;

    // ---- 程序化奶龙尺寸（米），原点在脚底，+Z 为正面 ----
    constexpr float kHipY      = 0.52f;   // 胯高
    constexpr float kHipX      = 0.20f;   // 两腿间距（半）
    constexpr float kThighLen  = 0.30f;
    constexpr float kShinLen   = 0.24f;
    constexpr float kBodyY     = 1.02f;   // 躯干中心高
    constexpr float kBodyR     = 0.46f;   // 躯干半径
    constexpr float kShoulderY = 1.24f;
    constexpr float kShoulderX = 0.44f;
    constexpr float kArmLen    = 0.26f;
    constexpr float kForeLen   = 0.22f;
    constexpr float kHeadY     = 1.72f;
    constexpr float kHeadR     = 0.42f;
    constexpr float kLimbR     = 0.15f;

    // ---- 配色（黄身白肚 + 深色四肢，按 mp4 里奶龙的样子调的，可自行改） ----
    const Color kColorBody  = { 252, 214,  74, 255 };
    const Color kColorBelly = { 252, 250, 240, 255 };
    const Color kColorLimb  = {  54,  46,  42, 255 };
    const Color kColorEye   = {  28,  24,  22, 255 };

    constexpr float kBlendSpeed   = 14.0f;   // 姿态插值速度（越大切换越快）
    constexpr float kShootSeconds = 0.35f;   // Shoot 一次性时长（和射击冷却一致）
    constexpr float kLaughSeconds = 1.60f;   // Laugh 一次性时长
    constexpr float kModelFps     = 24.0f;   // glb 动画播放帧率
    constexpr float kModelBlendSeconds = 0.18f;   // 模型动画交叉淡化时长（秒）
    constexpr float kYawTurnSpeed = 12.0f;   // 角色转身速度（越大转得越快）

    // 在当前的关节矩阵里画一根“肉嘟嘟的四肢”：两点之间叠一串球
    void Blob(Vector3 a, Vector3 b, float ra, float rb, Color c)
    {
        const int steps = 5;
        for (int i = 0; i <= steps; ++i)
        {
            const float t = (float)i / (float)steps;
            const Vector3 p = { a.x + (b.x - a.x) * t,
                                a.y + (b.y - a.y) * t,
                                a.z + (b.z - a.z) * t };
            DrawSphereEx(p, ra + (rb - ra) * t, 8, 12, c);
        }
    }

    // 椭球：靠矩阵缩放把球压扁/拉长，省得自己写网格
    void Ellipsoid(float cx, float cy, float cz, float r, float sx, float sy, float sz, Color c)
    {
        rlPushMatrix();
        rlTranslatef(cx, cy, cz);
        rlScalef(sx, sy, sz);
        DrawSphereEx({ 0.0f, 0.0f, 0.0f }, r, 10, 16, c);
        rlPopMatrix();
    }

    // 大小写不敏感的子串匹配（glb 动画名常是 Armature|Idle 这种带前缀的）
    bool NameContains(const char* name, const char* key)
    {
        if (!name || !key || !*key) return false;
        std::string n(name);
        std::string k(key);
        for (char& ch : n) ch = (char)tolower((unsigned char)ch);
        for (char& ch : k) ch = (char)tolower((unsigned char)ch);
        return n.find(k) != std::string::npos;
    }

    // 拼接目录 + 相对路径（自动补分隔符）
    std::string JoinAssetPath(const std::string& directory, const std::string& relative)
    {
        if (directory.empty()) return relative;
        std::string path = directory;
        const char last = path[path.size() - 1];
        if (last != '\\' && last != '/') path += '\\';
        return path + relative;
    }

    // 在 exe 目录、它的各级父目录、以及当前工作目录里找资源。
    // 为什么需要：VS 里直接 F5 运行时工作目录是 codes\，双击 exe 时可能是项目根；
    // 之前只按工作目录找，VS 下必然找不到模型 -> 回落成程序化奶龙（就是一坨）。
    std::string ResolveAssetPath(const char* relative)
    {
        if (relative == nullptr || relative[0] == '\0') return std::string();

        std::vector<std::string> roots;

        const char* appDirectory = GetApplicationDirectory();
        if (appDirectory != nullptr && appDirectory[0] != '\0')
        {
            std::string directory(appDirectory);
            for (int level = 0; level < 6 && !directory.empty(); ++level)
            {
                roots.push_back(directory);
                const size_t slash = directory.find_last_of("\\/");
                if (slash == std::string::npos || slash == 0) break;
                directory = directory.substr(0, slash);
            }
        }

        const char* workingDirectory = GetWorkingDirectory();
        if (workingDirectory != nullptr && workingDirectory[0] != '\0')
            roots.push_back(std::string(workingDirectory));

        for (const std::string& root : roots)
        {
            const std::string path = JoinAssetPath(root, relative);
            if (FileExists(path.c_str())) return path;
        }
        return std::string();
    }

    struct AnimMacro
    {
        NailongAnim anim;
        const char* name;
    };

    // 宏 -> 状态，一一对应（改 Config.h 里的宏就能适配真模型）
    const AnimMacro kAnimMacros[] = {
        { NailongAnim::Idle,  NAILONG_ANIM_IDLE  },
        { NailongAnim::Walk,  NAILONG_ANIM_WALK  },
        { NailongAnim::Run,   NAILONG_ANIM_RUN   },
        { NailongAnim::Jump,  NAILONG_ANIM_JUMP  },
        { NailongAnim::Shoot, NAILONG_ANIM_SHOOT },
        { NailongAnim::Laugh, NAILONG_ANIM_LAUGH },
    };
}

Nailong::Nailong() {}

Nailong::~Nailong()
{
    Unload();
}

// ------------------------------------------------------------
//  加载：有 glb 就用 glb，没有就用程序化奶龙
// ------------------------------------------------------------
void Nailong::Load()
{
    const std::string resolvedModel = ResolveAssetPath(NAILONG_MODEL_PATH);
    if (!resolvedModel.empty())
    {
        TraceLog(LOG_INFO, "NAILONG: 模型路径解析为 %s", resolvedModel.c_str());
        model = LoadModel(resolvedModel.c_str());
        if (model.meshCount > 0)
        {
            useModel = true;
            ApplySidecarTexture();          // raylib 取不到 glb 内嵌贴图，改挂同目录独立 PNG
            animations = LoadModelAnimations(resolvedModel.c_str(), &animationCount);   // 动画单独加载
            for (const AnimMacro& m : kAnimMacros)
            {
                animIndex[(int)m.anim] = -1;
                for (int i = 0; i < animationCount; ++i)
                {
                    if (NameContains(animations[i].name, m.name))
                    {
                        animIndex[(int)m.anim] = i;
                        break;
                    }
                }
                // 骨骼数对不上的动画直接弃用，免得运行时崩
                if (animIndex[(int)m.anim] >= 0 &&
                    !IsModelAnimationValid(model, animations[animIndex[(int)m.anim]]))
                {
                    animIndex[(int)m.anim] = -1;
                }
            }
        }
        else
        {
            UnloadModel(model);
            model = Model{ 0 };
        }
    }
    // 静态网格（没有骨骼动画）就上软件层骨架：标定 + CPU 蒙皮，补出走跑跳
    bool hasBoneAnim = false;
    for (int i = 0; i < (int)NailongAnim::Count; ++i)
        if (animIndex[i] >= 0) hasBoneAnim = true;
#if NAILONG_RIG_ENABLE
    if (useModel && !hasBoneAnim && model.meshCount > 0) rig.Init(model.meshes[0]);
#else
    (void)hasBoneAnim;
#endif
    // 模型最低点不一定在网格原点（Tripo 导出的 glb 带 node 平移），按静态 bbox 自动补偿：
    // 脚底永远落在 drawFeet.y 上，脚 / 尾巴不会插进地面。
    if (useModel && model.meshCount > 0)
    {
        const BoundingBox bb = GetModelBoundingBox(model);
        modelFeetLift = -bb.min.y * NAILONG_MODEL_SCALE;
        TraceLog(LOG_INFO, "NAILONG: 静态 bbox y=[%.3f, %.3f]，占地半宽 x=%.3f z=%.3f，脚底补偿 %.3f m",
                 bb.min.y, bb.max.y,
                 (bb.max.x - bb.min.x) * 0.5f * NAILONG_MODEL_SCALE,
                 (bb.max.z - bb.min.z) * 0.5f * NAILONG_MODEL_SCALE,
                 modelFeetLift);
    }
    PrintDebugInfo();
}

// ------------------------------------------------------------
//  独立贴图：本工程用的 raylib 构建加载不了 glb 内嵌贴图
//  （日志表现为 IMAGE: Data format not supported），所以贴图单独
//  存成 assets/models/nailong_color.png，加载后手动挂到材质上。
// ------------------------------------------------------------
void Nailong::ApplySidecarTexture()
{
    const std::string resolvedTexture = ResolveAssetPath(NAILONG_TEXTURE_PATH);
    if (model.materialCount < 1 || resolvedTexture.empty()) return;

    sideTexture = LoadTexture(resolvedTexture.c_str());
    if (sideTexture.id == 0)
    {
        TraceLog(LOG_WARNING, "NAILONG: 贴图 %s 加载失败，模型会是白模", resolvedTexture.c_str());
        return;
    }

    GenTextureMipmaps(&sideTexture);
    SetTextureFilter(sideTexture, TEXTURE_FILTER_BILINEAR);

    // 两个坑：① glb 没写 baseColorFactor，raylib 给材质留的乘色是黑的 (0,0,0)，
    //           贴图会被乘成全黑；② raylib 给这个模型建了不止一个材质。
    // 所以：所有材质都挂上贴图，并把乘色显式设回白色。
    for (int i = 0; i < model.materialCount; ++i)
    {
        model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = sideTexture;
        model.materials[i].maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
    }
    TraceLog(LOG_INFO, "NAILONG: 已挂上独立贴图 %s（%dx%d，共 %d 个材质）",
        resolvedTexture.c_str(), sideTexture.width, sideTexture.height, model.materialCount);
}

void Nailong::Unload()
{
    if (useModel)
    {
        if (animations)
        {
            UnloadModelAnimations(animations, animationCount);
            animations = nullptr;
            animationCount = 0;
        }
        rig.Unload();
        // 摘掉材质上的贴图：UnloadModel 会连带卸载材质贴图，不摘会重复卸载同一个 id
        for (int i = 0; i < model.materialCount; ++i)
            model.materials[i].maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{ 0 };
        if (sideTexture.id > 0) { UnloadTexture(sideTexture); sideTexture = Texture2D{ 0 }; }
        UnloadModel(model);
        model = Model{ 0 };
        useModel = false;
        blendFrom = -1;
        blendTimer = 0.0f;
    }
}

void Nailong::PrintDebugInfo() const
{
    if (!useModel)
    {
        TraceLog(LOG_WARNING, "NAILONG: 没找到 %s，改用程序化奶龙（放好模型后重开即可自动启用）",
            NAILONG_MODEL_PATH);
        return;
    }

    TraceLog(LOG_INFO, "NAILONG: 已加载 %s  mesh=%d  bone=%d  animation=%d",
        NAILONG_MODEL_PATH, model.meshCount,
        model.skeleton.boneCount, animationCount);
    if (rig.Ready())
        TraceLog(LOG_INFO, "NAILONG: 软件层骨架已标定（%d 顶点）静态网格已能走跑跳", rig.VertexCount());

    for (int i = 0; i < animationCount; ++i)
    {
        TraceLog(LOG_INFO, "NAILONG: 动画[%d] = %s（%d 帧 / %d 骨骼）",
            i, animations[i].name,
            animations[i].keyframeCount, animations[i].boneCount);
    }
    for (const AnimMacro& m : kAnimMacros)
    {
        if (animIndex[(int)m.anim] < 0)
            TraceLog(LOG_WARNING, "NAILONG: 宏 %s 没匹配到动画，请核对模型里的动画名", m.name);
    }
}

// ------------------------------------------------------------
//  状态机 + 姿态插值
// ------------------------------------------------------------
void Nailong::Update(float dt, const NailongState& s)
{
    drawFeet = s.feet;
    // 朝向平滑：走跑时朝着移动方向慢慢转过去，不会"啪"地一下扭过去
    {
        float diff = s.yaw - drawYaw;
        while (diff >  3.14159265f) diff -= 6.28318531f;
        while (diff < -3.14159265f) diff += 6.28318531f;
        drawYaw += diff * (1.0f - expf(-kYawTurnSpeed * dt));
    }

    // 一次性动作的触发（正在播就不重复触发）
    if (s.laughTriggered && laughTimer <= 0.0f) laughTimer = kLaughSeconds;
    if (s.shootTriggered && shootTimer <= 0.0f) shootTimer = kShootSeconds;

    if (laughTimer > 0.0f) laughTimer = fmaxf(0.0f, laughTimer - dt);
    if (shootTimer > 0.0f) shootTimer = fmaxf(0.0f, shootTimer - dt);

    // 优先级：Laugh（一次性覆盖）> Shoot > Jump > Run > Walk > Idle
    const NailongAnim prev = anim;
    if (laughTimer > 0.0f)            anim = NailongAnim::Laugh;
    else if (shootTimer > 0.0f)       anim = NailongAnim::Shoot;
    else if (!s.grounded || s.flying) anim = NailongAnim::Jump;
    else if (s.sprinting && s.moving) anim = NailongAnim::Run;
    else if (s.moving)                anim = NailongAnim::Walk;
    else                              anim = NailongAnim::Idle;

    if (anim != prev)                     // 换状态：重新计时 + 记录模型动画混合起点
    {
        animTime = 0.0f;
        if (useModel && prev < NailongAnim::Count)
        {
            blendFrom = animIndex[(int)prev];
            blendFrame = animFrame;
            blendTimer = kModelBlendSeconds;
        }
    }
    animTime += dt;

    // 目标姿态
    Pose target;
    switch (anim)
    {
    case NailongAnim::Walk:  target = MakeWalk(animTime); break;
    case NailongAnim::Run:   target = MakeRun(animTime); break;
    case NailongAnim::Jump:  target = MakeJump(s); break;
    case NailongAnim::Shoot: target = MakeShoot(kShootSeconds - shootTimer); break;
    case NailongAnim::Laugh: target = MakeLaugh(kLaughSeconds - laughTimer); break;
    default:                 target = MakeIdle(animTime); break;
    }

    // 交叉淡入淡出（关节角插值近似，指数收敛，和帧率无关）
    const float k = 1.0f - expf(-kBlendSpeed * dt);
    BlendPose(pose, target, k);

    UpdateModelAnim(dt);

    // 软件层蒙皮：把当前姿态烘到顶点上（没有 glb 骨骼动画时才会启用）
    if (rig.Ready() && model.meshCount > 0)
    {
        rig.SetPose(pose);
        rig.Apply(model.meshes[0]);
    }
}

void Nailong::UpdateModelAnim(float dt)
{
    if (!useModel || animationCount <= 0) return;

    const int idx = animIndex[(int)anim];
    if (idx < 0 || idx >= animationCount) return;

    const ModelAnimation& animData = animations[idx];
    if (animData.keyframeCount <= 1) return;

    animFrame += dt * kModelFps;
    const float frame = fmodf(animFrame, (float)animData.keyframeCount);

    // 换动作时双动画混合（真·交叉淡入淡出：blend 从 0 走到 1）
    if (blendTimer > 0.0f && blendFrom >= 0 && blendFrom < animationCount && blendFrom != idx)
    {
        blendTimer = fmaxf(0.0f, blendTimer - dt);
        blendFrame += dt * kModelFps;
        const float oldFrame = fmodf(blendFrame, (float)animations[blendFrom].keyframeCount);
        const float w = 1.0f - blendTimer / kModelBlendSeconds;
        UpdateModelAnimationEx(model, animations[blendFrom], oldFrame, animData, frame, w);
        return;
    }

    UpdateModelAnimation(model, animData, frame);
}

void Nailong::BlendPose(Pose& cur, const Pose& target, float k)
{
    cur.legL += (target.legL - cur.legL) * k;
    cur.legR += (target.legR - cur.legR) * k;
    cur.kneeL += (target.kneeL - cur.kneeL) * k;
    cur.kneeR += (target.kneeR - cur.kneeR) * k;
    cur.armL += (target.armL - cur.armL) * k;
    cur.armR += (target.armR - cur.armR) * k;
    cur.foreL += (target.foreL - cur.foreL) * k;
    cur.foreR += (target.foreR - cur.foreR) * k;
    cur.bodyLean += (target.bodyLean - cur.bodyLean) * k;
    cur.bodyRoll += (target.bodyRoll - cur.bodyRoll) * k;
    cur.bodyBob += (target.bodyBob - cur.bodyBob) * k;
    cur.headPitch += (target.headPitch - cur.headPitch) * k;
    cur.scaleY += (target.scaleY - cur.scaleY) * k;
    cur.tailSway += (target.tailSway - cur.tailSway) * k;
    cur.tailLift += (target.tailLift - cur.tailLift) * k;
}

// ---- 各个动作的目标姿态 ----
Nailong::Pose Nailong::MakeIdle(float t)
{
    Pose p;
    p.bodyBob = sinf(t * 2.2f) * 0.03f;          // 呼吸起伏
    p.armL = sinf(t * 2.2f) * 4.0f;
    p.armR = -sinf(t * 2.2f) * 4.0f;
    p.legL = 2.0f;
    p.legR = -2.0f;
    p.headPitch = sinf(t * 2.2f + 1.0f) * 3.0f;
    p.tailSway = sinf(t * 1.1f) * 11.0f;          // 尾巴慢慢左右摆
    p.tailLift = sinf(t * 2.2f + 0.6f) * 3.0f;
    return p;
}

Nailong::Pose Nailong::MakeWalk(float t)
{
    const float w = t * 7.5f;                    // 步频
    const float sw = sinf(w);
    Pose p;
    p.legL = sw * 28.0f;
    p.legR = -sw * 28.0f;
    p.kneeL = fmaxf(0.0f, -sw) * 34.0f;          // 后摆那条腿屈膝
    p.kneeR = fmaxf(0.0f, sw) * 34.0f;
    p.armL = -sw * 22.0f;                        // 手臂与腿反向摆
    p.armR = sw * 22.0f;
    p.foreL = 14.0f;
    p.foreR = 14.0f;
    p.bodyLean = 4.0f;
    p.bodyBob = fabsf(sinf(w)) * 0.045f - 0.015f;
    p.bodyRoll = cosf(w) * 2.0f;
    p.tailSway = -cosf(w) * 18.0f;               // 尾巴跟着步伐甩
    p.tailLift = fabsf(sinf(w)) * 6.0f;
    return p;
}

Nailong::Pose Nailong::MakeRun(float t)
{
    const float w = t * 11.0f;
    const float sw = sinf(w);
    Pose p;
    p.legL = sw * 46.0f;
    p.legR = -sw * 46.0f;
    p.kneeL = fmaxf(0.0f, -sw) * 62.0f;
    p.kneeR = fmaxf(0.0f, sw) * 62.0f;
    p.armL = -sw * 40.0f;
    p.armR = sw * 40.0f;
    p.foreL = 62.0f;
    p.foreR = 62.0f;
    p.bodyLean = 14.0f;                          // 前倾冲刺感
    p.bodyBob = fabsf(sinf(w)) * 0.075f - 0.02f;
    p.bodyRoll = cosf(w) * 4.0f;
    p.headPitch = -4.0f;
    p.tailSway = -cosf(w) * 26.0f;               // 跑起来尾巴甩得更欢
    p.tailLift = 10.0f + fabsf(sinf(w)) * 8.0f;
    return p;
}

Nailong::Pose Nailong::MakeJump(const NailongState& s)
{
    Pose p;
    if (s.flying)                                // 管理员飞行：悬停抱腿
    {
        p.legL = 24.0f; p.legR = 24.0f;
        p.kneeL = 50.0f; p.kneeR = 50.0f;
        p.armL = 55.0f; p.armR = 55.0f;
        p.foreL = 28.0f; p.foreR = 28.0f;
        p.bodyLean = 8.0f;
        p.tailLift = 16.0f;
        return p;
    }
    if (s.verticalSpeed > 0.5f)                  // 上升：收腿举臂
    {
        p.legL = 26.0f; p.legR = 22.0f;
        p.kneeL = 55.0f; p.kneeR = 60.0f;
        p.armL = 70.0f; p.armR = 70.0f;
        p.foreL = 30.0f; p.foreR = 30.0f;
        p.bodyLean = 6.0f;
        p.scaleY = 1.06f;
        p.tailLift = 20.0f;
    }
    else                                         // 下落：伸腿伸手准备落地
    {
        p.legL = 12.0f; p.legR = 8.0f;
        p.kneeL = 18.0f; p.kneeR = 22.0f;
        p.armL = -30.0f; p.armR = -30.0f;
        p.foreL = 12.0f; p.foreR = 12.0f;
        p.scaleY = 1.02f;
        p.tailLift = 6.0f;
    }
    return p;
}

Nailong::Pose Nailong::MakeShoot(float t)
{
    // 0.35 秒：抬手前举 -> 收手，右臂为主
    const float windup = fminf(1.0f, fmaxf(0.0f, t) / 0.10f);
    const float recover = (t > kShootSeconds - 0.10f) ? 0.45f : 1.0f;
    Pose p;
    p.armR = 85.0f * windup * recover;
    p.foreR = 10.0f;
    p.armL = -14.0f;
    p.foreL = 18.0f;
    p.bodyLean = -6.0f;                          // 后坐力
    p.headPitch = -3.0f;
    p.legL = 8.0f;
    p.legR = -6.0f;
    p.tailSway = 8.0f;
    return p;
}

Nailong::Pose Nailong::MakeLaugh(float t)
{
    // 双手合十捧在身前 + 身体前后大晃 + 仰头大笑
    const float w = t * 9.0f;
    Pose p;
    p.armL = 78.0f + sinf(w) * 6.0f;
    p.armR = 78.0f - sinf(w) * 6.0f;
    p.foreL = 55.0f;
    p.foreR = 55.0f;
    p.bodyLean = -10.0f + sinf(w) * 14.0f;
    p.headPitch = 16.0f + sinf(w * 0.5f) * 6.0f;
    p.bodyBob = sinf(w * 0.5f) * 0.06f;
    p.legL = 6.0f;
    p.legR = 6.0f;
    p.kneeL = 10.0f;
    p.kneeR = 10.0f;
    // 大笑只用姿态表达（双手合十、前俯后仰、甩尾），不再缩放 Y：
    // 缩放会让静态模型整体拉伸变形、双脚离地，所以体型全程保持不变。
    p.tailSway = sinf(w * 0.5f) * 20.0f;
    p.tailLift = 12.0f;
    return p;
}

// ---- 绘制 ----
void Nailong::Draw() const
{
    if (useModel)
    {
        // glb 的模型朝向同奶龙（靠 NAILONG_MODEL_YAW_OFFSET）
        Vector3 origin = drawFeet;
        origin.y += modelFeetLift;   // 按静态 bbox 补正：脚底正好落在 drawFeet 上，不会插进地面
        DrawModelEx(model, origin, { 0.0f, 1.0f, 0.0f },
            drawYaw * kRad2Deg + NAILONG_MODEL_YAW_OFFSET,
            { NAILONG_MODEL_SCALE, NAILONG_MODEL_SCALE, NAILONG_MODEL_SCALE }, WHITE);
        return;
    }
    DrawProcedural();
}

void Nailong::DrawLegProcedural(float side, float swing, float knee)
{
    // 腿直接挂在胯上（不随上身倾斜），坐标为世界尺度
    rlPushMatrix();
    rlTranslatef(side * kHipX, kHipY, 0.0f);
    rlRotatef(-swing, 1.0f, 0.0f, 0.0f);          // 绕 X 正向是向后摆，故取负：+ 为向前
    Blob({ 0.0f, 0.0f, 0.0f }, { 0.0f, -kThighLen, 0.0f }, kLimbR * 1.15f, kLimbR, kColorLimb);
    rlTranslatef(0.0f, -kThighLen, 0.0f);
    rlRotatef(knee, 1.0f, 0.0f, 0.0f);            // 小腿向后收
    Blob({ 0.0f, 0.0f, 0.0f }, { 0.0f, -kShinLen, 0.0f }, kLimbR, kLimbR * 1.05f, kColorLimb);
    rlTranslatef(0.0f, -kShinLen, 0.0f);
    Blob({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.17f }, kLimbR * 1.05f, kLimbR * 0.8f, kColorLimb);  // 脚掌
    rlPopMatrix();
}

void Nailong::DrawArmProcedural(float side, float swing, float fore)
{
    // 手臂挂在肩膀上，坐标是相对“胯部原点”的（调用处已把原点移到胯）
    rlPushMatrix();
    rlTranslatef(side * kShoulderX, kShoulderY - kHipY, 0.0f);
    rlRotatef(-swing, 1.0f, 0.0f, 0.0f);
    Blob({ 0.0f, 0.0f, 0.0f }, { 0.0f, -kArmLen, 0.0f }, kLimbR * 0.95f, kLimbR * 0.85f, kColorLimb);
    rlTranslatef(0.0f, -kArmLen, 0.0f);
    rlRotatef(fore, 1.0f, 0.0f, 0.0f);
    Blob({ 0.0f, 0.0f, 0.0f }, { 0.0f, -kForeLen, 0.0f }, kLimbR * 0.85f, kLimbR * 0.95f, kColorLimb);
    rlPopMatrix();
}

void Nailong::DrawProcedural() const
{
    rlPushMatrix();
    rlTranslatef(drawFeet.x, drawFeet.y, drawFeet.z);
    rlRotatef(drawYaw * kRad2Deg, 0.0f, 1.0f, 0.0f);
    rlScalef(1.0f, pose.scaleY, 1.0f);

    // 腿
    DrawLegProcedural(-1.0f, pose.legL, pose.kneeL);
    DrawLegProcedural(1.0f, pose.legR, pose.kneeR);

    // 上身（从胯部开始倾斜/浮动）
    rlPushMatrix();
    rlTranslatef(0.0f, kHipY + pose.bodyBob, 0.0f);
    rlRotatef(pose.bodyLean, 1.0f, 0.0f, 0.0f);
    rlRotatef(pose.bodyRoll, 0.0f, 0.0f, 1.0f);

    const float by = kBodyY - kHipY;              // 躯干中心（相对胯）

    // 躯干 + 白肚
    Ellipsoid(0.0f, by, 0.0f, kBodyR, 1.0f, 1.06f, 0.94f, kColorBody);
    Ellipsoid(0.0f, by - 0.02f, kBodyR * 0.70f, kBodyR * 0.60f, 1.0f, 0.94f, 0.5f, kColorBelly);

    // 头
    rlPushMatrix();
    rlTranslatef(0.0f, kHeadY - kHipY, 0.02f);
    rlRotatef(pose.headPitch, 1.0f, 0.0f, 0.0f);
    DrawSphereEx({ 0.0f, 0.0f, 0.0f }, kHeadR, 12, 18, kColorBody);
    DrawSphereEx({ -0.16f, 0.05f, kHeadR * 0.82f }, 0.075f, 8, 12, kColorEye);   // 左眼
    DrawSphereEx({ 0.16f, 0.05f, kHeadR * 0.82f }, 0.075f, 8, 12, kColorEye);    // 右眼
    const float mouthR = (anim == NailongAnim::Laugh) ? 0.10f : 0.055f;          // 大笑时张嘴
    DrawSphereEx({ 0.0f, -0.17f, kHeadR * 0.84f }, mouthR, 8, 12, kColorEye);    // 嘴
    rlPopMatrix();

    // 手臂
    DrawArmProcedural(-1.0f, pose.armL, pose.foreL);
    DrawArmProcedural(1.0f, pose.armR, pose.foreR);

    // 尾巴
    rlPushMatrix();
    rlTranslatef(0.0f, by - 0.30f, -kBodyR * 0.72f);
    rlRotatef(-35.0f, 1.0f, 0.0f, 0.0f);
    Blob({ 0.0f, 0.0f, 0.0f }, { 0.0f, 0.16f, -0.42f }, 0.13f, 0.07f, kColorBody);
    rlPopMatrix();

    rlPopMatrix();   // 上身
    rlPopMatrix();   // 角色根
}

const char* Nailong::CurrentAnimName() const
{
    switch (anim)
    {
    case NailongAnim::Walk:  return "Walk";
    case NailongAnim::Run:   return "Run";
    case NailongAnim::Jump:  return "Jump";
    case NailongAnim::Shoot: return "Shoot";
    case NailongAnim::Laugh: return "Laugh";
    default:                 return "Idle";
    }
}