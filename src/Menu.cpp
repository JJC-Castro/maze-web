#include "Menu.h"
#include "UIFont.h"
#include <cmath>

namespace
{
    constexpr float kTextSpacing = 1.0f;    // 字间距
    constexpr float kLineExtra   = 8.0f;    // 行高附加值
    constexpr float kPageItemW   = 420.0f;  // 整页菜单里菜单项的最大宽度
    constexpr float kSelRound    = 0.35f;   // 选中项圆角
    constexpr float kInfoOffsetX = 26.0f;   // (?) 图标中心相对菜单项右边缘的偏移
    constexpr float kInfoRadius  = 15.0f;   // (?) 图标半径
    constexpr float kDialogMargin = 28.0f;  // DIALOG 面板与窗口边的最小间距

    // 菜单项右侧 (?) 图标的圆心
    Vector2 InfoCenter(const Rectangle& r)
    {
        return Vector2{ r.x + r.width + kInfoOffsetX, r.y + r.height * 0.5f };
    }

    Font  PickFont(Font f) { return (f.texture.id != 0) ? f : GetFontDefault(); }
    float LineH(int fontSize) { return (float)fontSize + kLineExtra; }
}

Menu::Menu(const std::string& gbkTitle, std::vector<MenuItem> menuItems, MenuStyle menuStyle)
{
    Init(gbkTitle, std::move(menuItems), menuStyle);
}

void Menu::Init(const std::string& gbkTitle, std::vector<MenuItem> menuItems, MenuStyle menuStyle)
{
    SetTitle(gbkTitle);

    items.clear();
    items.reserve(menuItems.size());
    for (const MenuItem& it : menuItems)
        items.push_back(MenuItem{ Utf8(it.label), it.action, it.infoAction });

    style = menuStyle;
    selected = 0;
    Layout();
}

void Menu::SetTitle(const std::string& gbkTitle)   { title = Utf8(gbkTitle); }
void Menu::SetSubtitle(const std::string& gbkText) { subtitle = Utf8(gbkText); }
void Menu::SetHint(const std::string& gbkText)     { hint = Utf8(gbkText); }

void Menu::SetItems(std::vector<MenuItem> menuItems)
{
    items.clear();
    items.reserve(menuItems.size());
    for (const MenuItem& it : menuItems)
        items.push_back(MenuItem{ Utf8(it.label), it.action, it.infoAction });

    selected = 0;
    Layout();
}

void Menu::Reset()
{
    selected = 0;
}

void Menu::Layout()
{
    rects.clear();

    const int   n  = (int)items.size();
    const float sw = (float)GetScreenWidth();
    const float sh = (float)GetScreenHeight();

    const float itemH  = (float)textSize + 2.0f * (float)UI_ITEM_PADY;
    const float listH  = (n > 0) ? ((float)n * itemH + (float)(n - 1) * (float)UI_ITEM_GAP) : 0.0f;
    const float titleH = title.empty()    ? 0.0f : LineH(titleSize) + 10.0f;
    const float subH   = subtitle.empty() ? 0.0f : TextBlockHeight(subtitle, textSize) + 10.0f;
    const float hintH  = hint.empty()     ? 0.0f : LineH(hintSize) + 10.0f;
    const float contentH = titleH + subH + listH + hintH;

    // 行高 / 行距 / 内边距走这三个可变量：DIALOG 一屏放不下时会收紧它们。
    float itemHFit   = itemH;
    float gapFit     = (float)UI_ITEM_GAP;
    float padYFit    = (float)UI_ITEM_PADY;
    float contentFit = contentH;

    if (style == MenuStyle::PAGE)
    {
        panel = Rectangle{ 0.0f, 0.0f, sw, sh };
    }
    else
    {
        const float panelW = UI_PANEL_WIDTH;
        const float availH = sh - 2.0f * kDialogMargin;
        // 管理员暂停菜单最坏有 10 项，默认字号 + 行距会把面板撑到约 880 px，
        // 而窗口只有 720 px —— 下半截直接画到屏幕外。放不下就一档档收紧：
        // 先压行高（到 字号+4 为止），再压行距，最后压内边距。
        for (int guard = 0; guard < 96; ++guard)
        {
            const float listFit = (n > 0)
                ? ((float)n * itemHFit + (float)(n - 1) * gapFit) : 0.0f;
            contentFit = titleH + subH + listFit + hintH;
            if (contentFit + 4.0f * padYFit <= availH) break;
            if (itemHFit > (float)textSize + 4.0f)
                itemHFit = fmaxf((float)textSize + 4.0f, itemHFit - 4.0f);
            else if (gapFit > 2.0f) gapFit -= 1.0f;
            else if (padYFit > 2.0f) padYFit -= 1.0f;
            else break;
        }
        const float panelH = contentFit + 4.0f * padYFit;
        panel = Rectangle{ (sw - panelW) * 0.5f,
                           fmaxf(kDialogMargin, (sh - panelH) * 0.5f),
                           panelW, panelH };
    }

    float y = panel.y + (panel.height - contentFit) * 0.5f;
    titleY = y;    y += titleH;
    subtitleY = y; y += subH;

    const float itemW = (style == MenuStyle::PAGE)
                      ? fminf(panel.width - 2.0f * (float)UI_ITEM_PADX, kPageItemW)
                      : panel.width - 2.0f * (float)UI_ITEM_PADX;
    const float itemX = panel.x + (panel.width - itemW) * 0.5f;

    for (int i = 0; i < n; ++i)
    {
        rects.push_back(Rectangle{ itemX, y, itemW, itemHFit });
        y += itemHFit + gapFit;
    }
    hintY = y;
}

MenuAction Menu::Update()
{
    if (items.empty()) return MenuAction::NONE;

    Layout();
    const int n = (int)items.size();

    // 键盘导航
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) selected = (selected + 1) % n;
    if (IsKeyPressed(KEY_UP)   || IsKeyPressed(KEY_W)) selected = (selected - 1 + n) % n;

    // 鼠标悬停 / 点击
    const Vector2 mouse = GetMousePosition();
    for (int i = 0; i < n; ++i)
    {
        // 右侧 (?) 图标：点它返回详情动作
        if (items[i].infoAction != MenuAction::NONE)
        {
            const Vector2 icon = InfoCenter(rects[i]);
            if (CheckCollisionPointCircle(mouse, icon, kInfoRadius + 3.0f))
            {
                selected = i;
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return items[i].infoAction;
                break;
            }
        }

        if (!CheckCollisionPointRec(mouse, rects[i])) continue;
        selected = i;
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return items[i].action;
        break;
    }

    // 键盘确认
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) || IsKeyPressed(KEY_SPACE))
        return items[selected].action;

    return MenuAction::NONE;
}

void Menu::Draw()
{
    Layout();

    const Font  f  = PickFont(font);
    const float cx = panel.x + panel.width * 0.5f;

    if (style == MenuStyle::PAGE)
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), bgTop, bgBottom);
    else
        DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, UI_DIALOG_DIM));

    DrawRectangleRounded(panel, UI_ROUND, 12, panelColor);
    DrawRectangleRoundedLinesEx(panel, UI_ROUND, 12, 2.0f, borderColor);

    if (!title.empty())
        DrawTextBlock(f, title, cx, titleY, titleSize, kTextSpacing, titleColor);
    if (!subtitle.empty())
        DrawTextBlock(f, subtitle, cx, subtitleY, textSize, kTextSpacing, textColor);

    for (int i = 0; i < (int)rects.size(); ++i)
    {
        const Rectangle r = rects[i];
        const bool  sel = (i == selected);
        const float cy  = r.y + r.height * 0.5f;

        if (sel)
        {
            DrawRectangleRounded(r, kSelRound, 8, selColor);
            DrawRectangleRoundedLinesEx(r, kSelRound, 8, 1.5f, borderColor);
            DrawTriangle(Vector2{ r.x + 16.0f, cy + 8.0f },
                         Vector2{ r.x + 16.0f, cy - 8.0f },
                         Vector2{ r.x + 27.0f, cy },
                         selTextColor);
        }

        const Vector2 m = MeasureTextEx(f, items[i].label.c_str(), (float)textSize, kTextSpacing);
        const Vector2 p = Vector2{ r.x + (r.width - m.x) * 0.5f, cy - m.y * 0.5f };
        DrawTextEx(f, items[i].label.c_str(), p, (float)textSize, kTextSpacing,
                   sel ? selTextColor : textColor);

        // 右侧 (?) 图标（点开难度详情）
        if (items[i].infoAction != MenuAction::NONE)
        {
            const Vector2 icon = InfoCenter(r);
            const bool hot = CheckCollisionPointCircle(GetMousePosition(), icon, kInfoRadius + 3.0f);
            DrawCircleV(icon, kInfoRadius, hot ? selColor : Fade(RAYWHITE, 0.14f));
            DrawCircleLines((int)icon.x, (int)icon.y, kInfoRadius, borderColor);

            const float qs = (float)hintSize + 8.0f;
            const Vector2 qm = MeasureTextEx(f, "?", qs, kTextSpacing);
            DrawTextEx(f, "?", Vector2{ icon.x - qm.x * 0.5f, icon.y - qm.y * 0.5f },
                       qs, kTextSpacing, RAYWHITE);
        }
    }

    if (!hint.empty())
        DrawTextBlock(f, hint, cx, hintY, hintSize, kTextSpacing, hintColor);
}

void Menu::DrawTextBlock(Font font, const std::string& utf8Text, float centerX, float topY,
                         int fontSize, float spacing, Color color)
{
    float  y = topY;
    size_t start = 0;
    while (true)
    {
        const size_t end = utf8Text.find('\n', start);
        const std::string line = utf8Text.substr(start, (end == std::string::npos) ? std::string::npos : end - start);
        if (!line.empty())
        {
            const Vector2 m = MeasureTextEx(font, line.c_str(), (float)fontSize, spacing);
            DrawTextEx(font, line.c_str(), Vector2{ centerX - m.x * 0.5f, y },
                       (float)fontSize, spacing, color);
        }
        y += LineH(fontSize);
        if (end == std::string::npos) break;
        start = end + 1;
    }
}

float Menu::TextBlockHeight(const std::string& utf8Text, int fontSize)
{
    if (utf8Text.empty()) return 0.0f;
    int lines = 1;
    for (char c : utf8Text) if (c == '\n') ++lines;
    return (float)lines * LineH(fontSize);
}
