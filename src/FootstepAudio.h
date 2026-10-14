#pragma once

#include "Enemy.h"
#include "raylib.h"

#include <random>
#include <string>
#include <vector>

// 脚步声音全部可调参数集中在这里。
// 调音量和距离时优先改这个命名空间，不需要翻播放代码。
namespace FootstepConfig
{
    // ===== 听觉距离与音量 =====
    // 小于这个距离按满音量计算，避免贴近时音量超过上限。
    constexpr float FULL_VOLUME_DISTANCE = 1.5f;
    // 巡逻怪最大可听距离，超过后完全静音。
    constexpr float PATROL_MAX_HEAR_DISTANCE = 18.0f;
    // 追击怪最大可听距离，比巡逻怪更远，体现追逐压迫感。
    constexpr float CHASER_MAX_HEAR_DISTANCE = 12.0f;
    // 巡逻怪基础音量，范围 0 到 1。
    constexpr float PATROL_BASE_VOLUME = 0.90f;
    // 追击怪基础音量，范围 0 到 1，通常比巡逻怪更大。
    constexpr float CHASER_BASE_VOLUME = 0.72f;
    // 距离衰减指数，越大远处衰减越快，推荐 1.5 到 2.5。
    constexpr float DISTANCE_FALLOFF_EXPONENT = 1.8f;

    // ===== 左右声道与墙体遮挡 =====
    // 左右声像最大强度，0 为完全居中，1 为完全偏到一侧。
    constexpr float PAN_STRENGTH = 0.75f;
    // 中间隔墙时音量乘以这个系数，越小越听不清。
    constexpr float WALL_MUFFLE_VOLUME = 0.30f;
    // 隔墙检测采样步长，单位世界坐标。
    constexpr float WALL_CHECK_STEP = 0.25f;

    // ===== 步伐节奏 =====
    // 巡逻怪走多远算一步，单位世界坐标。
    constexpr float PATROL_STRIDE = 1.05f;
    // 追击怪步伐更短，脚步更急促。
    constexpr float CHASER_STRIDE = 0.78f;

    // ===== 随机化，避免机械重复 =====
    // 播放时音高随机浮动范围，0.05 表示正负 5%。
    constexpr float PITCH_RANDOM_RANGE = 0.05f;
    // 播放时音量随机浮动范围，0.10 表示正负 10%。
    constexpr float VOLUME_RANDOM_RANGE = 0.10f;
    // 巡逻怪基础音高，1.0 为原始音高。
    constexpr float PATROL_BASE_PITCH = 1.0f;
    // 追击怪基础音高，小于 1 会更低沉、更有压迫感。
    constexpr float CHASER_BASE_PITCH = 0.86f;

    // ===== 并发播放限制 =====
    // 每种脚步保留的声音别名数量，也是同时最多叠加的脚步数量。
    constexpr int VOICE_COUNT = 8;
    // 每帧最多播放多少个巡逻怪脚步，避免多敌人叠加过响。
    constexpr int PATROL_STEPS_PER_FRAME = 2;
    // 每帧最多播放多少个追击怪脚步。
    constexpr int CHASER_STEPS_PER_FRAME = 3;
}

// 脚步声播放器：只负责把后台线程产生的脚步事件变成可听的空间化声音。
// 所有 raylib 音频调用都在主线程执行，避免和 EnemyManager 后台线程并发。
class FootstepAudio
{
public:
    // 初始化音频设备并加载两种脚步样本；失败时返回 false，游戏仍可无声运行。
    bool Load();
    // 停止所有脚步并释放音频资源。
    void Unload();
    // 立即停止所有正在播放的脚步，用于暂停、菜单和结算界面。
    void StopAll();
    // 根据后台线程产生的脚步事件播放带距离衰减和左右声道的脚步声。
    void Update(
        const std::vector<FootstepEvent>& events,
        Vector3 playerPosition,
        const Camera3D& camera,
        const Maze& maze
    );
    // 音频是否已经成功加载。
    bool IsReady() const;

private:
    // 同一个样本的多个声音别名，用来支持多个脚步同时播放。
    struct VoicePool
    {
        std::vector<Sound> voices;
        size_t nextIndex = 0;
    };

    // 从 exe 目录和当前工作目录向上寻找 assets/audio 下的音频资源。
    static std::string FindAssetPath(const char* fileName);
    // 判断玩家到敌人的直线是否被墙挡住。
    static bool IsWallBlocked(const Maze& maze, Vector3 from, Vector3 to);
    // 从声音池里取一个空闲或最早的声音别名。
    Sound* PickVoice(VoicePool& pool);
    // 播放一次脚步，distance 是已经算好的玩家到敌人平面距离。
    void PlayOne(
        EnemyType type,
        Vector3 enemyPosition,
        Vector3 playerPosition,
        const Camera3D& camera,
        const Maze& maze,
        float distance
    );

    Sound patrolSource_{};
    Sound chaserSource_{};
    VoicePool patrolVoices_;
    VoicePool chaserVoices_;
    bool ready_ = false;
    bool audioDeviceReady_ = false;
    std::mt19937 rng_;
};
