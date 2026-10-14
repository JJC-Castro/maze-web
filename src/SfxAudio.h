#pragma once

#include "raylib.h"

#include <random>
#include <string>
#include <vector>

// 普通游戏音效全部可调参数集中在这里。
// 这里放拾取宝物、玩家受伤、发射光球等二维反馈音效，和脚步声分开管理。
namespace SfxConfig
{
    // ===== 每类音效的基础音量，范围 0 到 1 =====
    constexpr float GOLD_PICKUP_VOLUME = 0.65f;   // 金色宝物：加分音效
    constexpr float ATTACK_PICKUP_VOLUME = 0.65f; // 蓝色宝物：加攻击次数音效
    constexpr float HEAL_PICKUP_VOLUME = 0.60f;   // 红色宝物：回血音效
    constexpr float PLAYER_DAMAGE_VOLUME = 0.82f; // 玩家受伤音效
    constexpr float ORB_FIRE_VOLUME = 0.58f;      // 发射光球音效
    constexpr float SPRINT_START_VOLUME = 0.60f;  // 玩家刚进入加速跑时的音效
    constexpr float PLAYER_DEATH_VOLUME = 0.90f;  // 玩家死亡音效
    constexpr float PLAYER_JUMP_VOLUME = 0.55f;   // 玩家跳跃音效
    constexpr float SLOW_ZONE_ENTER_VOLUME = 0.60f; // 进入减速带的提示音
    constexpr float MINE_EXPLODE_VOLUME = 0.85f;  // 地雷爆炸音效
    constexpr float PLAYER_FOOTSTEP_VOLUME = 0.35f;        // 玩家普通脚步
    constexpr float PLAYER_FOOTSTEP_STICKY_VOLUME = 0.40f; // 玩家踩在减速带上的发黏脚步
    constexpr float PLAYER_FOOTSTEP_STICKY_LOOP_GAP = 0.0f; // 发黏脚步循环之间的间隔 (s)，0 = 无缝连续循环
    constexpr float FOOTSTEP_FADE_SECONDS = 0.035f;         // 停步时脚步的淡出时间 (s)，避免拖音和爆音
    constexpr float PLAYER_LAND_VOLUME = 0.55f;            // 玩家落地
    constexpr float PLAYER_PANT_VOLUME = 0.80f;            // 冲刺能量过半时的喘气
    constexpr float PLAYER_PANT_LOOP_GAP = 0.0f;           // 喘气循环之间的间隔 (s)，0 = 无缝连续循环
    constexpr float PLAYER_LAND_MAX_FALL_SPEED = 11.0f;    // 落地速度映射上限（调音量/音高用）
    constexpr float BGM_VOLUME = 0.80f;                    // 背景音乐总音量（0~1）

    // 背景音乐数量：简单 / 普通 / 困难 / 地狱，各一首，循环播放
    constexpr int BACKGROUND_COUNT = 4;
    constexpr float ENEMY_DEFEAT_VOLUME = 0.72f;  // 击败敌人音效
    constexpr float GAME_START_VOLUME = 0.70f;    // 游戏开始音效
    constexpr float GAME_CLEAR_VOLUME = 0.85f;    // 通关成功音效
    constexpr float UI_CLICK_VOLUME = 0.45f;      // UI 点击/确认音效
    constexpr float NAILONG_LAUGH_VOLUME = 0.85f; // 奶龙大笑音效
    constexpr float NAILONG_CHIRP_VOLUME = 0.70f; // 奶龙短叫音效

    // ===== 触发间隔 =====
    // 玩家受伤音效最短间隔，避免多个敌人同一帧攻击时连续叠音。
    constexpr float PLAYER_DAMAGE_SOUND_COOLDOWN = 0.18f;

    // ===== 随机化，避免同一个音效反复播放时听起来机械 =====
    constexpr float PITCH_RANDOM_RANGE = 0.04f; // 音高随机浮动正负 4%

    // ===== 并发播放 =====
    // 每种音效保留的声音别名数量，也是同时最多叠加的份数。
    constexpr int VOICE_COUNT_PER_SFX = 4;
}

// 普通游戏音效播放器。
// 所有 raylib 音频调用都在主线程执行，避免和 EnemyManager 后台线程并发。
class SfxAudio
{
public:
    // 初始化音频设备并加载五个音效；失败时返回 false，游戏仍可无声运行。
    bool Load();
    // 停止所有音效并释放音频资源。
    void Unload();
    // 只停止游戏内音效，不打断菜单 UI 点击音效。
    void StopGameplaySounds();
    // 立即停止所有正在播放的音效，用于退出和资源释放。
    void StopAll();
    // 每帧推进受伤音效冷却等计时。
    void Update(float dt);

    // 普通游戏音效接口。
    void PlayGoldPickup();
    void PlayAttackPickup();
    void PlayHealPickup();
    void PlayPlayerDamage();
    void PlayOrbFire();
    void PlaySprintStart();
    void PlayPlayerDeath();
    void PlayPlayerJump();
    void PlaySlowZoneEnter();      // 玩家进入减速带时播放
    void PlayMineExplode();        // 玩家踩到地雷爆炸时播放
    // 玩家普通脚步声（走路 / 跑步每次落步调用一次）
    void PlayPlayerFootstep();
    // 停步 / 离地时调用：把正在播放的脚步立刻淡出收住
    void StopPlayerFootsteps();
    // 站在减速带上时开启/关闭"发黏脚步"循环（在上面期间会一直循环播放）
    void SetPlayerStickyLoop(bool on);
    // 玩家落地：fallSpeed 是落地前的竖直速度（负值），越狠越响、音高越低
    void PlayPlayerLand(float fallSpeed);
    // 冲刺能量低于一半时开始/停止喘气循环（低于一半会一直循环播放）
    void SetPlayerPantLoop(bool on);
    void PlayPlayerPant();

    // ---- 背景音乐：四个难度各一首，循环播放 ----
    void PlayBackground(int index);   // 0..3 = 简单 / 普通 / 困难 / 地狱
    void StopBackground();            // 停止并复位（结算 / 返回菜单 / 退出）
    void PauseBackground();           // 暂停时挂起（暂停 / 排名）
    void ResumeBackground();          // 继续游戏时恢复
    void UpdateBackground();          // 每帧推进流式缓冲
    void PlayEnemyDefeat();
    void PlayGameStart();
    void PlayGameClear();
    void PlayUiClick();
    void PlayNailongLaugh();   // 奶龙大笑（F 键触发）
    void PlayNailongChirp();   // 奶龙短叫（起跳时触发）

    // 音效是否已经成功加载。
    bool IsReady() const;

private:
    // 一个音效样本和它的多个声音别名。
    struct SoundBank
    {
        Sound source{};
        std::vector<Sound> voices;
        size_t nextIndex = 0;
    };

    // 从 exe 目录和当前工作目录向上寻找 assets/audio 下的音频资源。
    static std::string FindAssetPath(const char* fileName);
    // 加载一个音效文件，并创建多个声音别名。
    bool LoadBank(SoundBank& bank, const char* fileName);
    // 卸载一个音效文件和它的所有别名。
    void UnloadBank(SoundBank& bank);
    // 从声音池里取一个空闲或最早的声音别名。
    Sound* PickVoice(SoundBank& bank);
    // 播放一个音效。
    void PlayBank(SoundBank& bank, float volume);
    // 循环播放一个短音效：active 为真时持续补播，为假时立即停止
    void UpdateLoopingSound(SoundBank& bank, bool active, float gap, float volume,
                            float& gapTimer, float dt);
    // 停止一个 SoundBank 里所有正在播放的声音
    void StopBankVoices(SoundBank& bank);

    SoundBank goldPickup_;
    SoundBank attackPickup_;
    SoundBank healPickup_;
    SoundBank playerDamage_;
    SoundBank orbFire_;
    SoundBank sprintStart_;
    SoundBank playerDeath_;
    SoundBank playerJump_;
    SoundBank slowZoneEnter_;
    SoundBank mineExplode_;
    SoundBank playerFootstep_;
    SoundBank playerFootstepSticky_;
    SoundBank playerLand_;
    SoundBank playerPant_;
    bool  pantLoopActive_ = false;   // 是否处于"低于一半能量"的喘气循环中
    float pantGapTimer_ = 0.0f;      // 两次喘气之间的间隔计时
    bool  stickyLoopActive_ = false; // 是否处于"站在减速带上"的发黏脚步循环中
    float stickyGapTimer_ = 0.0f;    // 两次发黏脚步之间的间隔计时
    float footstepFadeTimer_ = 0.0f; // >0 表示正在把脚步快速淡出

    // 背景音乐（流式，支持很长的音频；looping 打开后会一直重复）
    Music background_[SfxConfig::BACKGROUND_COUNT]{};
    int currentBackground_ = -1;
    bool backgroundPaused_ = false;
    SoundBank enemyDefeat_;
    SoundBank gameStart_;
    SoundBank gameClear_;
    SoundBank uiClick_;
    SoundBank nailongLaugh_;
    SoundBank nailongChirp_;

    bool ready_ = false;
    bool ownsAudioDevice_ = false;
    float damageSoundCooldown_ = 0.0f;
    std::mt19937 rng_;
};
