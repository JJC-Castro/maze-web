#include "SfxAudio.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

namespace
{
    // 拼接目录和文件名，自动补上缺失的分隔符。
    std::string JoinPath(const std::string& directory, const std::string& child)
    {
        if (directory.empty())
        {
            return child;
        }

        const char last = directory.back();
        if (last == '\\' || last == '/')
        {
            return directory + child;
        }

        return directory + "\\" + child;
    }

    // 生成 [-range, range] 范围内的随机浮点数。
    float RandomSigned(std::mt19937& rng, float range)
    {
        std::uniform_real_distribution<float> distribution(-range, range);
        return distribution(rng);
    }
}

// 在 exe 目录、工作目录及其父目录中寻找音效资源。
// 和脚步音频使用同一套查找规则，保证替换 assets/audio 下的文件即可生效。
std::string SfxAudio::FindAssetPath(const char* fileName)
{
    if (fileName == nullptr || fileName[0] == '\0')
    {
        return std::string();
    }

    std::vector<std::string> roots;

    const char* appDirectory = GetApplicationDirectory();
    if (appDirectory != nullptr && appDirectory[0] != '\0')
    {
        std::string directory(appDirectory);
        for (int level = 0; level < 6 && !directory.empty(); ++level)
        {
            roots.push_back(directory);

            const size_t slash = directory.find_last_of("\\/");
            if (slash == std::string::npos || slash == 0)
            {
                break;
            }
            directory = directory.substr(0, slash);
        }
    }

    const char* workingDirectory = GetWorkingDirectory();
    if (workingDirectory != nullptr && workingDirectory[0] != '\0')
    {
        roots.push_back(std::string(workingDirectory));
    }

    const std::string directRelative =
        std::string("assets\\audio\\") + fileName;
    const std::string projectRelative =
        std::string("codes\\assets\\audio\\") + fileName;

    for (const std::string& root : roots)
    {
        const std::string directPath = JoinPath(root, directRelative);
        if (FileExists(directPath.c_str()))
        {
            return directPath;
        }

        const std::string projectPath = JoinPath(root, projectRelative);
        if (FileExists(projectPath.c_str()))
        {
            return projectPath;
        }
    }

    return std::string();
}

// 加载一个音效文件和它的声音别名。
bool SfxAudio::LoadBank(SoundBank& bank, const char* fileName)
{
    const std::string path = FindAssetPath(fileName);
    if (path.empty())
    {
        TraceLog(LOG_WARNING, "Sfx audio asset not found: %s", fileName);
        return false;
    }

    bank.source = LoadSound(path.c_str());
    if (!IsSoundValid(bank.source))
    {
        TraceLog(LOG_WARNING, "Sfx audio failed to load: %s", fileName);
        return false;
    }

    for (int i = 0; i < SfxConfig::VOICE_COUNT_PER_SFX; ++i)
    {
        const Sound alias = LoadSoundAlias(bank.source);
        if (IsSoundValid(alias))
        {
            bank.voices.push_back(alias);
        }
    }

    return !bank.voices.empty();
}

// 卸载一个音效文件和它的所有别名。
void SfxAudio::UnloadBank(SoundBank& bank)
{
    for (Sound& voice : bank.voices)
    {
        if (IsSoundValid(voice))
        {
            StopSound(voice);
            UnloadSoundAlias(voice);
        }
    }
    bank.voices.clear();
    bank.nextIndex = 0;

    if (IsSoundValid(bank.source))
    {
        UnloadSound(bank.source);
    }
    bank.source = Sound{};
}

// 初始化音频并加载全部普通游戏音效。
// 如果 footsteps 已经初始化过音频设备，这里只复用，不重复关闭。
bool SfxAudio::Load()
{
    if (ready_)
    {
        return true;
    }

    ownsAudioDevice_ = false;
    if (!IsAudioDeviceReady())
    {
        InitAudioDevice();
        ownsAudioDevice_ = IsAudioDeviceReady();
    }

    if (!IsAudioDeviceReady())
    {
        TraceLog(LOG_WARNING, "Sfx audio disabled: audio device is not ready.");
        return false;
    }

    bool anyLoaded = false;
    anyLoaded = LoadBank(goldPickup_, "treasure_gold.wav") || anyLoaded;
    anyLoaded = LoadBank(attackPickup_, "treasure_blue.wav") || anyLoaded;
    anyLoaded = LoadBank(healPickup_, "treasure_red.wav") || anyLoaded;
    anyLoaded = LoadBank(playerDamage_, "player_damage.wav") || anyLoaded;
    anyLoaded = LoadBank(orbFire_, "orb_fire.wav") || anyLoaded;
    anyLoaded = LoadBank(sprintStart_, "player_sprint_start.wav") || anyLoaded;
    anyLoaded = LoadBank(playerDeath_, "player_death.wav") || anyLoaded;
    anyLoaded = LoadBank(playerJump_, "player_jump.wav") || anyLoaded;
    anyLoaded = LoadBank(slowZoneEnter_, "slow_zone_enter.wav") || anyLoaded;
    anyLoaded = LoadBank(mineExplode_, "mine_explode.wav") || anyLoaded;
    anyLoaded = LoadBank(playerFootstep_, "player_footstep.wav") || anyLoaded;
    anyLoaded = LoadBank(playerFootstepSticky_, "player_footstep_sticky.wav") || anyLoaded;
    anyLoaded = LoadBank(playerLand_, "player_land.wav") || anyLoaded;
    anyLoaded = LoadBank(playerPant_, "player_pant.wav") || anyLoaded;
    anyLoaded = LoadBank(enemyDefeat_, "enemy_defeat.wav") || anyLoaded;
    anyLoaded = LoadBank(gameStart_, "game_start.wav") || anyLoaded;
    anyLoaded = LoadBank(gameClear_, "game_clear.wav") || anyLoaded;
    anyLoaded = LoadBank(uiClick_, "ui_click.wav") || anyLoaded;
    anyLoaded = LoadBank(nailongLaugh_, "nailong_laugh.wav") || anyLoaded;
    anyLoaded = LoadBank(nailongChirp_, "nailong_chirp.wav") || anyLoaded;

    // ---- 背景音乐：四个难度各一首，流式加载并打开循环 ----
    // 找不到文件只是没有背景音乐，不影响其它音效与游戏运行
    const char* backgroundFiles[SfxConfig::BACKGROUND_COUNT] = {
        "bgm_easy.wav", "bgm_normal.wav", "bgm_hard.wav", "bgm_hell.wav"
    };
    for (int i = 0; i < SfxConfig::BACKGROUND_COUNT; ++i)
    {
        const std::string path = FindAssetPath(backgroundFiles[i]);
        if (path.empty())
        {
            TraceLog(LOG_WARNING, "BGM asset not found: %s", backgroundFiles[i]);
            continue;
        }

        background_[i] = LoadMusicStream(path.c_str());
        if (!IsMusicValid(background_[i]))
        {
            TraceLog(LOG_WARNING, "BGM failed to load: %s", backgroundFiles[i]);
            continue;
        }

        background_[i].looping = true;   // 无限循环，直到暂停/结算/退出
        SetMusicVolume(background_[i], SfxConfig::BGM_VOLUME);
    }

    if (!anyLoaded)
    {
        if (ownsAudioDevice_)
        {
            CloseAudioDevice();
            ownsAudioDevice_ = false;
        }
        return false;
    }

    rng_.seed(std::random_device{}());
    ready_ = true;
    return true;
}

// 停止并释放所有普通游戏音效。
void SfxAudio::Unload()
{
    StopAll();

    UnloadBank(goldPickup_);
    UnloadBank(attackPickup_);
    UnloadBank(healPickup_);
    UnloadBank(playerDamage_);
    UnloadBank(orbFire_);
    UnloadBank(sprintStart_);
    UnloadBank(playerDeath_);
    UnloadBank(playerJump_);
    UnloadBank(slowZoneEnter_);
    UnloadBank(mineExplode_);
    UnloadBank(playerFootstep_);
    UnloadBank(playerFootstepSticky_);
    UnloadBank(playerLand_);
    UnloadBank(playerPant_);
    UnloadBank(enemyDefeat_);
    UnloadBank(gameStart_);
    UnloadBank(gameClear_);
    UnloadBank(uiClick_);
    UnloadBank(nailongLaugh_);
    UnloadBank(nailongChirp_);
    // 背景音乐：停止并释放
    for (int i = 0; i < SfxConfig::BACKGROUND_COUNT; ++i)
    {
        if (IsMusicValid(background_[i]))
        {
            StopMusicStream(background_[i]);
            UnloadMusicStream(background_[i]);
        }
        background_[i] = Music{};
    }
    currentBackground_ = -1;
    backgroundPaused_ = false;

    ready_ = false;
    damageSoundCooldown_ = 0.0f;

    if (ownsAudioDevice_ && IsAudioDeviceReady())
    {
        CloseAudioDevice();
    }
    ownsAudioDevice_ = false;
}

// 暂停、菜单和结算界面调用：停止游戏内音效，但保留 UI 点击音效播放完。
void SfxAudio::StopGameplaySounds()
{
    for (SoundBank* bank : {
        &goldPickup_,
        &attackPickup_,
        &healPickup_,
        &playerDamage_,
        &orbFire_,
        &sprintStart_,
        &playerDeath_,
        &playerJump_,
        &slowZoneEnter_,
        &mineExplode_,
        &playerFootstep_,
        &playerFootstepSticky_,
        &playerLand_,
        &playerPant_,
        &enemyDefeat_,
        &gameStart_,
        &gameClear_
    })
    {
        for (Sound& voice : bank->voices)
        {
            if (IsSoundValid(voice))
            {
                StopSound(voice);
            }
        }
    }

    damageSoundCooldown_ = 0.0f;
    pantLoopActive_ = false;   // 暂停 / 菜单时不再循环喘气
    pantGapTimer_ = 0.0f;
    stickyLoopActive_ = false;   // 暂停 / 菜单时不再循环发黏脚步
    stickyGapTimer_ = 0.0f;
    footstepFadeTimer_ = 0.0f;
}

// 退出或释放资源时调用：连 UI 点击音效也一起停止。
void SfxAudio::StopAll()
{
    StopGameplaySounds();

    for (Sound& voice : uiClick_.voices)
    {
        if (IsSoundValid(voice))
        {
            StopSound(voice);
        }
    }
}

// 每帧推进受伤音效冷却与两种循环音效（喘气 / 减速带发黏脚步）。
void SfxAudio::Update(float dt)
{
    damageSoundCooldown_ = std::max(0.0f, damageSoundCooldown_ - dt);

    // 喘气：能量低于一半期间持续循环
    UpdateLoopingSound(playerPant_, pantLoopActive_,
        SfxConfig::PLAYER_PANT_LOOP_GAP, SfxConfig::PLAYER_PANT_VOLUME,
        pantGapTimer_, dt);

    // 减速带发黏脚步：站在减速带上期间持续循环
    UpdateLoopingSound(playerFootstepSticky_, stickyLoopActive_,
        SfxConfig::PLAYER_FOOTSTEP_STICKY_LOOP_GAP, SfxConfig::PLAYER_FOOTSTEP_STICKY_VOLUME,
        stickyGapTimer_, dt);
    // 停步淡出：把还在响的脚步音量快速拉到 0，再彻底停掉
    if (footstepFadeTimer_ > 0.0f)
    {
        footstepFadeTimer_ = std::max(0.0f, footstepFadeTimer_ - dt);
        const float fadeTotal = SfxConfig::FOOTSTEP_FADE_SECONDS;
        const float fadeK = (fadeTotal > 0.0f) ? (footstepFadeTimer_ / fadeTotal) : 0.0f;

        for (Sound& voice : playerFootstep_.voices)
        {
            if (!IsSoundValid(voice) || !IsSoundPlaying(voice))
            {
                continue;
            }

            if (footstepFadeTimer_ <= 0.0f)
            {
                StopSound(voice);
            }
            else
            {
                SetSoundVolume(voice, SfxConfig::PLAYER_FOOTSTEP_VOLUME * fadeK);
            }
        }
    }
}

// 冲刺能量低于一半时打开喘气循环；回到一半以上或结算时关闭
void SfxAudio::SetPlayerPantLoop(bool on)
{
    if (pantLoopActive_ == on)
    {
        return;
    }

    pantLoopActive_ = on;
    pantGapTimer_ = 0.0f;

    if (!on)
    {
        StopBankVoices(playerPant_);
    }
}

// 站在减速带上时打开发黏脚步循环；离开减速带或结算时关闭
void SfxAudio::SetPlayerStickyLoop(bool on)
{
    if (stickyLoopActive_ == on)
    {
        return;
    }

    stickyLoopActive_ = on;
    stickyGapTimer_ = 0.0f;

    if (!on)
    {
        StopBankVoices(playerFootstepSticky_);
    }
}

// 循环播放一个短音效：active 为真时持续补播（间隔 gap），为假时什么也不做
void SfxAudio::UpdateLoopingSound(SoundBank& bank, bool active, float gap, float volume,
                                  float& gapTimer, float dt)
{
    if (!active)
    {
        return;
    }

    for (const Sound& voice : bank.voices)
    {
        if (IsSoundValid(voice) && IsSoundPlaying(voice))
        {
            gapTimer = gap;
            return;
        }
    }

    gapTimer -= dt;
    if (gapTimer <= 0.0f)
    {
        PlayBank(bank, volume);
        gapTimer = gap;
    }
}

// 停止一个 SoundBank 里所有正在播放的声音
void SfxAudio::StopBankVoices(SoundBank& bank)
{
    for (Sound& voice : bank.voices)
    {
        if (IsSoundValid(voice))
        {
            StopSound(voice);
        }
    }
}

// 从声音池里取一个空闲或最早的声音别名。
Sound* SfxAudio::PickVoice(SoundBank& bank)
{
    if (bank.voices.empty())
    {
        return nullptr;
    }

    for (size_t offset = 0; offset < bank.voices.size(); ++offset)
    {
        const size_t index = (bank.nextIndex + offset) % bank.voices.size();
        if (!IsSoundPlaying(bank.voices[index]))
        {
            bank.nextIndex = (index + 1) % bank.voices.size();
            return &bank.voices[index];
        }
    }

    Sound* voice = &bank.voices[bank.nextIndex];
    bank.nextIndex = (bank.nextIndex + 1) % bank.voices.size();
    return voice;
}

// 播放一个音效，并加入轻微音高随机化。
void SfxAudio::PlayBank(SoundBank& bank, float volume)
{
    if (!ready_ || bank.voices.empty())
    {
        return;
    }

    Sound* voice = PickVoice(bank);
    if (voice == nullptr)
    {
        return;
    }

    const float pitch = 1.0f +
        RandomSigned(rng_, SfxConfig::PITCH_RANDOM_RANGE);
    SetSoundVolume(*voice, std::max(0.0f, std::min(volume, 1.0f)));
    SetSoundPitch(*voice, pitch);
    SetSoundPan(*voice, 0.0f);
    PlaySound(*voice);
}

// 普通游戏音效的播放入口。
void SfxAudio::PlayGoldPickup()
{
    PlayBank(goldPickup_, SfxConfig::GOLD_PICKUP_VOLUME);
}

void SfxAudio::PlayAttackPickup()
{
    PlayBank(attackPickup_, SfxConfig::ATTACK_PICKUP_VOLUME);
}

void SfxAudio::PlayHealPickup()
{
    PlayBank(healPickup_, SfxConfig::HEAL_PICKUP_VOLUME);
}

void SfxAudio::PlayPlayerDamage()
{
    if (!ready_ || damageSoundCooldown_ > 0.0f)
    {
        return;
    }

    PlayBank(playerDamage_, SfxConfig::PLAYER_DAMAGE_VOLUME);
    damageSoundCooldown_ = SfxConfig::PLAYER_DAMAGE_SOUND_COOLDOWN;
}

void SfxAudio::PlayOrbFire()
{
    PlayBank(orbFire_, SfxConfig::ORB_FIRE_VOLUME);
}

void SfxAudio::PlaySprintStart()
{
    PlayBank(sprintStart_, SfxConfig::SPRINT_START_VOLUME);
}

void SfxAudio::PlayPlayerDeath()
{
    PlayBank(playerDeath_, SfxConfig::PLAYER_DEATH_VOLUME);
}

void SfxAudio::PlaySlowZoneEnter()
{
    PlayBank(slowZoneEnter_, SfxConfig::SLOW_ZONE_ENTER_VOLUME);
}

void SfxAudio::PlayMineExplode()
{
    PlayBank(mineExplode_, SfxConfig::MINE_EXPLODE_VOLUME);
}

void SfxAudio::PlayPlayerJump()
{
    PlayBank(playerJump_, SfxConfig::PLAYER_JUMP_VOLUME);
}

void SfxAudio::PlayEnemyDefeat()
{
    PlayBank(enemyDefeat_, SfxConfig::ENEMY_DEFEAT_VOLUME);
}

void SfxAudio::PlayGameStart()
{
    PlayBank(gameStart_, SfxConfig::GAME_START_VOLUME);
}

void SfxAudio::PlayGameClear()
{
    PlayBank(gameClear_, SfxConfig::GAME_CLEAR_VOLUME);
}

void SfxAudio::PlayUiClick()
{
    PlayBank(uiClick_, SfxConfig::UI_CLICK_VOLUME);
}

// 奶龙大笑：F 键触发，按一下播一次
void SfxAudio::PlayNailongLaugh()
{
    PlayBank(nailongLaugh_, SfxConfig::NAILONG_LAUGH_VOLUME);
}

// 奶龙短叫：起跳时来一声
void SfxAudio::PlayNailongChirp()
{
    PlayBank(nailongChirp_, SfxConfig::NAILONG_CHIRP_VOLUME);
}

// 玩家普通脚步（减速带上的发黏声由 SetPlayerStickyLoop 循环播放）
void SfxAudio::PlayPlayerFootstep()
{
    footstepFadeTimer_ = 0.0f;   // 重新起步：取消可能还在进行的淡出
    PlayBank(playerFootstep_, SfxConfig::PLAYER_FOOTSTEP_VOLUME);
}

// 停步 / 离地时调用：把正在播放的脚步快速淡出收住
void SfxAudio::StopPlayerFootsteps()
{
    if (!ready_ || footstepFadeTimer_ > 0.0f)
    {
        return;
    }

    for (const Sound& voice : playerFootstep_.voices)
    {
        if (IsSoundValid(voice) && IsSoundPlaying(voice))
        {
            footstepFadeTimer_ = SfxConfig::FOOTSTEP_FADE_SECONDS;
            return;
        }
    }
}

// 玩家落地：按落地前的竖直速度调音量与音高，落得越狠越响、越低沉
void SfxAudio::PlayPlayerLand(float fallSpeed)
{
    if (!ready_ || playerLand_.voices.empty())
    {
        return;
    }

    const float speed = std::max(0.0f, -fallSpeed);
    float weight = speed / SfxConfig::PLAYER_LAND_MAX_FALL_SPEED;
    if (weight > 1.0f) weight = 1.0f;

    Sound* voice = PickVoice(playerLand_);
    if (voice == nullptr)
    {
        return;
    }

    const float volume = SfxConfig::PLAYER_LAND_VOLUME * (0.60f + 0.40f * weight);
    const float pitch = 1.10f - 0.25f * weight;
    SetSoundVolume(*voice, std::max(0.0f, std::min(volume, 1.0f)));
    SetSoundPitch(*voice, pitch);
    SetSoundPan(*voice, 0.0f);
    PlaySound(*voice);
}

// 冲刺能量掉到一半以下时的喘气
void SfxAudio::PlayPlayerPant()
{
    PlayBank(playerPant_, SfxConfig::PLAYER_PANT_VOLUME);
}

// 背景音乐：切换到指定难度并从头播放
void SfxAudio::PlayBackground(int index)
{
    if (index < 0 || index >= SfxConfig::BACKGROUND_COUNT)
    {
        return;
    }

    // 已经是同一首：只保证它处于播放状态
    if (currentBackground_ == index)
    {
        ResumeBackground();
        return;
    }

    StopBackground();

    if (!IsMusicValid(background_[index]))
    {
        return;
    }

    currentBackground_ = index;
    backgroundPaused_ = false;
    SetMusicVolume(background_[index], SfxConfig::BGM_VOLUME);
    PlayMusicStream(background_[index]);
}

// 停止并复位：结算、返回菜单、退出时调用
void SfxAudio::StopBackground()
{
    if (currentBackground_ >= 0 && IsMusicValid(background_[currentBackground_]))
    {
        StopMusicStream(background_[currentBackground_]);
    }

    currentBackground_ = -1;
    backgroundPaused_ = false;
}

// 暂停时挂起（暂停菜单 / 排名页）
void SfxAudio::PauseBackground()
{
    if (currentBackground_ < 0 || backgroundPaused_) return;
    if (!IsMusicValid(background_[currentBackground_])) return;
    if (!IsMusicStreamPlaying(background_[currentBackground_])) return;

    PauseMusicStream(background_[currentBackground_]);
    backgroundPaused_ = true;
}

// 回到游戏时恢复
void SfxAudio::ResumeBackground()
{
    if (currentBackground_ < 0 || !backgroundPaused_) return;
    if (!IsMusicValid(background_[currentBackground_])) return;

    ResumeMusicStream(background_[currentBackground_]);
    backgroundPaused_ = false;
}

// 每帧推进流式缓冲（暂停时不推进）
void SfxAudio::UpdateBackground()
{
    if (currentBackground_ < 0 || backgroundPaused_) return;
    if (!IsMusicValid(background_[currentBackground_])) return;

    UpdateMusicStream(background_[currentBackground_]);
}
// 音效是否已经成功加载。
bool SfxAudio::IsReady() const
{
    return ready_;
}
