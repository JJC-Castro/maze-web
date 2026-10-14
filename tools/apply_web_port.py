# -*- coding: utf-8 -*-
"""apply_web_port.py —— 把桌面版源码移植成 Web 版（Emscripten）可编译的样子。

设计原则：
  · 改动全部藏在 `#ifdef MAZE_WEB` 里，桌面版语义零变化
  · 每个补丁都带锚点断言：锚点对不上就直接抛错退出，绝不"改一半"
  · 脚本本身留在仓库里，等于一份可复现的移植记录

用法：
    python tools/apply_web_port.py --check    # 只检查锚点是否都在，不写文件
    python tools/apply_web_port.py            # 就地打补丁

已处理的平台差异：
  1. EnemyManager  后台线程 -> 主线程同步一拍（浏览器里没必要也不想开线程）
  2. RlTelemetry   _mkdir / fopen_s / <direct.h> -> 跨平台写法
  3. RLNet         fopen_s -> fopen
  4. Account       _get_pgmptr / localtime_s -> 跨平台；log.csv 落到 IndexedDB 挂载点
  5. GameTimer     strncpy_s / _TRUNCATE -> snprintf
  6. main.cpp      权重目录 _get_pgmptr -> /assets/rl
  7. SfxAudio / FootstepAudio  音频目录拼接用反斜杠 -> 正斜杠
     （浏览器 MEMFS 只认 '/'；Win32 同样接受 '/'，所以这三处没加 #ifdef）

注意：RlBridge.cpp（Windows 裸 Winsock）不是"打补丁"而是"换实现" ——
      Web 版编 src/RL/RlBridge_web.cpp，桌面版编 src/RL/RlBridge.cpp，见 CMakeLists。
      UIFont.cpp 同理，直接是重写过的版本，不在这里改。
"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src')


class Patch:
    """一处精确替换：old 必须命中恰好 count 次，否则整体失败。"""

    def __init__(self, path, old, new, note, count=1):
        self.path = os.path.join(SRC, path)
        self.rel = path
        self.old = old.replace('\n', '\r\n')
        self.new = new.replace('\n', '\r\n')
        self.note = note
        self.count = count
        # 源文件末尾可能没有换行符（Enemy.cpp 就是），这里留一个去掉尾换行的兼容锚点
        self.old_eof = self.old[:-2] if self.old.endswith('\r\n') else None
        self.new_eof = self.new[:-2] if self.new.endswith('\r\n') else None


PATCHES = [

    # ------------------------------------------------------------------ Enemy.h
    Patch('Enemy.h',
          """#include <atomic>
#include <condition_variable>""",
          """#include <atomic>
#include <chrono>
#include <condition_variable>""",
          'Enemy.h: 补 <chrono>（Web 版要用 steady_clock 记帧间时间）'),

    Patch('Enemy.h',
          """    // 等待后台处理玩家攻击请求
    vector<OrbFireRequest> pendingFireRequests_;""",
          """#ifdef MAZE_WEB
    // Web 版没有后台线程：WorkerLoop() 由主线程每帧同步调一次。
    // 上一拍的时间必须跨调用记住，否则每帧算出来的 dt 都接近 0，敌人根本不动。
    chrono::steady_clock::time_point lastTick_{};
#endif

    // 等待后台处理玩家攻击请求
    vector<OrbFireRequest> pendingFireRequests_;""",
          'Enemy.h: 加 Web 版专用的 lastTick_ 成员'),

    # ---------------------------------------------------------------- Enemy.cpp
    Patch('Enemy.cpp',
          """    auto nextTick = Clock::now() + tickDuration;
    auto lastTick = Clock::now();""",
          """    auto nextTick = Clock::now() + tickDuration;
#ifdef MAZE_WEB
    // Web：dt 从"上一次被调用"算起（lastTick_ 是成员，跨调用保留）
    auto& lastTick = lastTick_;
#else
    auto lastTick = Clock::now();
#endif""",
          'Enemy.cpp: dt 基准改成 Web 可跨调用保留'),

    Patch('Enemy.cpp',
          """            unique_lock<mutex> lock(inputMutex_);
            while (running_.load() && Clock::now() < nextTick)
            {
                condition_.wait_until(
                    lock,
                    nextTick,
                    [this]() { return !running_.load(); }
                );

                if (!running_.load())
                {
                    return;
                }

                if (playerSnapshotUpdated_)
                {
                    player = playerSnapshot_;
                    playerSnapshotUpdated_ = false;
                }
            }
""",
          """            unique_lock<mutex> lock(inputMutex_);
#ifndef MAZE_WEB
            while (running_.load() && Clock::now() < nextTick)
            {
                condition_.wait_until(
                    lock,
                    nextTick,
                    [this]() { return !running_.load(); }
                );

                if (!running_.load())
                {
                    return;
                }

                if (playerSnapshotUpdated_)
                {
                    player = playerSnapshot_;
                    playerSnapshotUpdated_ = false;
                }
            }
#else
            // Web：不等待。等一下就是白卡主线程 16ms，而调用方本来就是按帧来的。
#endif
""",
          'Enemy.cpp: Web 版不等待'),

    Patch('Enemy.cpp',
          """        nextTick += tickDuration;
        if (nextTick < Clock::now())
        {
            nextTick = Clock::now() + tickDuration;
        }
    }
}""",
          """        nextTick += tickDuration;
        if (nextTick < Clock::now())
        {
            nextTick = Clock::now() + tickDuration;
        }

#ifdef MAZE_WEB
        break;      // Web：主线程每帧只调一次，跑完这一拍就交还控制权
#endif
    }
}""",
          'Enemy.cpp: Web 版跑一拍就退出循环'),

    Patch('Enemy.cpp',
          """    if (running_.exchange(true))
    {
        return;
    }

    worker_ = thread(&EnemyManager::WorkerLoop, this);
}""",
          """    if (running_.exchange(true))
    {
        return;
    }

#ifdef MAZE_WEB
    // Web：不创建后台线程，WorkerLoop() 交给 PublishPlayerSnapshot() 每帧同步调用
    lastTick_ = chrono::steady_clock::now();
#else
    worker_ = thread(&EnemyManager::WorkerLoop, this);
#endif
}""",
          'EnemyManager::Start() Web 版不起线程'),

    Patch('Enemy.cpp',
          """        playerSnapshot_ = player;
        playerSnapshotUpdated_ = true;
    }

    condition_.notify_one();
}""",
          """        playerSnapshot_ = player;
        playerSnapshotUpdated_ = true;
    }

#ifdef MAZE_WEB
    // Web：没有后台线程可唤醒，主线程顺手把这 16ms 的一拍跑掉。
    // 跑完 WorkerLoop 里的 dt 仍然按真实经过时间算，所以速度手感和桌面版一致。
    if (running_.load())
    {
        WorkerLoop();
    }
#else
    condition_.notify_one();
#endif
}""",
          'EnemyManager::PublishPlayerSnapshot() Web 版同步跑一拍'),

    # ------------------------------------------------------------- RlTelemetry
    Patch('RL/RlTelemetry.cpp',
          """#include <cstdio>
#include <direct.h>
""",
          """#include <cstdio>
#ifdef MAZE_WEB
#include <sys/stat.h>   // ::mkdir
#else
#include <direct.h>     // _mkdir：Windows 专有
#endif
""",
          'RlTelemetry: 目录相关头文件按平台二选一'),

    Patch('RL/RlTelemetry.cpp',
          """    namespace terminal
    {
        namespace
        {""",
          """    namespace terminal
    {
        namespace
        {
            // 跨平台建目录：Windows 是 _mkdir（<direct.h>），
            // POSIX / Emscripten 是 mkdir（<sys/stat.h>）
            void MkDir(const char* dir)
            {
                if (dir == nullptr || *dir == 0) return;
#ifdef MAZE_WEB
                ::mkdir(dir, 0777);
#else
                _mkdir(dir);
#endif
            }
""",
          'RlTelemetry: 加跨平台 MkDir 帮助函数'),

    Patch('RL/RlTelemetry.cpp',
          """            if (!gRouteDumpDir.empty()) _mkdir(gRouteDumpDir.c_str());""",
          """            if (!gRouteDumpDir.empty()) MkDir(gRouteDumpDir.c_str());""",
          'RlTelemetry: _mkdir -> MkDir'),

    Patch('RL/RlTelemetry.cpp',
          """            _mkdir(gRouteDumpDir.c_str());""",
          """            MkDir(gRouteDumpDir.c_str());""",
          'RlTelemetry: _mkdir -> MkDir（第二处）'),

    Patch('RL/RlTelemetry.cpp',
          """            const std::string path = gRouteDumpDir + "\\\\route_last.txt";
            FILE* f = nullptr;
            if (fopen_s(&f, path.c_str(), "w") == 0 && f != nullptr)""",
          """            const std::string path = gRouteDumpDir + "/route_last.txt";   // 正斜杠 Windows 也认
            FILE* f = std::fopen(path.c_str(), "w");                      // fopen_s 非 MSVC 没有
            if (f != nullptr)""",
          'RlTelemetry: fopen_s -> fopen，路径分隔符改正斜杠'),

    # -------------------------------------------------------------------- RLNet
    Patch('RL/RLNet.cpp',
          """        if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr)
            return fail("cannot open weight file");""",
          """#ifdef MAZE_WEB
        f = std::fopen(path.c_str(), "rb");
#else
        if (fopen_s(&f, path.c_str(), "rb") != 0) f = nullptr;
#endif
        if (f == nullptr)
            return fail("cannot open weight file");""",
          'RLNet: fopen_s -> fopen'),

    # ------------------------------------------------------------------ Account
    Patch('Account.cpp',
          """    std::string BuildLogPath()
    {
        char* exePath = nullptr;""",
          """    std::string BuildLogPath()
    {
#ifdef MAZE_WEB
        // Web：/maze 是挂到 IndexedDB 上的持久化目录（见 WebPlatform.cpp）。
        // 写在这儿的数据刷新页面还在；没挂载成功时它退化成内存 FS，刷新即丢。
        return std::string("/maze/log.csv");
#else
        char* exePath = nullptr;""",
          'Account: Web 版 log.csv 路径'),

    Patch('Account.cpp',
          """        return dir + "\\\\log.csv";
    }""",
          """        return dir + "\\\\log.csv";
#endif
    }""",
          'Account: 关掉 Web 分支'),

    Patch('Account.cpp',
          """    localtime_s(&local, &now);""",
          """#ifdef MAZE_WEB
    localtime_r(&now, &local);      // MSVC 的 *_s 版本 Emscripten 里没有
#else
    localtime_s(&local, &now);
#endif""",
          'Account: localtime_s -> localtime_r'),

    # ---------------------------------------------------------------- GameTimer
    Patch('GameTimer.cpp',
          """    strncpy_s(prefix, sizeof(prefix), u.c_str(), _TRUNCATE);""",
          """    snprintf(prefix, sizeof(prefix), "%s", u.c_str());   // strncpy_s/_TRUNCATE 非 MSVC 没有""",
          'GameTimer: strncpy_s -> snprintf'),

    # ------------------------------------------------------------------ main.cpp
    Patch('main.cpp',
          """static std::string RlAssetDir()
{
    char* exePath = nullptr;""",
          """static std::string RlAssetDir()
{
#ifdef MAZE_WEB
    // Web：权重由 CMake --preload-file 打进虚拟 FS，路径写死即可。
    // 缺文件时 RlAgent 会自己退化成「朝出口贪心走」，所以这里不必报错。
    return std::string("/assets/rl");
#else
    char* exePath = nullptr;""",
          'main.cpp: Web 版权重目录'),

    Patch('main.cpp',
          """    return dir + "\\\\rl_assets";
}""",
          """    return dir + "\\\\rl_assets";
#endif
}""",
          'main.cpp: 关掉 RlAssetDir 的 Web 分支'),

    # --------------------------------------------------------------- main.cpp
    Patch('main.cpp',
          '''#include "Nailong.h"
#include "TPCamera.h"
''',
          '''#include "Nailong.h"
#include "TPCamera.h"
// WebPlatform 桌面版也有一份空实现（比如 SyncCanvasToWindow），
// 所以无条件包含 —— 主循环里那句调用就不用再套一层 #ifdef。
#include "WebPlatform.h"
#include "Leaderboard.h"    // 全网榜（桌面版是空实现，调它不花钱）
''',
          'main.cpp: 引入 WebPlatform / Leaderboard'),

    # --------------------------------------------- 全网榜：Account 侧的三个钩子
    Patch('Account.cpp',
          """#include "Account.h"
#include "UIFont.h"
""",
          """#include "Account.h"
#include "UIFont.h"
#ifdef MAZE_WEB
#include "Leaderboard.h"    // 全网榜：通关后把成绩传给服务端
#include "WebPlatform.h"    // IndexedDB 持久化
#endif
""",
          'Account: 引入全网榜 + IndexedDB'),

    Patch('Account.cpp',
          """    WriteAll(kept);
    return true;
}""",
          """    WriteAll(kept);

#ifdef MAZE_WEB
    // Web：本地 log.csv 写进 IndexedDB 目录后要先刷盘（否则刷新页面就没了）；
    // 再顺手把这一条传给全网榜。两个榜（分数 / 用时）都要传。
    web::FlushFileSystem();
    {
        ScoreRecord scoreBoard = rec;
        scoreBoard.board = RankBoard::Score;
        lb::Submit(scoreBoard);

        ScoreRecord timeBoard = rec;
        timeBoard.board = RankBoard::Time;
        lb::Submit(timeBoard);
    }
#endif

    return true;
}""",
          'Account: 通关后刷盘 + 上传全网榜'),

    Patch('Account.cpp',
          """    std::vector<ScoreRecord> out;
    bool (*better)(const ScoreRecord&, const ScoreRecord&) =
        (board == RankBoard::Time) ? BetterByTime : BetterByScore;""",
          """    std::vector<ScoreRecord> out;

#ifdef MAZE_WEB
    // Web：本地 log.csv 只在这台机器上，真正的「全网」在服务端。
    //   · RequestBoard() 是异步的 —— 这一帧拿不到，但会缓存下来，
    //     所以"第一次进这一页看本地、第二次进就看全网"是预期行为（不卡主线程）。
    //   · TakeCached() 把已经缓存到的云端成绩并进来，跟着本地榜一起排序。
    //     同名同模式的两条会由下面的 InsertBest 去重，留更好的那条。
    lb::RequestBoard(d, board, maxCount > 0 ? maxCount : RANK_LIST_MAX);
    lb::TakeCached(d, board, out);
#endif

    bool (*better)(const ScoreRecord&, const ScoreRecord&) =
        (board == RankBoard::Time) ? BetterByTime : BetterByScore;""",
          'Account: 排名页并进云端成绩'),

    # --------------------------------------------------------------- main.cpp
    Patch('main.cpp',
          """    char path[64];
    snprintf(path, sizeof(path), "saves/slot%d.json", slot);""",
          """    char path[64];
#ifdef MAZE_WEB
    // Web：存档必须写进 IndexedDB 挂载点（/maze），写相对路径刷新页面就没了。
    // 目录由 WebPlatform::InitFileSystem() 建好。
    snprintf(path, sizeof(path), "/maze/saves/slot%d.json", slot);
#else
    snprintf(path, sizeof(path), "saves/slot%d.json", slot);
#endif""",
          'main.cpp: 存档路径改到 /maze/saves（IndexedDB 持久化）', count=2),

    Patch('main.cpp',
          """static std::string RlRouteDir()
{
    std::string dir = RlAssetDir();""",
          """static std::string RlRouteDir()
{
#ifdef MAZE_WEB
    // Web：路线导出落在 IndexedDB 目录里（纯调试产物，别往相对路径写）
    return std::string(\"/maze/rl_log\");
#else
    std::string dir = RlAssetDir();""",
          'main.cpp: 路线导出目录改到 /maze/rl_log'),

    Patch('main.cpp',
          """    return dir + \"\\\\rl\\\\out\";
}""",
          """    return dir + \"\\\\rl\\\\out\";
#endif
}""",
          'main.cpp: 关上 RlRouteDir 的 #ifdef'),

    # --------------------------------------- 鼠标锁定状态改用平台给的真实状态
    Patch('main.cpp',
          """                cursorLocked = IsCursorHidden();          // 跟随 raylib 实际状态（暂停/结算时鼠标是解锁的）""",
          """                // 不能用 IsCursorHidden()：Web 后端的 DisableCursor() 只请求 pointer lock，
                // 从不动 cursorHidden，浏览器里它恒为 false —— 视角就永远拖不动。
                cursorLocked = web::CursorLocked();       // 跟随真实锁定状态（暂停/结算时鼠标是解锁的）""",
          'main.cpp: 鼠标锁定状态改用 WebPlatform::CursorLocked()'),
    # ------------------------------------------- SfxAudio / FootstepAudio 路径
    # AUDIO-SEP：音频查找函数拼的是 Windows 反斜杠（"assets\\audio\\" + 文件名），
    # 浏览器 MEMFS 只把 '/' 当分隔符，所以 Web 版音效一直哑巴（footstep 更是整段禁用）。
    # 正斜杠在 Win32 文件 API 上同样合法、桌面版语义零变化，故这三处没加 #ifdef
    # （见文件头「已处理的平台差异」第 7 条）。
    Patch('SfxAudio.cpp',
          'std::string("assets\\\\audio\\\\") + fileName;',
          'std::string("assets/audio/") + fileName;',
          'SfxAudio.cpp: 音频相对目录 \\ -> /'),
    Patch('SfxAudio.cpp',
          'std::string("codes\\\\assets\\\\audio\\\\") + fileName;',
          'std::string("codes/assets/audio/") + fileName;',
          'SfxAudio.cpp: 工程相对目录 \\ -> /'),
    Patch('SfxAudio.cpp',
          'return directory + "\\\\" + child;',
          'return directory + "/" + child;',
          'SfxAudio.cpp: JoinPath 分隔符 \\ -> /'),
    Patch('FootstepAudio.cpp',
          'std::string("assets\\\\audio\\\\") + fileName;',
          'std::string("assets/audio/") + fileName;',
          'FootstepAudio.cpp: 音频相对目录 \\ -> /'),
    Patch('FootstepAudio.cpp',
          'std::string("codes\\\\assets\\\\audio\\\\") + fileName;',
          'std::string("codes/assets/audio/") + fileName;',
          'FootstepAudio.cpp: 工程相对目录 \\ -> /'),
    Patch('FootstepAudio.cpp',
          'return directory + "\\\\" + child;',
          'return directory + "/" + child;',
          'FootstepAudio.cpp: JoinPath 分隔符 \\ -> /'),
    # ------------------------------------------------------------ LightSource
    # LIGHTSRC：Web 后端是 GRAPHICS_API_OPENGL_ES2，shader 按 GLSL ES 1.00 编译，而 LightSource
    # 里那套 #version 330 的 in/out/texture() 在浏览器里编不过（报
    # "'in' : storage qualifier supported in GLSL ES 3.00 and above only"），
    # IsReady() 因此恒为 false，表现就是「地狄难度没有视野限制」。
    # 这里给 Web 一套等价写法：attribute / varying / texture2D / gl_FragColor，
    # 且不写 #version（GLSL ES 1.00 本来就是默认值 100）。
    # uniform 名字一个都不能改，GetShaderLocation 是按名字取的。
    Patch('LightSource.cpp',
          '''static const char* FOG_VS = R"(#version 330''',
          '''// ---- Web 变体：GLSL ES 1.00（raylib Web 后端按 ES 1.00 编译 shader）----
#ifdef MAZE_WEB
static const char* FOG_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec4 vertexColor;

uniform mat4 mvp;

varying highp vec3 fragWorldPos;
varying mediump vec2 fragTexCoord;
varying mediump vec4 fragColor;

void main()
{
    fragWorldPos = vertexPosition;
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* FOG_FS = R"(
precision highp float;

varying highp vec3 fragWorldPos;
varying mediump vec2 fragTexCoord;
varying mediump vec4 fragColor;

uniform sampler2D texture0;
uniform vec3  lightPos;
uniform float lightRadius;
uniform float lightInner;
uniform float lightDarkness;

void main()
{
    vec4 texel = texture2D(texture0, fragTexCoord) * fragColor;
    float d = distance(fragWorldPos, lightPos);
    float t = smoothstep(lightRadius * lightInner, lightRadius, d);
    texel.rgb = mix(texel.rgb, texel.rgb * 0.03, clamp(t * lightDarkness, 0.0, 1.0));
    gl_FragColor = texel;
}
)";

static const char* BLUR_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec4 vertexColor;

uniform mat4 mvp;

varying mediump vec2 fragTexCoord;
varying mediump vec4 fragColor;

void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* BLUR_FS = R"(
precision highp float;

varying mediump vec2 fragTexCoord;
varying mediump vec4 fragColor;

uniform sampler2D texture0;
uniform vec2  texSize;
uniform float blurAmount;
uniform float edgeDarkness;
uniform float lightInner;

void main()
{
    vec2 uv = fragTexCoord;
    vec2 aspect = vec2(texSize.x / max(texSize.y, 1.0), 1.0);
    vec2 p = (uv - vec2(0.5)) * aspect;
    float r = clamp(length(p) / 0.5, 0.0, 1.0);
    float t = smoothstep(lightInner, 1.0, r);

    vec4 col = texture2D(texture0, uv);

    if (t > 0.004 && blurAmount > 0.004)
    {
        vec2 dir = uv - vec2(0.5);
        if (dot(dir, dir) < 1e-8) dir = vec2(0.0, 1.0);
        dir = normalize(dir);

        vec2 offset = dir * (t * blurAmount * 0.045);
        vec4 sum = vec4(0.0);
        for (int i = 0; i < 9; ++i)
        {
            float k = (float(i) / 8.0 - 0.5) * 2.0;
            sum += texture2D(texture0, uv + offset * k);
        }
        col = mix(col, sum / 9.0, clamp(t * 1.3, 0.0, 1.0));
    }

    col.rgb *= mix(1.0, 1.0 - edgeDarkness, t);
    gl_FragColor = vec4(col.rgb, 1.0);
}
)";
#else
// ---- 桌面版：GLSL 330（OpenGL 3.3）----
static const char* FOG_VS = R"(#version 330''',
          'LightSource.cpp: 给 Web 补 GLSL ES 1.00 版 shader'),
    Patch('LightSource.cpp',
          '''// 生命周期''',
          '''#endif  // MAZE_WEB（GLSL 100 / 330 两套 shader）

// 生命周期''',
          'LightSource.cpp: 合上 #endif'),
]


def apply(check_only=False):
    done = 0
    for p in PATCHES:
        if not os.path.isfile(p.path):
            raise SystemExit('[MISS FILE] %s' % p.rel)

        with io.open(p.path, encoding='utf-8', newline='') as f:
            text = f.read()

        # 幂等：新文本已经在了 = 这个补丁打过了，直接跳过（脚本可重复跑）
        already = (text.count(p.new) == p.count) or (
            p.new_eof is not None and text.count(p.new_eof) == p.count)
        if already:
            print('[%s] %s%s' % ('ok ' if check_only else 'skip', p.note,
                                 ' (already applied)' if not check_only else ''))
            done += 1
            continue

        eof_variant = False
        n = text.count(p.old)
        if n == 0 and p.old_eof is not None and text.count(p.old_eof) == p.count:
            n = p.count
            eof_variant = True

        if n != p.count:
            raise SystemExit(
                '[ANCHOR FAIL] %s : 期望 %d 处，实际 %d 处\n  --- 锚点 ---\n%s'
                % (p.rel, p.count, text.count(p.old), p.old[:400]))

        if check_only:
            print('[ok ] %s' % p.note)
        else:
            if eof_variant:
                text = text.replace(p.old_eof, p.new_eof, p.count)
            else:
                text = text.replace(p.old, p.new, p.count)
            with io.open(p.path, 'w', encoding='utf-8', newline='') as f:
                f.write(text)
            print('[fix] %s' % p.note)
        done += 1

    print('---')
    print('%d 个补丁%s' % (done, '锚点全部命中' if check_only else '已应用'))


if __name__ == '__main__':
    apply('--check' in sys.argv)