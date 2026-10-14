# 3D 迷宫 · Web 版

> 把桌面版（C++17 + raylib）的 15k 行迷宫游戏搬进浏览器：**Emscripten → WebAssembly**，
> 砍掉裸 TCP 的训练桥，加上一套**服务端排行榜**（带合理性校验，不是客户端自说自话）。

桌面版仓库：[maze_game](https://gitee.com/fidel-castro/maze_game)（`final-version` 分支）
本仓库是从它派生的**独立 Web 版**，桌面版分支不受影响。

---

## 目录

| 章节 | 内容 |
|---|---|
| [一、跑起来](#一跑起来) | 三条命令从零到能玩 |
| [二、目录结构](#二目录结构) | 每个目录是干什么的 |
| [三、移植说明](#三移植说明) | 桌面版 → Web 到底改了什么，为什么这么改 |
| [四、全网排行榜](#四全网排行榜) | 后端架构、接口、防作弊与它的边界 |
| [五、部署](#五部署) | Cloudflare Pages + Workers 一条龙 |
| [六、常见问题](#六常见问题) | 踩过的坑都在这里 |

---

## 一、跑起来

### 0. 环境

| 依赖 | 说明 |
|---|---|
| **emsdk** | Emscripten 工具链，装到 `D:\emsdk`（脚本默认路径，可用参数改） |
| **CMake + Ninja** | 用来驱动构建（raylib 官方就这么走） |
| **Python 3** | 只用来跑 `tools/` 下的几个脚本（转码、字体子集、本地服务器） |

**装 emsdk —— 两条路，`build_web.ps1` 都认。** 国内直连 `storage.googleapis.com`
很容易掉到 17 KB/s，所以走 B 更省事（本机就是这么装的）：

**A) 官方流程**

```powershell
git clone https://github.com/emscripten-core/emsdk.git D:\emsdk
cd D:\emsdk
.\emsdk install latest        # 这里会下 668MB 的 wasm-binaries.zip
.\emsdk activate latest
```

**B) 手动解包（绕开 emsdk 那个慢下载器）**

```powershell
# 668MB，建议多连接/断点续传下载
curl.exe -L -o D:\emsdk\downloads\wasm-binaries.zip `
  https://storage.googleapis.com/webassembly/emscripten-releases-builds/win/<releases-tag>/wasm-binaries.zip
# 解到 upstream\（里面是 emscripten\ bin\ lib\ share\）
python -c "import zipfile;zipfile.ZipFile(r'D:\emsdk\downloads\wasm-binaries.zip').extractall(r'D:\emsdk\upstream')"
```

`<releases-tag>` 在 `emsdk_manifest.json` / `emscripten-releases-tags.json` 里查（例如 `6.0.12`
对应 `6e9f6885cb17a3d6c8fd488a5cff730f50aba61d`）。
脚本会自动补一份 `.emscripten` 配置（`LLVM_ROOT` / `BINARYEN_ROOT` / `NODE_JS`），
只要求本机有 **Python 3 + node.exe**。

> 工具链装好后可以只编探针先试水：
> ```powershell
> powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -ProbeOnly
> ```
> 探针单独验三条最容易翻车的链路（emcc / 中文字体 / 预加载资源）。

### 1. 构建

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_web.ps1
```

产物在 `build/web/index.html`（外加 `index.js` / `index.wasm` / `index.data`）。

### 2. 本地预览

```powershell
python tools\serve.py --open
```

**别双击 `index.html`** —— `file://` 下浏览器会以 CORS 为由拒绝加载 `.wasm` / `.data`，游戏起不来。

### 3. 操作

和桌面版完全一致：`WASD` 移动、`Shift` 奔跑、`Space` 跳跃、鼠标转视角、左键射击、
`V` 切视角（奶龙模式）、`Tab` 锁鼠标、`ESC` 暂停。

### 4. 自检（不启动游戏就能验的两件事）

**a) 排行榜签名算法** —— 客户端那份 SHA-256/HMAC 是手写的。算错一个字节，成绩会被
服务端 403 拒收，而现象是"本地 `log.csv` 好好的、就是不上榜"，极难查。所以留了靶子：

```powershell
cmake --build build\desktop --config Release --target selftest
build\desktop\Release\selftest.exe > build\selftest_out.txt
python tools\selftest\check.py build\selftest_out.txt
```

**b) 后端验签链路** —— 起一个临时 sqlite 后端，拿 C++ 算出来的**真签名**去打它：

```powershell
python tools\selftest\server_check.py
```

依次验：真签名被接受 / 改分数被拒(403) / 乱签名被拒 / 用时越界被拒(422) / 榜单查得到 /
重复提交不刷榜。改服务端规则时跑一遍，能立刻发现前后端不一致。

---

## 二、目录结构

```
maze_web/
├── CMakeLists.txt          Web / 桌面两套构建（桌面版拿来对照调试用）
├── src/                    游戏源码（已全部转成 UTF-8）
│   ├── main.cpp            主程序：状态机 + 帧循环（约 5000 行，未做结构改动）
│   ├── Config.h            全部常量 / 枚举 / 难度表 / UI 字形表
│   ├── ...                 和桌面版同名同结构，见下方"移植说明"
│   ├── Sha256.h/.cpp       ★新增：自带的 SHA-256 + HMAC-SHA256（排行榜签名）
│   ├── Leaderboard.h/.cpp  ★新增：全网榜客户端（提交 / 拉取 / 缓存 / 合并）
│   ├── WebPlatform.h/.cpp  ★新增：IndexedDB 持久化（IDBFS 挂载）
│   └── RL/
│       ├── RlBridge.cpp        桌面版：Windows 裸 Winsock 的 TCP 训练桥
│       ├── RlBridge_web.cpp    ★新增：同接口的桩，Web 版编这个
│       └── ...                 RL 离线推理（纯 C++，原样保留）
├── probe/probe.cpp         可移植性探针
├── web/
│   ├── shell.html          Emscripten 页面外壳（加载进度条 + 兜底提示）
│   └── config.js           运行时配置（后端地址、是否联网）
├── assets/
│   ├── models/             nailong.glb + 贴图
│   ├── audio/              BGM 与音效
│   ├── fonts/              NotoSansSC-Subset.ttf（216 KB，由 tools/fonts 的源字体裁出）
│   └── rl/                 policy_*.bin 权重（客户端离线推理用）
├── third_party/raylib/     raylib 6.0 源码（内置；必须和桌面版同一版 API，见下）
├── server/                 排行榜后端
│   ├── worker.js           Cloudflare Worker 实现（主）
│   ├── local_server.py     纯标准库实现（本地联调 / 自建）
│   ├── schema.sql          D1 / SQLite 建表
│   └── wrangler.toml       部署配置
└── tools/
    ├── gbk2utf8.py         桌面版 GBK 源码 → UTF-8（迁移第一步）
    ├── apply_web_port.py   25 处平台补丁（带锚点断言，可重复跑）
    ├── make_font_subset.py 按 UI_GLYPHS 裁字体子集
    ├── fonts/              NotoSansSC-VF.ttf（17MB 源字体；故意不放 assets/，
    │                       否则 --preload-file assets 会把它整个打进 .data）
    ├── selftest/           可执行自检：SHA-256/HMAC 对照 Python + 后端验签联调
    ├── serve.py            本地静态服务器
    └── build_web.ps1       一键构建
```

---

## 三、移植说明

### 3.1 先分类：哪些能直接搬，哪些搬不了

| 类别 | 模块 | 结论 |
|---|---|---|
| **纯逻辑，原样搬** | Maze / Player / Treasure / LightSource / Nailong / NailongRig / TPCamera / Menu / GameTimer / Save / Entity / Enemy | 一行没改（除 Enemy 的线程，见下） |
| **要换实现** | `UIFont`（系统字体 + GBK 转换）、`RlBridge`（裸 Winsock） | 见 3.3 / 3.4 |
| **要跨平台改** | `Account` / `GameTimer` / `RlTelemetry` / `RLNet` / `main.cpp` | 见 3.5，全是 MSVC 专有函数 |

### 3.2 后台线程 → 主线程同步一拍

`EnemyManager` 原本开一个后台线程，每 16ms 跑一拍敌人 AI（`Enemy.cpp:1204`）。

浏览器里当然也能用 pthreads，但代价是**必须**给页面加 `Cross-Origin-Opener-Policy` +
`Cross-Origin-Embedder-Policy` 两个响应头（否则 `SharedArrayBuffer` 不可用），
而 GitHub Pages 这类静态托管根本发不了自定义头 —— 等于把部署面砍掉一半。

**改成单线程更划算**：敌人逻辑本来就是"每 16ms 一拍"，主线程按帧调一次完全等价，
而 `dt` 依旧按真实经过时间算，速度手感和桌面版一模一样。

改动只有 5 处，全在 `#ifdef MAZE_WEB` 里：

```cpp
// Start()：不起线程，只记一下时间基准
// WorkerLoop()：不等待、跑完一拍就 break
// PublishPlayerSnapshot()：主线程顺手把这一拍跑掉
// dt 的基准改成成员 lastTick_，跨调用保留
```

于是 `main.cpp` 里 `Start()` / `PublishPlayerSnapshot()` / `GetRenderStates()` 的调用
**一行都不用动**。

### 3.3 字体：浏览器里没有"系统字体"这回事

桌面版的 `UIFont::Load()` 是从 `C:/Windows/Fonts/` 里挨个探测等线 / 黑体 / 雅黑 / 宋体。
Web 上没有这个目录，**必须自带字体**，否则界面上的中文全变 `?`。

所以：

1. 内置 **Noto Sans SC**（OFL 授权，可自由分发）；
2. `tools/make_font_subset.py` 按 `Config.h` 里的 `UI_GLYPHS` 裁子集 ——
   完整字体 17.3 MB，实际用到的只有 **621 个字符**，裁完 **216 KB（1.2%）**；
3. 顺手丢掉 `fvar` / `gvar` / `avar` 这些可变字体表 ——
   raylib 底层的 stb_truetype 不认可变轴，不删会按默认轴渲染出歪掉的字。

另外，**源码编码也变了**：桌面版源码是 GBK（VS 中文版默认），`Utf8()` 靠
`MultiByteToWideChar` 转 UTF-8；Web 版源码已经全部转成 UTF-8，`Utf8()` 直接原样返回。
调用方写的是同一句 `Utf8("开始游戏")`，差异全被 `UIFont.cpp` 吃掉了。

### 3.4 RlBridge：砍掉，但用"桩"而不是删调用点

`RlBridge.cpp` 是 Windows 裸 Winsock 写的 TCP 服务端（`WSAStartup` / `socket` / `accept`）。
浏览器既没有裸 socket、也不允许页面自己当 server —— **这块功能在 Web 上没法存在**。

但 `RlBridge.h` 本身是纯 std 的（只有 `atomic` / `mutex` / `condition_variable`），
对平台零依赖。所以做法是：

- 头文件照用，`main.cpp` 一行不改；
- Web 版编 `src/RL/RlBridge_web.cpp`（同接口的桩，`Start()` 永远返回 `false`）；
- 桌面版编原来的 `RlBridge.cpp`。

在 CMakeLists 里二选一：

```cmake
if(EMSCRIPTEN)
    target_sources(maze PRIVATE src/RL/RlBridge_web.cpp)
else()
    target_sources(maze PRIVATE src/RL/RlBridge.cpp)
endif()
```

菜单里点「RL 训练桥」会看到一句友好提示，**其余功能不受影响** ——
包括「RL 自动通关」（纯 C++ 前向推理 + 贪心退化），网页版照常能看机器人代打。

### 3.5 MSVC 专有函数：跨平台改法

| 原写法 | 问题 | 改法 |
|---|---|---|
| `_get_pgmptr()` | MSVC 专有，用来从 exe 路径反推仓库根目录 | Web 版直接写死虚拟 FS 路径 |
| `localtime_s()` | MSVC 安全版 | Web 用 `localtime_r()` |
| `strncpy_s(..., _TRUNCATE)` | 同上 | 统一换 `snprintf` |
| `fopen_s(&f, ...)` | 同上 | 统一换 `fopen` |
| `_mkdir()` / `<direct.h>` | Windows 专有 | Web 用 `::mkdir` / `<sys/stat.h>` |
| `ofstream` 写 `log.csv` | Web 上写的是**内存 FS**，刷新就没了 | 挂 IDBFS 到 `/maze`，落盘走 IndexedDB |

全部改动集中在 `tools/apply_web_port.py` 里，共 **22 个补丁**，
每条都带锚点断言 —— 对不上就直接报错退出，绝不会"改一半"。
这个脚本留在仓库里，本身就是一份可复现的移植记录；它还是**幂等**的，可以放心重复跑。

### 3.6 存档 / log.csv 的持久化

Web 上 `std::ofstream` 写的是 MEMFS（内存文件系统），**页面一刷新全没了**。
Emscripten 的 IDBFS 能把 IndexedDB 挂成一个目录，于是：

```cpp
web::InitFileSystem();     // 启动时：挂载 /maze 到 IndexedDB，异步读回上次的数据
web::FlushFileSystem();    // 写完 log.csv / 存档后：同步回 IndexedDB
```

`Account::BuildLogPath()` 在 Web 下返回 `/maze/log.csv`，桌面下还是原来的
`<仓库根>/log.csv`。

> 代价要说清楚：IndexedDB 是**按浏览器 + 按设备**隔离的。换台电脑、换个浏览器、
> 清一下站点数据，本地记录就没了 —— 这正是「全网榜」存在的意义。

---

## 四、全网排行榜

### 4.1 架构

```
游戏（wasm）
  │  通关 → Account::SubmitRecord()
  │           ├─ 本地 log.csv（/maze，IndexedDB）
  │           └─ lb::Submit()  → HMAC-SHA256 签名 → POST /api/score
  │
  │  打开「记录排名」→ lb::RequestBoard() → GET /api/board
  ▼
Cloudflare Worker（worker.js）
  ├─ 验签（Web Crypto）
  ├─ 范围 / 合理性校验
  ├─ 限流（按 IP 哈希）
  └─ UPSERT 到 D1（(用户名, 难度, 榜单) 唯一，只留更好的那次）
```

### 4.2 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| `GET` | `/api/health` | 健康检查 |
| `GET` | `/api/board?difficulty=easy&board=score&limit=10` | 取榜单 |
| `POST` | `/api/score` | 提交成绩 |

提交体：

```json
{
  "name": "甲基橙", "difficulty": "hard", "board": "score", "mode": "normal",
  "score": 120, "elapsed": 45.123,
  "sig": "hmac-sha256 的十六进制"
}
```

签名原文（**字段顺序必须完全一致**，客户端和服务端各拼一遍）：

```
name|difficulty|board|mode|score|elapsed
```

### 4.3 防作弊：方案 2，以及它的边界

| 层 | 做了什么 |
|---|---|
| **验签** | 客户端用编译进 wasm 的密钥算 HMAC-SHA256，服务端用 Web Crypto 复核，定长比较防时序侧信道 |
| **范围** | 分数上限 / 用时下限按难度分别设（`LIMITS` 表）；用时不能超过 6 小时；用户名按码点数限 12 |
| **限流** | 同一 IP 每小时最多 30 次提交；IP 只存 SHA-256 前 32 位 |
| **去重** | 60 秒内完全相同的成绩只算一次（防手抖双击 / 网络重试） |
| **存疑标记** | 比当前榜首高出一个量级的成绩**不拒**，但打上 `flagged`，方便你回头人工看 |

> ⚠ **必须说清楚**：密钥编译在 wasm 里，肯下功夫的人能挖出来。
> 这套东西把作弊门槛从「打开 DevTools 改个数字」抬高到「先逆向前端再写脚本」，
> 但**不是**绝对安全。
>
> 真要杜绝作弊，唯一办法是**服务端重放验证**（提交 seed + 操作序列，服务器重跑一遍迷宫），
> 那意味着服务端要再实现一份权威模拟，工作量约等于再写一遍游戏逻辑。
> 对这个项目来说投入产出比不划算，所以停在这里 —— 但你知道边界在哪。

### 4.4 客户端行为

- **只有通关才上传**。死亡 / 中途退出和桌面版一样不记。
- **两个榜都传**：分数榜和用时榜各一条。
- **拉取是异步的**：第一次打开「记录排名」看到的是本地记录，同时后台在拉云端；
  第二次打开（或稍后再进）云端成绩就并进来了。
  这是**故意的** —— 没必要为了"立刻显示"把主线程卡在一个同步请求上。
- **去重**：同名的云端记录和本地记录会由 `Account` 里同一套 `InsertBest` 合并，
  留更好的那条（先比分数，同分比用时）。
- **不联网也能玩**：`web/config.js` 里把 `online` 改成 `false`，游戏就完全离线，
  只写本地 `log.csv`。

---

## 五、部署

### 5.1 后端（Cloudflare Worker + D1）

```powershell
cd server
npm install -g wrangler
wrangler login
wrangler d1 create maze-leaderboard          # 把输出的 database_id 填进 wrangler.toml
wrangler d1 execute maze-leaderboard --file=schema.sql --remote
wrangler deploy
```

### 5.2 前端（Cloudflare Pages）

```
构建命令：  （留空，本地构建好上传）
输出目录：  build/web
```

构建产物里 `web/config.js` 会被打进 `build/web`，把 `apiBase` 指向 Worker 域名即可。
更省事的做法是在 Pages 里配一条路由 `/api/*` → Worker，这样 `apiBase` 留空就行（同域）。

> 顺带一提：**如果你哪天想用 pthreads**，`tools/serve.py` 已经把
> `Cross-Origin-Opener-Policy` / `Cross-Origin-Embedder-Policy` 两个头加好了；
> 线上要在 `web/_headers` 里补同样两条。

### 5.3 自建（不想用 Cloudflare）

```powershell
python server/local_server.py --port 8787
```

纯标准库（`http.server` + `sqlite3`），校验规则和 `worker.js` 一一对应。
扔到任何有 Python 3 的小机器上就能当全网榜服务器，游戏里把 `apiBase` 指过去即可。

---

## 六、常见问题

**Q：双击 `index.html` 白屏 / 报错？**
`file://` 下浏览器拒绝加载 `.wasm`，必须走 HTTP。用 `python tools\serve.py`。

**Q：中文全是 `?`？**
`assets/fonts/NotoSansSC-Subset.ttf` 没打进包，或者 `UI_GLYPHS` 里漏了字。
新增中文文案时记得同步补 `Config.h` 的 `UI_GLYPHS`，再重跑 `make_font_subset.py`。

**Q：`emcc: command not found`？**
`emsdk activate latest` 没生效。`tools/build_web.ps1` 默认找 `D:\emsdk`，
装别处就传 `-EmsdkRoot`。

**Q：第二次打开排行榜才有云端数据？**
预期行为，见 4.4。HTTP 是异步的，第一帧拿不到。

**Q：成绩提交被拒（HTTP 403 / 422）？**
403 = 验签失败，通常是 `src/Leaderboard.cpp` 的 `kSignKey` 和 `server/worker.js`
的 `SIGN_KEY` 不一致；422 = 超出合理范围，去 `LIMITS` 表里调你自己的上限。

**Q：帧率不如桌面版？**
`NailongRig` 是 **CPU 软件蒙皮**，每帧重算顶点。WASM 大概是原生 60~80% 的速度。
真嫌慢的话，把奶龙模型换成带骨骼的真 glb，让 GPU 去蒙皮 —— 那就不需要 `NailongRig` 了。

---

## 七、已知限制

- **RL 训练桥在 Web 上不存在**（浏览器没有裸 TCP）。离线推理正常。
- **本地存档 / log.csv 是按浏览器隔离的**，换设备就没了 —— 交给全网榜兜底。
- **排行榜不防决心大的作弊者**，见 4.3 的边界说明。
- **桌面版和 Web 版是两份源码**。`src/` 里的通用逻辑改动需要同步回桌面版仓库。
  真要彻底解决，得把 `Config.h` / 玩法模块抽成两边共用的子模块 —— 现在还没做到那一步。

---

## 八、致谢

- [raylib](https://github.com/raysan5/raylib) —— 图形 / 输入 / 音频底座
- [Emscripten](https://emscripten.org/) —— C++ → WebAssembly
- [Noto Sans SC](https://github.com/notofonts/noto-cjk) —— 中文字体（SIL OFL 1.1）