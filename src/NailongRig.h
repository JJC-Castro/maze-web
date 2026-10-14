#pragma once
#include "raylib.h"

#include <vector>

// ============================================================
//  NailongRig.h —— 奶龙的「软件层骨架」（标定 + CPU 蒙皮）
//
//  为什么要它：
//    assets/models/nailong.glb 是 Tripo3D 生成的**静态网格**
//    （bones = 0, animations = 0），直接加载只会“站着不动”。
//    这里不依赖任何外部建模工具，直接在游戏里按几何自动标定一副
//    9 根骨头的小骨架 + 蒙皮权重，然后每帧在 CPU 上重算顶点，
//    于是这个静态模型也能走 / 跑 / 跳 / 甩尾 / 呼吸。
//
//  坐标系约定（模型局部空间，和 glb 一致）：
//    上 = +Y，正面 = +X，左脚 = -Z、右脚 = +Z，原点 = 脚底
//
//  对外接口（只有 4 个）：
//    bool Init(const Mesh& mesh);      标定骨架 + 权重（加载模型后调一次，
//                                      约 10~40 ms；失败返回 false，不致命）
//    void Unload();                    释放
//    void SetPose(const NailongPose&); 每帧喂关节角（度 / 米）
//    void Apply(Mesh& mesh);           蒙皮 + 回写显存（Draw 之前调用）
// ============================================================

// ---- 关节姿态（度；位移单位米）----
//  这些角度的语义与「程序化奶龙」完全一致，两边共用同一套动作数据。
struct NailongPose
{
    float legL = 0.0f, legR = 0.0f;         // 大腿摆角，+ 为向前
    float kneeL = 0.0f, kneeR = 0.0f;       // 小腿弯曲，+ 为向后收
    float armL = 0.0f, armR = 0.0f;         // 大臂摆角，+ 为向前（仅程序化奶龙用）
    float foreL = 0.0f, foreR = 0.0f;       // 小臂弯曲（仅程序化奶龙用）
    float bodyLean = 0.0f;                  // 上身前后倾，+ 为前倾
    float bodyRoll = 0.0f;                  // 上身左右倾
    float bodyBob = 0.0f;                   // 身体上下浮动（米）
    float headPitch = 0.0f;                 // 头部俯仰，+ 为抬头
    float scaleY = 1.0f;                    // 整体拉伸（跳跃蓄力 / 滞空）
    float tailSway = 0.0f;                  // 尾巴左右甩（度）
    float tailLift = 0.0f;                  // 尾巴上下抬（度，+ 为翘起）
};

class SoftRig
{
public:
    bool Init(const Mesh& mesh);        // 标定（脚底 y=0 的静态网格）
    void Unload();
    bool Ready() const { return ready; }

    void SetPose(const NailongPose& p) { pose = p; }
    void Apply(Mesh& mesh);             // CPU 蒙皮并把顶点/法线推回显存

    float LastCostMs() const { return lastCostMs; }   // 上次蒙皮耗时（调试用）
    int   VertexCount() const { return vertexCount; }

private:
    // 骨骼：head 是关节（旋转中心），tail 只用来算权重（骨段）
    struct Bone
    {
        Vector3 head{ 0.0f, 0.0f, 0.0f };
        Vector3 tail{ 0.0f, 0.0f, 0.0f };
        int     parent = -1;
    };

    static constexpr int kMaxBones   = 9;   // 含 0 号 Root（不参与蒙皮）
    static constexpr int kInfluences = 3;   // 每顶点最多 3 根骨头混合

    void BuildMatrices();                   // 姿态 -> 每根骨头的世界矩阵

    bool  ready = false;
    float height = 1.0f;                    // 标定时的模型高度（诊断用）
    float baseY = 0.0f;                     // 网格最低点 y（脚底平面）：体型缩放绕它做
    Bone  bones[kMaxBones]{};
    Matrix boneMat[kMaxBones]{};

    NailongPose pose{};

    int vertexCount = 0;
    std::vector<float> restPos;             // 原始顶点（不动）
    std::vector<float> restNrm;
    std::vector<float> skinPos;             // 蒙皮结果
    std::vector<float> skinNrm;
    std::vector<int>   vBone;               // vertexCount * kInfluences
    std::vector<float> vWeight;
    float lastCostMs = 0.0f;
};