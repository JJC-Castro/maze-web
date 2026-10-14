// ============================================================
//  NailongRig.cpp —— 软件层骨骼标定 + CPU 蒙皮（接口见 NailongRig.h）
//
//  标定思路（全自动，不依赖建模软件）：
//    1) 量出网格 bbox，按身高 H 归一化出各关节位置（比例从 nailong.glb
//       实测得到：腿在 z=±0.205H、胯高 0.29H、躯干轴线压在脚正上方…）；
//    2) 每个顶点算到各「骨段」的反距离平方权重，取最大的 3 根混合归一化；
//       权重是位置的连续函数，所以网格上没有断层，不用额外做平滑。
//  蒙皮：每帧按姿态算好骨头矩阵，在 CPU 上线性混合顶点，再推回显存。
// ============================================================
#include "NailongRig.h"
#include "raymath.h"

#include <cmath>

namespace
{
    constexpr float kDeg2Rad = 0.0174532925f;

    // ---- 骨架几何比例（相对身高 H）----
    //  换模型时，只要新模型依然是「脚底 y=0 / 正面 +X / 左右为 ±Z」，
    //  这套比例基本通用；偏差大就把下面几个数字按 bbox 量一遍改掉即可。
    constexpr float kHipY      = 0.290f;   // 胯（大腿根）高度
    constexpr float kKneeY     = 0.155f;   // 膝盖高度
    constexpr float kFootY     = 0.010f;   // 脚踝高度
    constexpr float kLegZ      = 0.205f;   // 两腿在 Z 方向的半间距
    constexpr float kFootZ     = 0.215f;   // 脚在 Z 方向的外扩
    constexpr float kAxisX     = 0.150f;   // 躯干轴线相对 bbox 中心的 X 偏移
    constexpr float kFootX     = 0.170f;   // 脚相对 bbox 中心的 X 位置
    constexpr float kSpineMidY = 0.450f;   // 腰（Spine 骨头起点）
    constexpr float kSpineTopY = 0.780f;   // 脖子（Spine 骨头终点）
    constexpr float kHeadTopY  = 0.960f;   // 头顶
    constexpr float kHeadX     = 0.135f;   // 头相对 bbox 中心的 X 偏移
    constexpr float kTailBaseX = -0.080f;  // 尾巴根
    constexpr float kTailBaseY =  0.300f;
    constexpr float kTailTipX  = -0.370f;  // 尾巴尖（朝 -X，即身后）
    constexpr float kTailTipY  =  0.170f;

    // 反距离平方权重里的软化项（相对身高）：越小越“硬”，越大越“糊”
    constexpr float kWeightEps = 0.030f;

    // 点到线段距离的平方
    float DistSqToSegment(const float* p, const Vector3& a, const Vector3& b)
    {
        const float ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
        const float wx = p[0] - a.x, wy = p[1] - a.y, wz = p[2] - a.z;
        const float uu = ux * ux + uy * uy + uz * uz;
        float t = (uu > 1e-12f) ? (wx * ux + wy * uy + wz * uz) / uu : 0.0f;
        t = (t < 0.0f) ? 0.0f : ((t > 1.0f) ? 1.0f : t);
        const float dx = wx - ux * t, dy = wy - uy * t, dz = wz - uz * t;
        return dx * dx + dy * dy + dz * dz;
    }

    // 绕某个关节点的局部变换：T(h) · R · T(-h)
    Matrix AroundHead(const Vector3& h, const Matrix& r)
    {
        return MatrixMultiply(MatrixTranslate(h.x, h.y, h.z),
                              MatrixMultiply(r, MatrixTranslate(-h.x, -h.y, -h.z)));
    }
}

// ------------------------------------------------------------
//  标定：按网格 bbox 摆骨头 + 算蒙皮权重（加载模型后调用一次）
// ------------------------------------------------------------
bool SoftRig::Init(const Mesh& mesh)
{
    Unload();

    if (mesh.vertexCount <= 0 || mesh.vertices == nullptr) return false;

    vertexCount = mesh.vertexCount;
    const float* v = mesh.vertices;

    // ---- 1. 量 bbox ----
    Vector3 mn = { v[0], v[1], v[2] };
    Vector3 mx = mn;
    for (int i = 1; i < vertexCount; ++i)
    {
        const float x = v[i * 3 + 0], y = v[i * 3 + 1], z = v[i * 3 + 2];
        if (x < mn.x) mn.x = x;  if (x > mx.x) mx.x = x;
        if (y < mn.y) mn.y = y;  if (y > mx.y) mx.y = y;
        if (z < mn.z) mn.z = z;  if (z > mx.z) mx.z = z;
    }

    const float H  = (mx.y - mn.y > 0.001f) ? (mx.y - mn.y) : 1.0f;
    const float y0 = mn.y;                       // 脚底
        baseY = y0;                                // 记下脚底平面，体型缩放绕它做（脚不离地）
    const float cx = (mn.x + mx.x) * 0.5f;       // 模型 X 中心
    const float ax = cx + kAxisX * H;            // 躯干轴线（压在脚正上方）
    const float fx = cx + kFootX * H;

    height = H;

    // ---- 2. 摆骨头（head = 关节，tail = 骨段末端，只用于算权重）----
    bones[0] = { { ax, y0, 0.0f },              { ax, y0 + kHipY * H, 0.0f },       -1 };  // Root（不蒙皮）
    bones[1] = { { ax, y0 + kHipY * H, 0.0f },  { ax, y0 + kSpineMidY * H, 0.0f },   0 };  // Hips 胯
    bones[2] = { { ax, y0 + kSpineMidY * H, 0.0f },
                 { ax + 0.02f * H, y0 + kSpineTopY * H, 0.0f },                          1 };  // Spine 腰
    bones[3] = { { ax + 0.02f * H, y0 + kSpineTopY * H, 0.0f },
                 { cx + kHeadX * H, y0 + kHeadTopY * H, 0.0f },                          2 };  // Head 头
    bones[4] = { { ax, y0 + kHipY * H, -kLegZ * H },
                 { fx, y0 + kKneeY * H, -kLegZ * H },                                    1 };  // ThighL 左大腿
    bones[5] = { { fx, y0 + kKneeY * H, -kLegZ * H },
                 { fx, y0 + kFootY * H, -kFootZ * H },                                   4 };  // ShinL 左小腿
    bones[6] = { { ax, y0 + kHipY * H,  kLegZ * H },
                 { fx, y0 + kKneeY * H,  kLegZ * H },                                    1 };  // ThighR 右大腿
    bones[7] = { { fx, y0 + kKneeY * H,  kLegZ * H },
                 { fx, y0 + kFootY * H,  kFootZ * H },                                   6 };  // ShinR 右小腿
    bones[8] = { { cx + kTailBaseX * H, y0 + kTailBaseY * H, 0.0f },
                 { cx + kTailTipX * H,  y0 + kTailTipY * H,  0.0f },                     1 };  // Tail 尾巴

    // ---- 3. 蒙皮权重：到骨段的反距离平方，取最大的 kInfluences 根 ----
    const float eps = (kWeightEps * H) * (kWeightEps * H);
    vBone.assign((size_t)vertexCount * kInfluences, 0);
    vWeight.assign((size_t)vertexCount * kInfluences, 0.0f);

    for (int i = 0; i < vertexCount; ++i)
    {
        const float* p = &v[i * 3];

        float bestW[kInfluences];
        int   bestB[kInfluences];
        for (int k = 0; k < kInfluences; ++k) { bestW[k] = -1.0f; bestB[k] = 1; }

        for (int b = 1; b < kMaxBones; ++b)     // 0 号 Root 不参与蒙皮
        {
            const float d2 = DistSqToSegment(p, bones[b].head, bones[b].tail);
            const float w  = 1.0f / (d2 + eps);
            for (int k = 0; k < kInfluences; ++k)         // 插入排序，保持降序
            {
                if (w > bestW[k])
                {
                    for (int j = kInfluences - 1; j > k; --j) { bestW[j] = bestW[j - 1]; bestB[j] = bestB[j - 1]; }
                    bestW[k] = w;  bestB[k] = b;
                    break;
                }
            }
        }

        float sum = 0.0f;
        for (int k = 0; k < kInfluences; ++k) sum += bestW[k];
        if (sum <= 0.0f) sum = 1.0f;

        for (int k = 0; k < kInfluences; ++k)
        {
            vBone[i * kInfluences + k]   = bestB[k];
            vWeight[i * kInfluences + k] = bestW[k] / sum;
        }
    }

    // ---- 4. 留一份原始顶点（蒙皮每次都从原始数据算起，避免误差累积）----
    restPos.assign(v, v + (size_t)vertexCount * 3);
    skinPos.assign((size_t)vertexCount * 3, 0.0f);
    if (mesh.normals != nullptr)
    {
        restNrm.assign(mesh.normals, mesh.normals + (size_t)vertexCount * 3);
        skinNrm.assign((size_t)vertexCount * 3, 0.0f);
    }

    ready = true;
    TraceLog(LOG_INFO, "NAILONG-RIG: 标定完成 身高=%.3f 骨头=%d 顶点=%d 躯干轴x=%.3f 胯y=%.3f 腿Z=+-%.3f",
        height, kMaxBones - 1, vertexCount, bones[1].head.x, bones[1].head.y, kLegZ * H);
    return true;
}

void SoftRig::Unload()
{
    ready = false;
    vertexCount = 0;
    restPos.clear();  restNrm.clear();
    skinPos.clear();  skinNrm.clear();
    vBone.clear();    vWeight.clear();
    lastCostMs = 0.0f;
}

// ------------------------------------------------------------
//  姿态 -> 骨头矩阵（父子依次相乘，绕各自关节旋转）
// ------------------------------------------------------------
void SoftRig::BuildMatrices()
{
    const float lean = pose.bodyLean * kDeg2Rad;
    const float roll = pose.bodyRoll * kDeg2Rad;
    const float legL = pose.legL * kDeg2Rad;
    const float legR = pose.legR * kDeg2Rad;
    const float knL  = pose.kneeL * kDeg2Rad;
    const float knR  = pose.kneeR * kDeg2Rad;
    const float head = pose.headPitch * kDeg2Rad;
    const float sway = pose.tailSway * kDeg2Rad;
    const float lift = pose.tailLift * kDeg2Rad;

    // Root：体型缩放绕【脚底平面】——静态模型的原点不一定在脚底，绕原点缩放会把脚压进地面
    boneMat[0] = MatrixMultiply(MatrixTranslate(0.0f, baseY * (1.0f - pose.scaleY), 0.0f),
                                MatrixScale(1.0f, pose.scaleY, 1.0f));

    // Hips：上半身前后倾（+ 前倾 => 绕 Z 负方向转）
    boneMat[1] = MatrixMultiply(boneMat[0], AroundHead(bones[1].head, MatrixRotateZ(-lean)));

    // Spine：呼吸起伏 + 左右倾
    const Matrix spineLocal = MatrixMultiply(MatrixTranslate(0.0f, pose.bodyBob, 0.0f),
        MatrixMultiply(MatrixRotateZ(-lean * 0.7f), MatrixRotateX(roll)));
    boneMat[2] = MatrixMultiply(boneMat[1], AroundHead(bones[2].head, spineLocal));

    // Head：点头 / 抬头
    boneMat[3] = MatrixMultiply(boneMat[2], AroundHead(bones[3].head, MatrixRotateZ(head)));

    // 两条腿：大腿绕胯摆，小腿绕膝弯（+ 摆角为向前；+ 屈膝为向后收）
    boneMat[4] = MatrixMultiply(boneMat[1], AroundHead(bones[4].head, MatrixRotateZ(legL)));
    boneMat[5] = MatrixMultiply(boneMat[4], AroundHead(bones[5].head, MatrixRotateZ(-knL)));
    boneMat[6] = MatrixMultiply(boneMat[1], AroundHead(bones[6].head, MatrixRotateZ(legR)));
    boneMat[7] = MatrixMultiply(boneMat[6], AroundHead(bones[7].head, MatrixRotateZ(-knR)));

    // 尾巴：左右甩 + 上下翘
    boneMat[8] = MatrixMultiply(boneMat[1], AroundHead(bones[8].head,
        MatrixMultiply(MatrixRotateY(sway), MatrixRotateZ(-lift))));
}

// ------------------------------------------------------------
//  蒙皮：CPU 混合顶点，并把顶点/法线推回显存
// ------------------------------------------------------------
void SoftRig::Apply(Mesh& mesh)
{
    if (!ready || mesh.vertexCount != vertexCount) return;
    if (mesh.vertices == nullptr || mesh.vboId == nullptr) return;

    const double t0 = GetTime();

    BuildMatrices();

    const float* bp = restPos.data();
    const float* bn = restNrm.empty() ? nullptr : restNrm.data();
    float* sp = skinPos.data();
    float* sn = skinNrm.empty() ? nullptr : skinNrm.data();

    for (int i = 0; i < vertexCount; ++i)
    {
        const int i3 = i * 3;
        const float bx = bp[i3 + 0], by = bp[i3 + 1], bz = bp[i3 + 2];

        // 法线只吃旋转部分（不带平移），所以单独取一份基法线
        float gx = 0.0f, gy = 0.0f, gz = 0.0f;
        if (bn) { gx = bn[i3 + 0]; gy = bn[i3 + 1]; gz = bn[i3 + 2]; }

        float px = 0.0f, py = 0.0f, pz = 0.0f;
        float nx = 0.0f, ny = 0.0f, nz = 0.0f;

        for (int k = 0; k < kInfluences; ++k)
        {
            const float w = vWeight[i * kInfluences + k];
            if (w <= 0.0f) continue;

            const Matrix& m = boneMat[vBone[i * kInfluences + k]];

            px += w * (m.m0 * bx + m.m4 * by + m.m8  * bz + m.m12);
            py += w * (m.m1 * bx + m.m5 * by + m.m9  * bz + m.m13);
            pz += w * (m.m2 * bx + m.m6 * by + m.m10 * bz + m.m14);

            if (bn)
            {
                nx += w * (m.m0 * gx + m.m4 * gy + m.m8  * gz);
                ny += w * (m.m1 * gx + m.m5 * gy + m.m9  * gz);
                nz += w * (m.m2 * gx + m.m6 * gy + m.m10 * gz);
            }
        }

        // 脚底不穿地钳制：自研骨架的关节轴和模型真实关节有偏差，跑 / 走到
        // 极端摆腿相位时脚底顶点会沉到脚底平面以下（插进地面）。这里只钳 y：
        // 任何顶点都不低于标定出的脚底平面 baseY（脚底板会被压平一点，但绝不穿地），
        // x / z 一律不动，横向形状和朝向不受影响。
        if (py < baseY) py = baseY;

        sp[i3 + 0] = px;  sp[i3 + 1] = py;  sp[i3 + 2] = pz;

        if (sn)
        {
            const float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len > 1e-6f) { nx /= len; ny /= len; nz /= len; }
            sn[i3 + 0] = nx;  sn[i3 + 1] = ny;  sn[i3 + 2] = nz;
        }
    }

    UpdateMeshBuffer(mesh, 0, sp, vertexCount * 3 * (int)sizeof(float), 0);
    if (sn) UpdateMeshBuffer(mesh, 2, sn, vertexCount * 3 * (int)sizeof(float), 0);

    lastCostMs = (float)((GetTime() - t0) * 1000.0);

    static bool loggedOnce = false;
    if (!loggedOnce)          // 只打一条：确认蒙皮真的跑起来了 + 首帧耗时
    {
        loggedOnce = true;
        TraceLog(LOG_INFO, "NAILONG-RIG: 首次蒙皮 %.2f ms（%d 顶点 x %d 骨混合）",
            lastCostMs, vertexCount, kInfluences);
    }
}