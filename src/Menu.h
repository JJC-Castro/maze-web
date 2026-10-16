#pragma once
#include "raylib.h"
#include "Config.h"
#include <string>
#include <vector>

// 菜单被激活后返回的动作，具体做什么由 main.cpp 决定
enum class MenuAction
{
    NONE = 0,       // 本帧没有操作
    START_GAME,     // 开始游戏
    RESUME,         // 继续游戏
    RESTART,        // 重新开始
    HELP,           // 打开操作说明
    BACK,           // 返回上一层
    EXIT_GAME,      // 退出游戏
    OPEN_DIFFICULTY,   // 难度菜单
    MODE_NORMAL,       // 选正常模式（第一人称）
    MODE_NAILONG,      // 选奶龙模式（第三人称奶龙）
    DIFF_EASY,         
    DIFF_NORMAL,      
    DIFF_HARD,
    DIFF_HELL,         // 地狱（困难 + 视野受限）

    DIFF_HOLY,         // 神圣：地狱的墙 + 玻璃墙 / 可破墙
    DIFF_HUNGER,       // 饥饿：神圣 + 饱腹值（能量条换成饱腹值）
    GUIDE_PREV,        // 玩法说明：上一页
    GUIDE_NEXT,        // 玩法说明：下一页
    DIFF_ROGUE,        // 肉鸽：5 层连打 + 层间选增益
    OPEN_LOAD_MENU,
   
    SAVE_SLOT_1,     
    SAVE_SLOT_2,
    SAVE_SLOT_3,
    LOAD_SLOT_1,
    LOAD_SLOT_2,
    LOAD_SLOT_3,

    OPEN_RANKING,      // 打开记录排名（主菜单 / 暂停 / 结算里都放了一栏）

    INFO_EASY,         // 难度详情弹窗（点难度右边的 ? ）
    INFO_NORMAL,
    INFO_HARD,
    INFO_HELL,

    INFO_HOLY,
    INFO_HUNGER,       // 饥饿难度详情
    INFO_ROGUE,        // 肉鸽难度详情

    RL_AUTO,           // 管理员暂停菜单：RL 机器人自动通关
    RL_TRAIN,          // 管理员暂停菜单：RL 训练桥
    RL_STOP,           // 管理员暂停菜单：停止 RL，恢复手动
    ADMIN_BUFFS,       // 管理员暂停菜单：增益选择页（选中即获得）
    ADMIN_NEXT_FLOOR,  // 管理员暂停菜单：直接跳到下一层
    ADMIN_PREV_FLOOR,   // 管理员暂停菜单：回到上一层
    ADMIN_CONTINUE     // 管理员结算弹窗：继续游戏（重开本层，保留状态）
};

// 外观：PAGE = 整页（开始界面）；DIALOG = 居中弹窗（暂停面板，自带半透明遮罩）
enum class MenuStyle { PAGE, DIALOG };

struct MenuItem
{
    std::string label;      // 显示文字（GBK 传入，内部转 UTF-8）
    MenuAction  action;     // 激活时返回的动作
    MenuAction  infoAction = MenuAction::NONE;   // 点右侧 (?) 时返回的动作（NONE = 不画 ? ）
};

class Menu
{
public:
    Menu() = default;
    Menu(const std::string& gbkTitle, std::vector<MenuItem> menuItems, MenuStyle style = MenuStyle::PAGE);

    // ---------- 配置（文字按 GBK 传入，内部自动转 UTF-8）----------
    void Init(const std::string& gbkTitle, std::vector<MenuItem> menuItems, MenuStyle style = MenuStyle::PAGE);
    void SetTitle(const std::string& gbkTitle);
    void SetSubtitle(const std::string& gbkText);   // 支持 '\n' 换行
    void SetHint(const std::string& gbkText);       // 底部提示
    void SetFont(Font f) { font = f; }
    void SetItems(std::vector<MenuItem> menuItems); // 重建条目（GBK 传入），选中项归零

    // ---------- 每帧调用 ----------
    void Reset();               // 打开菜单时调用：选中项复位
    MenuAction Update();        // 读键鼠输入，返回本帧被激活的动作（没有则 NONE）
    void Draw();                // 绘制，需在 BeginDrawing() / EndDrawing() 之间调用

    // ---------- 查询 ----------
    int  GetSelected() const { return selected; }
    bool IsEmpty() const { return items.empty(); }

    // ---------- 多行文字工具（'\n' 分行并水平居中，文本需已是 UTF-8）----------
    static void  DrawTextBlock(Font font, const std::string& utf8Text, float centerX, float topY,
                               int fontSize, float spacing, Color color);
    static float TextBlockHeight(const std::string& utf8Text, int fontSize);

private:
    void Layout();              // 计算面板与各项矩形（Update / Draw 内部自动调用）

    std::string title, subtitle, hint;      // 均已转成 UTF-8
    std::vector<MenuItem> items;
    MenuStyle style = MenuStyle::PAGE;
    Font font{};
    int  selected = 0;

    Rectangle panel{ 0.0f, 0.0f, 0.0f, 0.0f };      // 缓存的布局结果
    std::vector<Rectangle> rects;
    float titleY = 0.0f, subtitleY = 0.0f, hintY = 0.0f;

    // ---------- 外观（可在构造后按需覆盖）----------
    int   titleSize = UI_TITLE_SIZE;
    int   textSize = UI_TEXT_SIZE;
    int   hintSize = UI_HINT_SIZE;
    Color bgTop = { 10, 14, 28, 255 };
    Color bgBottom = { 34, 48, 92, 255 };
    Color panelColor = { 18, 24, 44, 235 };
    Color borderColor = { 90, 130, 220, 255 };
    Color titleColor = { 245, 245, 245, 255 };
    Color textColor = { 185, 195, 215, 255 };
    Color selColor = { 58, 96, 190, 255 };
    Color selTextColor = { 255, 255, 255, 255 };
    Color hintColor = { 140, 150, 175, 255 };
};
