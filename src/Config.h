#pragma once
#include "raylib.h"
// 玩家参数
#define WALKSPEED 4.0f			// 行走速度 (m/s)
#define SPRINTSPEED 8.0f		// 奔跑速度 (m/s)
#define SPRINT_MAX_SECONDS 5.0f		// 冲刺能量全满可持续加速时长 (s)，改这里调能跑多久
#define SPRINT_RECOVER_SECONDS 15.0f	// 冲刺能量从空回满所需时间 (s)，改这里调回复快慢
#define JUMPFORCE 5.0f			// 起跳初速度 (m/s)
#define GRAVITY -9.8f			// 重力加速度 (m/s^2)
#define EYE_HEIGHT 1.8f			// 眼睛相对脚部的高度 (m)
#define HALFWIDTH 0.64f		    // 碰撞半宽 (m)：按奶龙模型足迹取（半宽 0.633），贴墙不穿模
#define GROUND_Y 0.1f		    // 地面顶面高度 (m)，用于落地检测
#define MOUSESENS 0.003f	    // 鼠标灵敏度
#define PITCH_LIM 1.5f 		    // 俯仰角上限（弧度），防止翻转


// 窗口参数(分辨率)
#define WINDOW_WIDTH 1280 		// 窗口宽度
#define WINDOW_HEIGHT 720 		// 窗口高度
//宝物数量
// ---- 光源 / 视野限制（只有"地狱"难度启用，见 IsLightLimited）----
#define LIGHT_RADIUS_METERS 5.0f	// 光照半径 N（米）：只看得清 N 米内，改这里调难度
#define LIGHT_INNER_RATIO 0.0f	// 半径内这个比例范围内保持全亮（0~1）
#define LIGHT_DARKNESS 1.0f		// 超出半径后的压黑程度（0=不黑，1=最黑）
#define LIGHT_BLUR_AMOUNT 2.0f	// 远处径向模糊强度（0=不糊）
#define LIGHT_EDGE_DARKNESS 0.85f	// 画面边缘额外压暗（0~1）
// ---- 登录 / 记录排名 ----
#define RANK_LIST_MAX 10		// 排名页每个难度显示多少条记录
#define RANK_NAME_MAX_CHARS 12	// 用户名最大长度（字符数）
// ---- 奶龙（第三人称主角）----
#define NAILONG_MODEL_PATH       "assets/models/nailong.glb"	// 相对游戏根目录；文件不存在时自动用程序化奶龙
#define NAILONG_TEXTURE_PATH     "assets/models/nailong_color.png"	// 贴图独立成文件：本工程 raylib 构建加载不了 glb 内嵌贴图
#define NAILONG_MODEL_SCALE      1.75f							// 模型整体缩放
#define NAILONG_MODEL_YAW_OFFSET 270.0f							// 现状：模型正面在模型空间的 +X，转到角色正面(+Z)要 +270 度

// ---- 软件层骨架（软件蒙皮）：静态 glb 没有骨骼/动画，用这里标定出骨架补上走跑跳 ----
#define NAILONG_RIG_ENABLE       1		// 1=开启（0 则静态模型只会站着不动）
#define NAILONG_ANIM_IDLE        "Idle"		// 动画名按“子串匹配”，带 Armature| 前缀也能认出来
#define NAILONG_ANIM_WALK        "Walk"
#define NAILONG_ANIM_RUN         "Run"
#define NAILONG_ANIM_JUMP        "Jump"
#define NAILONG_ANIM_SHOOT       "Shoot"
#define NAILONG_ANIM_LAUGH       "Laugh"

// ---- 第三人称相机 ----
// ---------------- 第三人称环绕相机（TPCamera）----------------
#define TP_CAM_DIST          4.20f	// 相机到角色的基础距离 (m)：贴近一点，看得清奶龙
#define TP_CAM_MIN_DIST      2.20f	// 被墙挤压时的最小距离 (m)
#define TP_CAM_ZOOM_MIN      2.20f	// 滚轮最近距离 (m)，与撞墙最小值一致
#define TP_CAM_ZOOM_MAX      6.50f	// 滚轮最远距离 (m)：最高点仍低于墙顶
#define TP_CAM_ZOOM_STEP     0.35f	// 每格滚轮调整的距离 (m)
#define TP_CAM_LOOK_Y        1.20f	// 注视点高度（胸口）
#define TP_CAM_PITCH_DEFAULT 34.0f	// 默认俯角（度）：从角色后上方俯视
#define TP_CAM_PITCH_MIN   (-10.0f)	// 俯仰下限（度）
#define TP_CAM_PITCH_MAX     52.0f	// 俯角上限（度）：保证相机最高点不超过墙顶
#define TP_CAM_FOVY          62.0f	// 视野角（度）
#define TP_CAM_SENS_DEG      0.14f	// 鼠标灵敏度（度/像素）
#define TP_CAM_COLLIDE_PAD   0.30f	// 避墙检测的相机体积半径 (m)
#define TP_CAM_COLLIDE_STEP  0.20f	// 避墙逐点采样步长 (m)
#define TP_CAM_LIFT_STEP     9.0f	// 被墙挤住时每次抬高的角度（度）
#define TP_CAM_LIFT_TRIES    6		// 抬高机位的尝试次数
#define TP_CAM_POS_SMOOTH    9.0f	// 机位平滑速度（越大越跟手）
#define TP_CAM_FOCUS_SMOOTH  16.0f	// 注视点平滑速度
#define TP_CAM_IN_SPEED      26.0f	// 撞墙时距离回缩速度（要快，防穿模）
#define TP_CAM_OUT_SPEED     4.5f	// 离墙后距离伸出速度（要慢，防忽然弹开）

// ---------------- 射击：范围内自动索敌 ----------------
#define PLAYER_AUTO_AIM_RANGE 9.0f	// 自动索敌半径 (m)：范围内最近、有视线的敌人

// ---- 管理员模式 ----
#define ADMIN_PASSWORD "123456"		// 管理员密码（回车确认）
#define RL_PASSWORD "rl666666"		// RL 自动通关模式密码（管理员模式右下角入口）
#define ADMIN_FLY_SPEED 10.0f		// 管理员飞行升降速度 (m/s)
#define ADMIN_FLY_MAX_HEIGHT 55.0f	// 管理员飞行最大高度 (m)
#define ADMIN_FLY_KEY KEY_Q		// 管理员起飞键：长按它才起飞（Space 永远只负责跳跃）
#define ADMIN_FLY_HOLD 0.20f		// 管理员：按住 ADMIN_FLY_KEY 超过这个秒数算起飞（防止手抖误触）


#define TREASURE_SCORE_COUNT   4   // 加分宝物（金）
#define TREASURE_ATTACK_COUNT  2   // 加攻击次数宝物（蓝）
#define TREASURE_HEAL_COUNT    2   // 回血宝物（红）
#define TREASURE_TOTAL_COUNT   (TREASURE_SCORE_COUNT + TREASURE_ATTACK_COUNT + TREASURE_HEAL_COUNT)

// ---- 减速带（红色地面区）----
// 条数 / 格数 / 减速倍率随难度提升，见 GetDifficultyConfig；颜色和贴地厚度在这里调
#define SLOW_ZONE_THICKNESS 0.08f	// 减速带薄板厚度 (m)
#define SLOW_ZONE_TOP_LIFT  0.005f	// 顶面比地面高出多少 (m)，避免和地面共面闪烁
constexpr Color SLOW_ZONE_COLOR{ 200, 40, 40, 255 };	// 减速带颜色（红色打底）

// ---- 地雷（红黄棋盘格扁圆盘）----
// 扣血 / 尺寸 / 触发判定 / 起点留空 / 雷间距 / 闪烁与震动时长，都在这里调
#define MINE_DAMAGE 5			// 踩到地雷扣的血量
#define MINE_RADIUS 0.35f		// 地雷半径 (m)
#define MINE_HEIGHT 0.12f		// 地雷高度 (m)，扁圆盘
#define MINE_TRIGGER_RADIUS 0.6f	// 踩踏判定半径 (m)
#define MINE_TRIGGER_HEIGHT_TOLERANCE 0.25f	// 脚底高于顶面这么多就不触发（可跳过去）
#define MINE_SPAWN_CLEAR_RADIUS 2	// 起点周围多少格（切比雪夫）不刷地雷
#define MINE_MIN_PATH_GAP 5		// 两颗地雷之间至少相隔多少格（按通路距离）
#define MINE_MIN_START_PATH_CELLS 12	// 地雷离起点的最小通路距离（格数地板）
#define MINE_MIN_START_PATH_RATIO 0.20f	// 至少占入口->出口最短通路长度的比例(随迷宫大小可变）
#define MINE_FLASH_SECONDS 0.10f	// 踩雷红色全屏闪烁时长 (s)，后期可调
#define MINE_SHAKE_SECONDS 0.30f	// ???????? (s)
#define MINE_BLIND_SECONDS 1.5f		// ?????? (s)????????????
#define MINE_BLIND_FADE 0.25f		// ?????? / ????????? (s)
#define MINE_SHAKE_STRENGTH 0.20f	// 屏幕震动幅度 (m)
constexpr Color MINE_COLOR_RED{ 205, 45, 45, 255 };		// 棋盘格：红
constexpr Color MINE_COLOR_YELLOW{ 240, 200, 60, 255 };	// 棋盘格：黄
constexpr Color MINE_COLOR_BODY{ 120, 25, 25, 255 };		// 侧壁 / 底座深红

// ---- 玩家脚步声 / 落地 / 喘气 ----
// 这些是手感参数（米 / 比例 / 秒），音量在 SfxAudio.h 的 SfxConfig 里
#define PLAYER_WALK_STRIDE 1.15f		// 走路多少米算一步
#define PLAYER_SPRINT_STRIDE 0.85f		// 跑步步伐更密
#define PLAYER_LAND_MIN_FALL_SPEED 3.0f	// 落地竖直速度低于这个值不播放落地声
#define PLAYER_PANT_STAMINA_RATIO 0.50f	// 喘气阈值：冲刺能量小于该比例时喘气


// ---- 游戏状态（主循环用）----
enum class GameState
{
	MENU,       // 开始界面
	HELP,       // 操作说明
	DIFFICULTY,   // 难度
	LOAD_MENU,   // 槽位
	LOGIN,        // 登录（输入用户名 / 跳过）
	MODE_SELECT,  // 模式选择（正常 / 奶龙）
	RANKING,      // 记录排名
	DIFF_INFO,    // 难度详情（点难度右边的 ? 弹出）
	ADMIN_LOGIN,  // 管理员密码输入
	ADMIN_BUFFS,  // 管理员：增益选择页（暂停菜单右下角进入，选中即获得）
	RL_LOGIN,     // RL 模式密码输入（管理员模式右下角入口）
	ROGUE_REWARD, // 肉鸽：层间增益选择弹窗
	PLAYING,    // 游戏中
	PAUSED,     // 暂停菜单
	CONFIRM_EXIT // 退出游戏前的确认框（防止误触发杀进程）
};

// ---- 难度 ----
enum class Difficulty
{
	EASY,     // 简单：小迷宫，少敌人，血多
	NORMAL,   // 普通：默认
	HARD,     // 困难：大迷宫，多敌人，血少
	HELL,     // 地狱：困难 + 视野受限（光源只照亮 N 米）
	HOLY,     // 神圣：地狱的一切 + 玻璃墙 / 可破墙（见 main.cpp 的神圣模式布置）
	HUNGER,   // 饥饿：神圣的一切 + 用饱腹值代替能量条（饱腹耗尽会致盲并掉血）
	ROGUE     // 肉鸽：5 层连打（简单/困难/地狱/神圣/神圣全透明），层间选增益
};

// ---- 模式（登录 / 跳过后先选模式，再选难度）----
enum class GameMode
{
	NORMAL,   // 正常模式：第一人称原版玩法（锁定第一人称）
	NAILONG   // 奶龙模式：第三人称奶龙，可 V 切视角，也支持管理员模式
};

// ---- 肉鸽模式：层间可选的增益：一般 19 种 + 特殊 8 种 + 稀有 3 种 ----
// 三个档位各自连续成段（BUFF_*_FIRST/LAST 就是遍历范围），新增增益只能
// 追加到对应档位的末尾，不要往中间插，否则抽卡的概率分档会错位。
enum class Buff
{
	// ---- 一般增益（普通概率，免费）----
	HUNGER_SLOW,     // 饱腹值消耗减半
	ENEMY_WEAK,      // 怪物伤害更低（每下最多 5 点）
	STRONG_BULLET,   // 子弹伤害更高（一发打掉一只怪）
	FAST_MOVE,       // 走路 / 跑步都翻倍
	DARK_FREE,       // 开局 30s 内无光源限制
	MORE_HP,         // 生命上限 +50
	MORE_SCORE,      // 立刻 +50 分
	MINE_NO_BLIND,   // 地雷不再致盲（伤害不变）
	BREAK_ALL,       // 拆掉所有可破墙
	SHOW_EXIT,       // 雷达显示终点方向（绿点）
	MORE_SATIETY,    // 饱腹上限 +20 并回等量
	ENEMY_HALF,      // 怪物数量减半（此后每层）
	TREASURE_X2,     // 金色宝物数量翻倍（此后每层）
	AMMO_X2,         // 弹药包数量翻倍（此后每层）
	HEAL_X2,         // 血包数量翻倍（此后每层）
	DOUBLE_JUMP,     // 解锁二段跳（空中还能再跳一次）
	SLOW_TIMER,      // 计时速度 -50%（1 秒只记 0.5 秒）
	KILL_SATIETY,    // 每击杀一只怪回 10 饱腹值
	HURT_SPEED,      // 受伤后 5s 内移速提升到 4 倍
	// ---- 特殊增益（出现率 1/10，免费）----
	TELEPORT_START,  // 开局传送到随机出生点
	COPY_MAP,        // 下一层复刻本层地图
	NO_ENEMY,        // 之后不再出现怪物
	NO_MINE,         // 之后不再出现地雷
	INF_SATIETY,     // 饱腹值无限
	NO_LIGHT_LIMIT,  // 常态无光源限制（原来的「狂笑」改成不需要大笑也生效）
	SHRINK_MAZE,     // 迷宫缩小一档
	LOCK_HP,         // 锁血（代价：无法跑步）
	HOVER,           // 特殊：长按空格可以一直滞空（松开就落）
	// ---- 稀有增益（出现率 1/20，要花 1 个宝物买）----
	COMPANION,       // 伙伴带路（绿方块带你到终点并帮打怪；血上限锁 10、移速 1/4）
	PHASE_THROUGH,   // 穿墙（代价：下一层回到第一层重新开始）
	SKIP_FLOOR       // 直接跳过下一层（代价：用时 +150s）
};

// 三档增益的枚举范围（main.cpp 的抽卡 / 遍历都用这三个区间）
#define BUFF_GENERAL_FIRST ((int)Buff::HUNGER_SLOW)
#define BUFF_GENERAL_LAST  ((int)Buff::HURT_SPEED)
#define BUFF_SPECIAL_FIRST ((int)Buff::TELEPORT_START)
#define BUFF_SPECIAL_LAST  ((int)Buff::HOVER)
#define BUFF_RARE_FIRST    ((int)Buff::COMPANION)
#define BUFF_RARE_LAST     ((int)Buff::SKIP_FLOOR)

struct DifficultyConfig
{
	int mazeWidth;        // 迷宫宽（必须奇数）
	int mazeHeight;       // 迷宫高（必须奇数）
	int enemyCount;       // 敌人总数
	int playerMaxHealth;  // 玩家初始 HP
	int attackLimit;      // 初始攻击次数

	// ---- 减速带（红色地面区）：普通难度 2~4 格，难度越高格数越多、减速越强 ----
	int slowZoneSegments;  // 减速带条数
	int slowZoneMinCells;  // 每条最少格数
	int slowZoneMaxCells;  // 每条最多格数
	float slowZoneFactor;  // 减速带上速度倍率（0~1，越小越慢）
	int mineCount;         // 地雷数量（按难度）
};

// ---- UI / 菜单参数 ----
#define UI_FONT_SIZE 64			// 字体图集加载字号（比显示字号大，缩放更清晰）
#define UI_TITLE_SIZE 64			// 标题字号
#define UI_TEXT_SIZE 30			// 正文字号（菜单项 / 副标题）
#define UI_HINT_SIZE 20			// 提示字号
#define UI_ITEM_GAP 14			// 菜单项之间的间距 (px)
#define UI_ITEM_PADX 26			// 菜单项左右内边距 (px)
#define UI_ITEM_PADY 12			// 菜单项上下内边距 (px)
#define UI_PANEL_WIDTH 520.0f		// 弹窗宽度 (px)
#define UI_ROUND 0.28f			// 面板圆角比例 (0~1)
#define UI_DIALOG_DIM 0.55f		// 弹窗打开时背景压暗程度 (0~1)

// 三档难度的具体参数
inline DifficultyConfig GetDifficultyConfig(Difficulty d)
{
	switch (d)
	{
	case Difficulty::EASY:
		return { 15, 15, 2, 150, 15, 3, 3, 5, 0.60f, 2 };   // 15×15，2 个敌人，150 HP，15 次攻击；3 条减速带，3-5 格，0.60倍速
	case Difficulty::HARD:
		return { 31, 31, 6,  70,  6, 4, 3, 5, 0.50f, 6 };  // 困难：4 条减速带，3-5 格，0.50 倍速
	case Difficulty::HELL:               // 地狱 = 困难 + 视野受限（光照在 main.cpp 里开）
		return { 31, 31, 6,  70,  6, 3, 3, 5, 0.40f, 8 };  // 地狱：3 条减速带，3-5 格，0.40 倍速
	case Difficulty::HOLY:               // 神圣 = 地狱（除了墙的花样，其他一模一样）
		return { 31, 31, 6,  70,  6, 3, 3, 5, 0.40f, 8 };
	case Difficulty::HUNGER:             // 饥饿 = 神圣的墙和光环照旧，只是初始血 30 + 饱腹值机制
		return { 31, 31, 6,  30,  6, 3, 3, 5, 0.40f, 8 };
	case Difficulty::ROGUE:              // 肉鸽每层各用自己的难度参数，这条只是兜底（第 1 层 = 简单）
		return { 15, 15, 2, 100, 15, 3, 3, 5, 0.60f, 2 };
	case Difficulty::NORMAL:
	default:
		return { 21, 21, 4, 100, 10, 1, 2, 4, 0.50f, 4 };   // 21×21，4 个敌人，100 HP，10 次攻击
	}
}

// 地狱系难度（地狱 / 神圣）：视野受限、恐怖氛围、安静的音效
inline bool IsHellLike(Difficulty d)
{
	return d == Difficulty::HELL || d == Difficulty::HOLY || d == Difficulty::HUNGER;
}

// 只有地狱系难度启用光源（视野受限）
inline bool IsLightLimited(Difficulty d)
{
	return IsHellLike(d);
}

// ---- 神圣模式：墙的花样 ----
#define HOLY_GLASS_WALL_MIN 8   // 随机变成玻璃墙的面数下限（看上去什么都没有，实则挡路）
#define HOLY_GLASS_WALL_MAX 10  // 上限
#define HOLY_BREAK_WALLS    2   // 堵在路口的可破墙数量（颜色更深，一发子弹打掉）
#define HOLY_BREAK_SHOOT_RANGE 25.0f   // 可破墙的射击距离 (m)：视线里这个距离内的可破墙能打碎
#define HOLY_BREAK_FLASH_SECONDS 0.35f // 可破墙被打碎后的亮光时长 (s)，纯视觉反馈
constexpr Color HOLY_BREAK_WALL_COLOR{ 110, 106, 114, 255 };	// 可破墙：只比普通墙（GRAY 130）深一点
constexpr Color HOLY_GLASS_WALL_COLOR{ 255, 255, 255, 0 };      // 玻璃墙：半透明淡蓝，看得见却挡路（打不碎）
constexpr Color HOLY_GLASS_WALL_EDGE_COLOR{ 205, 240, 255, 130 }; // 玻璃墙描边：让玻璃在远处也看得出是一堵墙

// ---- 饥饿模式（难度第 6 项）：能量条换成饱腹值 ----
// 平时 4s 掉 1 点饱腹，冲刺 2s 掉 1 点（双倍）；归零当场致盲 3s，随后进入饥饿掉血态。
#define HUNGER_START_SATIETY 10.0f		// 饥饿模式初始饱腹值（上限同为这个数）
#define HUNGER_MAX_HEALTH 30			// 饥饿模式初始血量
#define HUNGER_TREASURE_SCALE 2			// 饥饿模式：宝物 / 弹药包 / 血包数量 = 神圣的两倍
#define HUNGER_DRAIN_SECONDS 4.0f		// 平时多少秒掉 1 点饱腹
#define HUNGER_SPRINT_DRAIN_SECONDS 2.0f	// 冲刺时多少秒掉 1 点饱腹（加速 = 双倍消耗）
#define HUNGER_BLIND_SECONDS 3.0f		// 饱腹耗尽的致盲时长 (s)，窗口全黑
#define HUNGER_STARVE_HP_SECONDS 4.0f	// 饥饿掉血：每多少秒 -1 血
#define HUNGER_STARVE_SPRINT_HP_SECONDS 2.0f	// 饥饿掉血：冲刺时每多少秒 -1 血
#define HUNGER_REFILL_PER_PICKUP 5.0f	// 吃到任意宝物 / 弹药包 / 血包回复的饱腹值
constexpr Color HUNGER_BAR_COLOR{ 225, 55, 55, 255 };		// 饱腹条：红
constexpr Color HUNGER_BAR_LOW_COLOR{ 150, 25, 25, 255 };	// 饱腹条：见底

// ---- 肉鸽模式（难度第 7 项）：5 层连打 + 层间选增益 ----
#define ROGUE_FLOOR_COUNT 5			// 层数：简单 / 困难 / 地狱 / 神圣 / 神圣全透明
#define ROGUE_START_HEALTH 100			// 肉鸽初始血量（层间继承）
#define ROGUE_START_SATIETY 30.0f		// 肉鸽初始饱腹值
#define ROGUE_DARK_FREE_SECONDS 30.0f	// 增益：开局 30s 内无光源限制
#define ROGUE_MORE_HP 50				// 增益：生命上限 +50
#define ROGUE_MORE_SCORE 50				// 增益：立刻 +50 分
#define ROGUE_MORE_SATIETY 20.0f		// 增益：饱腹上限 +20 并回等量
#define ROGUE_SPECIAL_RATE 0.10f		// 特殊增益的出现率（一般增益的 1/10）
#define ROGUE_RARE_RATE 0.05f			// 稀有增益的出现率（一般增益的 1/20）
#define ROGUE_RARE_COST 1			// 稀有增益的价钱：1 个宝物
#define ROGUE_REFRESH_TIMES 1			// 全局刷新机会：一局只有这么多次，用完不能刷新
#define ROGUE_SPECIAL_GUARANTEE_FLOOR_A 2	// 第 3 层（0 基 2）必出一个特殊增益
#define ROGUE_SPECIAL_GUARANTEE_FLOOR_B 3	// 第 4 层（0 基 3）必出一个特殊增益
#define ROGUE_RARE_GUARANTEE_FLOOR 4		// 第 5 层（0 基 4）必出一个稀有增益
#define ROGUE_RARE_GUARANTEE_GIFT 1		// 第 5 层的白送宝物数（正好够买那个稀有增益）
#define ROGUE_SKIP_FLOOR_PENALTY 150.0f		// 稀有「跳层」的代价：用时 +150 秒
#define ROGUE_HURT_SPEED_SECONDS 5.0f		// 一般「受伤提速」：持续 5 秒
#define ROGUE_HURT_SPEED_MUL 4.0f		// 一般「受伤提速」：移速倍率
#define ROGUE_KILL_SATIETY 10.0f		// 一般「击杀回饱腹」：每杀一只回多少
#define ROGUE_TIMER_SLOW_SCALE 0.5f		// 一般「计时减慢」：计时速度 -50%
#define ROGUE_COMPANION_HP_CAP 10		// 稀有「伙伴」：血量上限固定为 10
#define ROGUE_COMPANION_SPEED_DIV 4.0f		// 稀有「伙伴」：移速降到 1/4
#define ROGUE_COMPANION_AHEAD 5.0f		// 稀有「伙伴」：吊在玩家身前 3~5m 的带里带路
#define ROGUE_COMPANION_FIRE_SECONDS 2.0f	// 稀有「伙伴」：每 2 秒帮打一发
#define ROGUE_COMPANION_FIRE_RANGE 15.0f	// 稀有「伙伴」：索敌距离 (m)
#define ROGUE_ADMIN_COINS 99		// 管理员模式：宝物币直接给到这么多（等于无限）
constexpr Color ROGUE_COMPANION_COLOR{ 70, 240, 130, 255 };	// 稀有「伙伴」：绿色方块
constexpr Color ROGUE_RARE_TAG_COLOR{ 235, 120, 235, 255 };	// 弹窗里「稀有」两个字的颜色
constexpr Color ROGUE_LOCKED_TAG_COLOR{ 130, 130, 140, 255 };	// 买不起的稀有增益：灰
constexpr Color ROGUE_EXIT_MARK_COLOR{ 60, 230, 90, 255 };	// 雷达上的终点标记：绿


// 界面上会用到的全部文字（GBK）：新增中文文案时把新字补进来，否则会显示成 '?'
// （2026-10 补：说明书 / 增益表新增文案里漏收的字，一次性补齐）
#define UI_GLYPHS \
	"开始游戏 操作说明 退出 返回 主菜单 继续 重新 已暂停 当前用时 " \
	"移动 奔跑 跳跃 鼠标 转动视角 目标 走出迷宫 越短越好 " \
	"宝物 得分 通关 按 先收集全部齐再到向口" \
	"难度选择 简单 普通 困难 当前 " \
	"保存 加载 读取 存档 槽位 迷宫 成功 失败 文件 不存在 " \
	"一二三到 " \
	"上下键或选择 确认 直接 帮助 能量 地狱 视野 受限 光照 半径 米 ，。：、！？（）·" \
	"登录 用户名 请输入 后 或 点 跳过 游客 身份 才 记录 排名 次 暂 无 管理员 密码 错误 确认 模式 " \
	"地图 敌人 个 初始 生命 攻击 数 总数 限制 有 长 起飞 升降 成绩 已 详情 查看 金 蓝 红 " \
	"视野 难度 大小 关 于 true false" \
	"降落血锁定计入第三人称切换大笑画" \
	"中右侧该的是否为可格源未如旧即最多字也以空四别比高同就会这里日期" \
	"最高分榜 最快用时榜" \
	"神圣 特殊墙体玻璃面看不见却挡路可破堵住口一发子弹打碎替换但" \
	"饥饿肉鸽饱腹值平归零致盲之每补给药包复双倍消耗" \
	"慢化怪削弱必杀行暗强健防眩拆迁雷达食随启程刻清场狂减来掉只和都翻内并满立踩伤害变所把终绿些传送置层各增益勾效般左透间承现；约窗概率与途死亡" \
	"正常奶龙原版玩法支持疾优体冲刷刺力励咬器均奖局待心机权桥步秒等练缺自被训质贪连速" \
	"→买亮代价伙伴你免块壁少带捷提整方本此段永白稀穿缩色花要费还远" \
	"拿获" \
	"松滞" \
	"了候剩占叠哪唯完实对币弃我手找抽拥捡放某止相真知离端累线贴超足距蹭进逃道际" \
	"结算" \
	"—「」专世东两乐云五交什仅介从仓任低余例依便倒做偿像共具写况准则判剑匹压去台吃合啪喂围址坐坨型基境声外头套娱学安宏家容宽射将尺屏属巡帧幕序库底废建志态怎恢想意懂扣折挂据控改料断旁星显晰更材束条析架栏样核桌桩求汇没流浏混滚火然状独环球界留皮眼着睛础示种站章糊系素索细经统缘网者胯脚腿范蒙表装西规览解触议试调资赖跟跨躯躲车轮软轴较边近追逻那配钮链问阅静靠音页顶项须题颜额首验骨骼黏黑"

