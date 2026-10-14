#include "FootstepAudio.h"

#include "Maze.h"
#include "raymath.h"

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

        return directory + "/" + child;
    }
}

// 在当前 exe 目录、工作目录及其父目录中寻找脚步资源。
// 这样无论从 Visual Studio、codes 目录还是 x64/Debug 启动，都能找到项目根目录的 assets。
std::string FootstepAudio::FindAssetPath(const char* fileName)
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
        std::string("assets/audio/") + fileName;
    const std::string projectRelative =
        std::string("codes/assets/audio/") + fileName;

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

// 初始化音频设备并加载两套脚步样本。
bool FootstepAudio::Load()
{
    if (ready_)
    {
        return true;
    }

    InitAudioDevice();
    audioDeviceReady_ = IsAudioDeviceReady();
    if (!audioDeviceReady_)
    {
        TraceLog(LOG_WARNING, "Footstep audio disabled: audio device is not ready.");
        return false;
    }

    const std::string patrolPath = FindAssetPath("footstep_patrol.ogg");
    const std::string chaserPath = FindAssetPath("footstep_chaser.ogg");
    if (patrolPath.empty() || chaserPath.empty())
    {
        TraceLog(LOG_WARNING, "Footstep audio disabled: wav asset not found.");
        CloseAudioDevice();
        audioDeviceReady_ = false;
        return false;
    }

    patrolSource_ = LoadSound(patrolPath.c_str());
    chaserSource_ = LoadSound(chaserPath.c_str());
    if (!IsSoundValid(patrolSource_) || !IsSoundValid(chaserSource_))
    {
        TraceLog(LOG_WARNING, "Footstep audio disabled: failed to load wav.");
        Unload();
        return false;
    }

    // 同一个 Sound 被连续 PlaySound 时容易互相打断，因此为每种脚步创建多个别名。
    for (int i = 0; i < FootstepConfig::VOICE_COUNT; ++i)
    {
        const Sound patrolAlias = LoadSoundAlias(patrolSource_);
        if (IsSoundValid(patrolAlias))
        {
            patrolVoices_.voices.push_back(patrolAlias);
        }

        const Sound chaserAlias = LoadSoundAlias(chaserSource_);
        if (IsSoundValid(chaserAlias))
        {
            chaserVoices_.voices.push_back(chaserAlias);
        }
    }

    rng_.seed(std::random_device{}());
    ready_ = true;
    return true;
}

// 停止声音并释放音频设备。
void FootstepAudio::Unload()
{
    StopAll();

    for (Sound& voice : patrolVoices_.voices)
    {
        if (IsSoundValid(voice))
        {
            UnloadSoundAlias(voice);
        }
    }
    for (Sound& voice : chaserVoices_.voices)
    {
        if (IsSoundValid(voice))
        {
            UnloadSoundAlias(voice);
        }
    }
    patrolVoices_.voices.clear();
    chaserVoices_.voices.clear();
    patrolVoices_.nextIndex = 0;
    chaserVoices_.nextIndex = 0;

    if (IsSoundValid(patrolSource_))
    {
        UnloadSound(patrolSource_);
    }
    if (IsSoundValid(chaserSource_))
    {
        UnloadSound(chaserSource_);
    }

    patrolSource_ = Sound{};
    chaserSource_ = Sound{};
    ready_ = false;

    if (audioDeviceReady_)
    {
        CloseAudioDevice();
        audioDeviceReady_ = false;
    }
}

// 暂停或离开游戏状态时立即停止所有脚步。
void FootstepAudio::StopAll()
{
    for (Sound& voice : patrolVoices_.voices)
    {
        if (IsSoundValid(voice))
        {
            StopSound(voice);
        }
    }
    for (Sound& voice : chaserVoices_.voices)
    {
        if (IsSoundValid(voice))
        {
            StopSound(voice);
        }
    }
}

// 判断玩家和敌人之间是否有墙。只要有采样点落在墙格，就按隔墙处理。
bool FootstepAudio::IsWallBlocked(const Maze& maze, Vector3 from, Vector3 to)
{
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float distance = sqrt(dx * dx + dz * dz);
    if (distance <= FootstepConfig::WALL_CHECK_STEP)
    {
        return false;
    }

    const int steps = max(
        1,
        static_cast<int>(ceil(distance / FootstepConfig::WALL_CHECK_STEP))
    );

    // 只检查中间采样点，起点和终点本来就在可行走区域内。
    for (int step = 1; step < steps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const Vector3 sample = {
            from.x + dx * t,
            0.0f,
            from.z + dz * t
        };

        if (maze.IsWorldPosBlocked(sample))
        {
            return true;
        }
    }

    return false;
}

// 轮询声音池：优先使用没在播放的别名，全部占用时复用最早的那个。
Sound* FootstepAudio::PickVoice(VoicePool& pool)
{
    if (pool.voices.empty())
    {
        return nullptr;
    }

    for (size_t offset = 0; offset < pool.voices.size(); ++offset)
    {
        const size_t index = (pool.nextIndex + offset) % pool.voices.size();
        if (!IsSoundPlaying(pool.voices[index]))
        {
            pool.nextIndex = (index + 1) % pool.voices.size();
            return &pool.voices[index];
        }
    }

    Sound* voice = &pool.voices[pool.nextIndex];
    pool.nextIndex = (pool.nextIndex + 1) % pool.voices.size();
    return voice;
}

// 播放单个脚步，完成距离衰减、墙体衰减、左右声像和随机化。
void FootstepAudio::PlayOne(
    EnemyType type,
    Vector3 enemyPosition,
    Vector3 playerPosition,
    const Camera3D& camera,
    const Maze& maze,
    float distance)
{
    const bool isChaser = type == EnemyType::Chaser;
    const float maxDistance = isChaser
        ? FootstepConfig::CHASER_MAX_HEAR_DISTANCE
        : FootstepConfig::PATROL_MAX_HEAR_DISTANCE;

    if (distance >= maxDistance)
    {
        return;
    }

    const float baseVolume = isChaser
        ? FootstepConfig::CHASER_BASE_VOLUME
        : FootstepConfig::PATROL_BASE_VOLUME;
    const float basePitch = isChaser
        ? FootstepConfig::CHASER_BASE_PITCH
        : FootstepConfig::PATROL_BASE_PITCH;

    float strength = 1.0f;
    if (distance > FootstepConfig::FULL_VOLUME_DISTANCE &&
        maxDistance > FootstepConfig::FULL_VOLUME_DISTANCE)
    {
        strength =
            (maxDistance - distance) /
            (maxDistance - FootstepConfig::FULL_VOLUME_DISTANCE);
    }
    strength = max(0.0f, min(strength, 1.0f));

    float volume = baseVolume *
        powf(strength, FootstepConfig::DISTANCE_FALLOFF_EXPONENT);

    if (IsWallBlocked(maze, playerPosition, enemyPosition))
    {
        volume *= FootstepConfig::WALL_MUFFLE_VOLUME;
    }

    std::uniform_real_distribution<float> volumeJitter(
        1.0f - FootstepConfig::VOLUME_RANDOM_RANGE,
        1.0f + FootstepConfig::VOLUME_RANDOM_RANGE
    );
    std::uniform_real_distribution<float> pitchJitter(
        1.0f - FootstepConfig::PITCH_RANDOM_RANGE,
        1.0f + FootstepConfig::PITCH_RANDOM_RANGE
    );
    volume *= volumeJitter(rng_);
    float pitch = basePitch * pitchJitter(rng_);
    pitch = max(0.35f, min(pitch, 2.5f));

    // 用摄像机右方向计算敌人是在左边还是右边。
    const Vector3 forward = Vector3Normalize(
        Vector3Subtract(camera.target, camera.position)
    );
    const Vector3 right = Vector3Normalize(
        Vector3CrossProduct(forward, camera.up)
    );

    Vector3 toEnemy = Vector3Subtract(enemyPosition, playerPosition);
    toEnemy.y = 0.0f;
    const float toEnemyLength = sqrt(toEnemy.x * toEnemy.x + toEnemy.z * toEnemy.z);
    float pan = 0.0f;
    if (toEnemyLength > 1e-4f)
    {
        toEnemy.x /= toEnemyLength;
        toEnemy.z /= toEnemyLength;
        pan = (toEnemy.x * right.x + toEnemy.z * right.z) *
            FootstepConfig::PAN_STRENGTH;
        pan = max(-1.0f, min(pan, 1.0f));
    }

    VoicePool& pool = isChaser ? chaserVoices_ : patrolVoices_;
    Sound* voice = PickVoice(pool);
    if (voice == nullptr)
    {
        return;
    }

    SetSoundVolume(*voice, max(0.0f, min(volume, 1.0f)));
    SetSoundPitch(*voice, pitch);
    SetSoundPan(*voice, pan);
    PlaySound(*voice);
}

// 每帧处理脚步事件。先按距离排序，再限制每帧最多播放的数量。
void FootstepAudio::Update(
    const std::vector<FootstepEvent>& events,
    Vector3 playerPosition,
    const Camera3D& camera,
    const Maze& maze)
{
    if (!ready_ || events.empty())
    {
        return;
    }

    struct Candidate
    {
        const FootstepEvent* event = nullptr;
        float distance = 0.0f;
    };

    std::vector<Candidate> patrolCandidates;
    std::vector<Candidate> chaserCandidates;
    patrolCandidates.reserve(events.size());
    chaserCandidates.reserve(events.size());

    for (const FootstepEvent& event : events)
    {
        const float dx = event.position.x - playerPosition.x;
        const float dz = event.position.z - playerPosition.z;
        const float distance = sqrt(dx * dx + dz * dz);
        const float maxDistance = event.type == EnemyType::Chaser
            ? FootstepConfig::CHASER_MAX_HEAR_DISTANCE
            : FootstepConfig::PATROL_MAX_HEAR_DISTANCE;

        if (distance >= maxDistance)
        {
            continue;
        }

        Candidate candidate;
        candidate.event = &event;
        candidate.distance = distance;
        if (event.type == EnemyType::Chaser)
        {
            chaserCandidates.push_back(candidate);
        }
        else
        {
            patrolCandidates.push_back(candidate);
        }
    }

    const auto byDistance = [](const Candidate& first, const Candidate& second)
    {
        return first.distance < second.distance;
    };
    std::sort(patrolCandidates.begin(), patrolCandidates.end(), byDistance);
    std::sort(chaserCandidates.begin(), chaserCandidates.end(), byDistance);

    const int patrolCount = min(
        static_cast<int>(patrolCandidates.size()),
        FootstepConfig::PATROL_STEPS_PER_FRAME
    );
    const int chaserCount = min(
        static_cast<int>(chaserCandidates.size()),
        FootstepConfig::CHASER_STEPS_PER_FRAME
    );

    for (int i = 0; i < patrolCount; ++i)
    {
        PlayOne(
            patrolCandidates[i].event->type,
            patrolCandidates[i].event->position,
            playerPosition,
            camera,
            maze,
            patrolCandidates[i].distance
        );
    }

    for (int i = 0; i < chaserCount; ++i)
    {
        PlayOne(
            chaserCandidates[i].event->type,
            chaserCandidates[i].event->position,
            playerPosition,
            camera,
            maze,
            chaserCandidates[i].distance
        );
    }
}

// 音频是否已经成功加载。
bool FootstepAudio::IsReady() const
{
    return ready_;
}
