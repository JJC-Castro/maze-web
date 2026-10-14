# 界面模块接口说明（开始界面 / 暂停弹窗 / 计时器）

新增 3 个模块，每个模块一个 `.h` + 一个 `.cpp`（遵循工程"一个 cpp 一个功能"的约定）：

| 文件 | 作用 |
|---|---|
| `codes/UIFont.h` `.cpp` | 中文字体加载 + GBK -> UTF-8 转换 |
| `codes/Menu.h` `.cpp` | 通用菜单控件（开始界面 / 暂停弹窗 / 操作说明共用一个类） |
| `codes/GameTimer.h` `.cpp` | 游戏计时器（用时统计 + HUD 显示） |

`main.cpp` 用 `Config.h` 里的 `GameState` 串起状态机：`MENU` -> `PLAYING` <-> `PAUSED`，`HELP` 是主菜单的二级页。

## 一、UIFont —— 中文显示

```cpp
UIFont uiFont;
uiFont.Load(UI_FONT_SIZE, UI_GLYPHS);   // 依次尝试 Deng.ttf / simhei.ttf / msyh.ttc / simsun.ttc
Font font = uiFont.Get();               // 交给 Menu::SetFont / GameTimer::SetFont / DrawTextEx
uiFont.Unload();                        // 放在 CloseWindow() 之前
```

| 接口 | 说明 |
|---|---|
| `bool Load(int fontSize, const char* glyphs)` | 自动探测系统字体；找不到中文字体返回 false 并退回 raylib 默认字体（中文会显示成 `?`） |
| `bool LoadFrom(const char* path, int size, const char* glyphs)` | 指定字体文件加载 |
| `Font Get()` / `bool IsLoaded()` / `const char* GetPath()` | 取字体 / 是否加载成功 / 实际使用的字体路径 |
| `std::string Utf8(const char* gbk)` | **源码里的中文是 GBK，raylib 需要 UTF-8**；自己画中文时套一层：`DrawTextEx(font, Utf8("开始").c_str(), ...)` |

`glyphs` 传 `Config.h` 里的 `UI_GLYPHS`：里面列出界面会用到的所有汉字，**新增中文文案时要把新字补进去**，否则该字显示成 `?`（只影响中文，ASCII 自动包含）。

## 二、Menu —— 菜单控件

```cpp
Menu pauseMenu("已暂停",
               { { "继续游戏", MenuAction::RESUME },
                 { "重新开始", MenuAction::RESTART },
                 { "返回主菜单", MenuAction::BACK } },
               MenuStyle::DIALOG);          // PAGE = 整页；DIALOG = 居中弹窗（自带压暗遮罩）
pauseMenu.SetHint("ESC 直接继续");
pauseMenu.SetFont(font);

pauseMenu.Reset();                          // 打开菜单前调用，选中项回到第一项
MenuAction action = pauseMenu.Update();     // 每帧调用，返回本帧激活的动作
pauseMenu.Draw();                           // 画在 BeginDrawing() / EndDrawing() 之间
```

| 接口 | 说明 |
|---|---|
| `Menu(title, items, style)` / `void Init(...)` | title、`items[i].label` 按 GBK 传入，内部自动转 UTF-8 |
| `void SetTitle / SetSubtitle / SetHint(const std::string&)` | 副标题支持 `\n` 换行（暂停页用它显示当前用时） |
| `void SetFont(Font)` | 不设置则用 raylib 默认字体 |
| `void Reset()` | 选中项复位（每次打开菜单时调用） |
| `MenuAction Update()` | 键盘：`↑ ↓` 或 `W S` 移动，`Enter` / 空格 确认；鼠标：悬停即选中，左键确认。无操作返回 `NONE` |
| `void Draw()` | 绘制（内部自动算布局，窗口尺寸变化也自适应） |
| `int GetSelected()` | 当前选中项下标，做音效 / 多语言时可用 |
| `static void DrawTextBlock(Font, utf8, centerX, topY, fontSize, spacing, color)` | 多行居中文字（传 UTF-8），画自定义文案时可用 |
| `static float TextBlockHeight(utf8, fontSize)` | 多行文字高度 |

`MenuAction`：`NONE`(无) / `START_GAME`(开始) / `RESUME`(继续) / `RESTART`(重开) / `HELP`(说明) / `BACK`(返回) / `EXIT_GAME`(退出)。
控件只负责"报动作"，具体做什么由 `main.cpp` 决定，方便其他人接自己的逻辑。

## 三、GameTimer —— 计时器

```cpp
GameTimer timer;
timer.SetFont(font);
timer.Start();                       // 开局 / 重开：归零并开始计时
timer.Update();                      // 每帧一次（只在 PLAYING 状态调用，暂停自然就不走了）
timer.Pause();  timer.Resume();  timer.Reset();
timer.DrawHUD(20, 16, UI_TEXT_SIZE, RAYWHITE);   // 角落显示 "用时 MM:SS.mmm"
```

| 接口 | 说明 |
|---|---|
| `void Start() / Pause() / Resume() / Reset()` | 归零并开始 / 暂停 / 继续 / 归零并停止 |
| `void Update()` / `void Update(double dt)` | 累加时间；不在计时状态时不累加 |
| `bool IsRunning()` / `double GetElapsed()` | 是否在走 / 已用秒数 |
| `const char* GetText()` | `"MM:SS.mmm"`，内部缓冲，可直接 `DrawText` |
| `static const char* Format(double sec, char* buf, int size)` | 秒数转字符串 |
| `void SetPrefix(const char* gbk)` | HUD 前缀，默认 `"用时 "` |
| `void SetFont(Font)` / `void DrawHUD(x, y, fontSize, color)` | 字体 / 绘制 |

## 四、main.cpp 状态机

| 状态 | 行为 |
|---|---|
| `MENU` | `mainMenu.Update()`；`ESC` 直接退出 |
| `HELP` | 操作说明弹窗；`Enter` / `ESC` 返回主菜单 |
| `PLAYING` | `player.Update(dt)` + `timer.Update()`；`ESC` -> `timer.Pause()` 并切到 `PAUSED` |
| `PAUSED` | `pauseMenu.Update()`；`ESC` 直接继续；菜单里可继续 / 重开 / 回主菜单 |

两个容易踩的坑：

1. `main.cpp` 里调了 `SetExitKey(KEY_NULL)`。raylib 默认按 `ESC` 会直接关窗口，不关掉的话暂停功能就废了。
2. 鼠标指针：游戏中 `DisableCursor()`，菜单 / 暂停 `EnableCursor()`，否则暂停时鼠标动不了，点不到按钮。

## 五、编码约定（重要）

仓库现有源码是 **GBK**（Visual Studio 中文版默认），新模块也是 GBK，所以：

- 菜单文案直接写中文即可（`Menu` 内部自动转 UTF-8）；
- 自己调 `DrawTextEx` 画中文时，记得套 `Utf8("...")`；
- 新增汉字要补进 `Config.h` 的 `UI_GLYPHS`；
- 换编译器 / 部署到非中文 Windows 时，源码编码需要统一（现有工程本来就是 GBK，保持一致最省事）。
