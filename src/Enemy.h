#pragma once
using namespace std;
#include "Entity.h"
#include "raylib.h"

// 管理器使用后台线程更新敌人，因此需要互斥量、条件变量和原子状态
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// Maze 只在声明中使用引用/指针，放到 Enemy.cpp 中包含即可
class Maze;
// Enemy.cpp 内部定义的组件，这里前置声明，头文件只保存指针
class HealthComponent;
class AttackComponent;

//--------------------------------------------------------------------------------

// 敌人模块集中使用的可调参数，敌人尺寸、视野、速度、寻路频率、攻击和自动搜索等行为
namespace EnemyConfig       //命名空间，隔离内部的变量名
{
    // 敌人碰撞盒的尺寸（世界坐标单位）
    constexpr float ENEMY_HEIGHT = 1.6f;
    constexpr float ENEMY_HALF_WIDTH = 0.35f;

    // 巡逻敌人的移动速度和发现玩家所需的视距
    constexpr float PATROL_SPEED = 2.0f;
    constexpr float PATROL_VISION_RANGE = 7.0f; //同时也是下面扫描半径

    // 追击敌人的移动速度和发现玩家所需的视距
    constexpr float CHASER_SPEED = 4.0f;
    constexpr float CHASER_VISION_RANGE = 10.0f;
    constexpr float CHASER_SEARCH_TIME = 6.0f; //追击怪丢失目标后搜索附近的时间

    constexpr float ENEMY_REPATH_INTERVAL = 0.35f;  //两次重新计算路径的最短间隔

    // 敌人->攻击->玩家，碰撞伤害
    constexpr float ENEMY_ATTACK_COOLDOWN = 1.0f;   //攻击冷却
    constexpr int ENEMY_ATTACK_DAMAGE = 10;         //单次伤害
    constexpr float ENEMY_SEARCH_TIME = 3.0f;       //丢失目标后的搜索时间
    constexpr float ENEMY_SEARCH_DWELL_TIME = 0.35f;//搜索时在单个附近点停留观察的时间
    constexpr int ENEMY_SEARCH_RADIUS = 2;          //以最后目击格为中心搜索的网格半径
    constexpr int ENEMY_SEARCH_MAX_TARGETS = 12;    //单次搜索最多保留的候选目标数
    
    constexpr float ENEMY_ARRIVE_DISTANCE = 0.08f;  // 判定“已到达当前路点”的距离阈值，用于消除浮点误差和抖动

    // 敌人最大生命值和复活时间
    constexpr float ENEMY_MAX_HEALTH = 100.0f;
    constexpr float ENEMY_REborn_TIME = 8.0f;

    // 玩家初始生命值
    constexpr int PLAYER_MAX_HEALTH = 100;

    // 玩家->攻击->敌人，发射伤害
    constexpr int ORB_DAMAGE = 25;              //单次伤害
    constexpr float ORB_FIRE_COOLDOWN = 0.5f;   //攻击冷却
    constexpr float ORB_RADIUS = 0.22f;     //光球半径
    constexpr float ORB_SPEED = 12.0f;      //光球飞行速度
    constexpr float ORB_LIFETIME = 1.8f;    //存在时间

    // 玩家攻击次数上限
    constexpr int PLAYER_ATTACK_LIMIT = 10;

    // 敌人碰撞额外间距，防止穿模
    constexpr float ENEMY_COLLISION_PADDING = 0.02f;

    // 初始出生点与玩家起点的硬性最小距离：保证出生瞬间任何敌人都看不到玩家
    // 追击怪视距是 10 m，这里留出余量；候选不足时会自动退回旧的加权行为
    constexpr float ENEMY_SPAWN_MIN_DISTANCE = 16.0f;
    // 出生点与玩家起点至少隔几格（切比雪夫），作为兜底
    constexpr int ENEMY_SPAWN_MIN_CELLS = 2;

    // 第一层：软避让
    // 敌人避让参数
    constexpr float ENEMY_NEIGHBOR_RADIUS = 2.0f;   //需要避让的探测距离，2.0为迷宫宽度
    constexpr float ENEMY_LANE_OFFSET = 0.42f;      //会车时左右偏移距离限制，两个0.42可空出 0.84 > 0.72 敌人(半宽0.35)中心距0.72 = 不碰撞的距离
    constexpr float ENEMY_LANE_FALLBACK_OFFSET = 0.25f; //多个敌人时补充微小偏移，防止普通双向偏移会抵消
    constexpr float ENEMY_PERSONAL_SPACE = 0.95f;   //提前避让的距离，若小于，推开

    // 第二层：硬分离，兜底->避免穿模
    constexpr int ENEMY_SEPARATION_ITERATIONS = 12;     //计算检查，避免穿模，共进行12轮
    constexpr float ENEMY_SEPARATION_MAX_STEP = 0.15f;  //每轮推开0.15，慢慢分离穿模的敌人

    // 卡死看门狗：状态要求移动但连续无位移超过该时间，触发有限范围恢复->就近寻找落脚点
    constexpr float ENEMY_STUCK_TIME = 1.5f;                //若本该移动，却1.5秒未动->卡死
    constexpr float ENEMY_STUCK_SAMPLE_TIME = 0.5f;         //采样时间间隔
    constexpr float ENEMY_STUCK_PROGRESS_DISTANCE = 0.25f;  //净移动距离下限，若低于->没动

    // 巡逻敌人3D下的红色扫描圈
    constexpr int PATROL_SCAN_RAY_COUNT = 96;   //圆心出发的射线数，相邻射线两个终点与圆心构成三角形填充面积
    constexpr float PATROL_SCAN_STEP = 0.08f;   //射线检测步长->准确识别墙边交点
    constexpr float PATROL_SCAN_SPEED = 1.5f;   //扩散速度
    constexpr int PATROL_SCAN_RING_COUNT = 3;   //叠加层数，视觉层次感
    constexpr float PATROL_SCAN_FILL_ALPHA = 0.8f;  //扫描面积的透明度
    constexpr float PATROL_SCAN_MAX_ALPHA = 0.45f;  //墙面投影最大透明度
    constexpr float PATROL_SCAN_MIN_ALPHA = 0.10f;  //墙面投影最小透明度
    constexpr float PATROL_SCAN_FLOOR_LIFT = 0.06f; //扫描面积离地高度->小则与地面穿模，大则像悬浮
    constexpr float PATROL_SCAN_WALL_WIDTH = 0.018f;//墙面投影线宽度
    constexpr float PATROL_SCAN_WALL_HEIGHT = 0.5f;//墙面投影线高度

    // 巡逻敌人在小地图中的雷达圈
    constexpr int PATROL_RADAR_SCAN_SEGMENTS = 64;      //雷达圆周上的分段数
    constexpr float PATROL_RADAR_SCAN_ALPHA = 0.85f;    //雷达圈最大透明度
    constexpr float PATROL_RADAR_SCAN_THICKNESS = 3.0f; //雷达圈线宽
}

//----------------------------------------------------------------------------------------------

// 敌人行为类型：Patrol 往返巡逻；Chaser 在原地等待/追击/搜索
enum class EnemyType
{
    Patrol,
    Chaser
};

// 敌人状态机的状态：
// 巡逻敌人使用 Patrol/Chase/Search/Return/Attack
// 追击敌人使用 Idle/Chase/Search/Attack
enum class EnemyState
{
    Patrol,
    Alert,
    Chase,
    Return,
    Idle,
    Search,
    Attack
};

// 迷宫逻辑坐标，x/y 分别对应网格列和行
struct GridPosition
{
    int x = 0;
    int y = 0;
};

// 重载 "==" 运算符，两个网格坐标相等
inline bool operator==(const GridPosition& a, const GridPosition& b)
{
    return a.x == b.x && a.y == b.y;
}
// 重载 "!=" 运算符，两个网格坐标不相等
inline bool operator!=(const GridPosition& a, const GridPosition& b)
{
    return !(a == b);
}
// 内联：把函数代码直接嵌入调用处，省去函数跳转调用，提速

//----------------------------------------------------------------------

// 主线程在每帧把玩家当前状态复制给后台敌人线程，避免直接跨线程访问
struct PlayerSnapshot
{
    // 玩家的世界坐标，用于距离判断和网格换算
    Vector3 feetPosition{ 0.0f, 0.0f, 0.0f };
    // 玩家当前碰撞盒，用于敌人接触攻击判定
    BoundingBox bounds{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    // 玩家是否仍存活；死亡后敌人不再攻击或追踪
    bool alive = true;
};

// 每次更新敌人时传入的信息
struct EnemyUpdateContext
{
    // 用于网格/世界坐标转换和阻挡检测
    const Maze* maze = nullptr;
    // 共享的路径规划器，敌人借此寻路和视线检测
    const class Pathfinder* pathfinder = nullptr;
    // 本帧玩家快照，查看玩家位置、存活状态
    PlayerSnapshot player;
};

// 敌人攻击成功后生成的伤害事件，由主线程消费并扣减玩家生命值
struct DamageEvent
{
    // 本次造成的伤害值
    int amount = 0;
    // 攻击来源位置，目前用于扩展伤害方向/特效
    Vector3 source{ 0.0f, 0.0f, 0.0f };
};

// 脚步声事件：后台线程只记录“谁在什么位置走了一步”，主线程负责播放音频。
struct FootstepEvent
{
    // 敌人种类，用于选择巡逻或追击脚步样本。
    EnemyType type = EnemyType::Patrol;
    // 产生脚步时的世界坐标。
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    // 当前移动速度，供后续扩展轻重脚步声或额外音量。
    float speed = 0.0f;
};

// 后台线程交给主线程渲染的敌人快照
struct EnemyRenderState
{
    // 敌人种类、当前行为和世界坐标
    EnemyType type = EnemyType::Patrol;
    EnemyState state = EnemyState::Patrol;
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    // 当前包围盒，渲染大小和调试碰撞都使用它
    BoundingBox bounds{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    // 当前生命值和是否仍然存活
    float health = 0.0f;
    bool active = false;
};

// 给主线程渲染的光球快照
struct OrbRenderState
{
    Vector3 position{ 0.0f, 0.0f, 0.0f };
    float radius = EnemyConfig::ORB_RADIUS;
};

//------------------------------------------------------------------------

// 基于迷宫网格的 A* 路径规划器，并提供敌人到玩家之间的视线检测
class Pathfinder
{
public:
    explicit Pathfinder(const Maze& maze);

    // 计算从 start 到 goal 的可行路径
    vector<GridPosition> FindPath(
        GridPosition start,
        GridPosition goal
    ) const;

    // 检查两个世界坐标之间是否没有墙体阻挡
    // 返回true、false
    bool HasLineOfSight(Vector3 from, Vector3 to) const;

private:
    // 外部迷宫的生命周期必须覆盖 Pathfinder
    const Maze& maze_;  //只读不写外部迷宫，且同一个，不创建副本
};

//--------------------------------------------------------------------

// 所有敌人的抽象基类：位置、碰撞盒、生命值、攻击冷却和路径
class Enemy : public Entity
{
public:
    // 创建一个指定类型的敌人
    Enemy(EnemyType type, GridPosition bornGrid, Vector3 bornPosition);
    ~Enemy() override = default;    //override表示重写父类Entity的虚函数

    // 两种敌人之间独立管理，禁止复制同一套指针，避免组件被共用或重复释放内存
    Enemy(const Enemy&) = delete;
    Enemy& operator=(const Enemy&) = delete;

    // 每帧更新敌人行为
    // context 本帧信息：迷宫、寻路器和玩家快照
    // dt 距离上一帧的时间，单位秒
    // damageEvents 本帧产生的攻击伤害输出队列
    virtual void Update(const EnemyUpdateContext& context,float dt,
        vector<DamageEvent>& damageEvents) = 0;    //纯虚函数

    // 获取当前状态的名称，返回Patrol/Chase/Search/Return/Idle
    virtual const char* GetStateName() const = 0;

    // 生成给主线程渲染的只读快照
    virtual EnemyRenderState GetRenderState() const;

    // 获取敌人的包围盒，供碰撞检测和绘制使用
    BoundingBox GetBoundingBox() const override;
    // 获取敌人种类
    EnemyType GetType() const;
    // 获取敌人网格坐标
    GridPosition GetGridPosition() const;
    // 获取敌人世界坐标
    Vector3 GetPosition() const;
    // 获取敌人生命值
    float GetHealth() const;

    // 设置敌人世界坐标
    void SetPosition(Vector3 position);
    // 不止设置世界坐标，还更新当前网格坐标->复活
    void SetPosition(Vector3 position, const Maze& maze);
    // 写入本帧局部避让提示：offset 是世界坐标侧向偏移，speedScale 是速度倍率。
    void SetSteeringHint(Vector3 offset, float speedScale);
    // 碰撞分离显著推开后作废旧路径，避免继续追逐已经过期的路点。
    void InvalidatePath();
    // 获取当前路径的下一个路点；没有路径时返回 false。
    bool GetNextWaypoint(GridPosition& waypoint) const;
    // 敌人受到伤害，生命值归零后自动停用实体
    void ApplyDamage(int amount);

protected:
    // 从当前位置重新计算到 goal 的路径，并重置路径
    void Repath(const EnemyUpdateContext& context, GridPosition goal);

    // 沿当前路径向下一格移动，路径已走完时返回 true
    bool FollowPath(const Maze& maze, float dt, float speed);

    // 在给定视距内判断玩家是否可见（包含距离和墙体视线检测）
    bool CanSeePlayer(const EnemyUpdateContext& context, float range) const;

    // 判断敌人包围盒是否与玩家包围盒接触
    bool IsTouchingPlayer(const EnemyUpdateContext& context) const;

    // 判断是否碰到玩家、冷却结束->产生伤害事件
    bool TryAttack(const EnemyUpdateContext& context,
        vector<DamageEvent>& damageEvents);

    // 敌人的不可变类型、出生格和当前格
    EnemyType type_;
    GridPosition bornGrid_;
    GridPosition currentGrid_;

    // 敌人脚底位置以及当前路径、当前正处在路径的第几个点
    Vector3 position_{ 0.0f, 0.0f, 0.0f };
    vector<GridPosition> path_;
    size_t pathIndex_ = 0;

    // 管理器每帧写入的转向提示；只影响 FollowPath 的靠近方向，不参与 A* 寻路。
    Vector3 steeringOffset_{ 0.0f, 0.0f, 0.0f };
    float steeringSpeedScale_ = 1.0f;

    // Entity 组件指针
    HealthComponent* healthComponent_ = nullptr;
    AttackComponent* attackComponent_ = nullptr;

private:
    // 根据世界坐标刷新当前网格坐标
    void UpdateCurrentGrid(const Maze& maze);
};

//---------------------------------------------------------------------

// 巡逻敌人：在出生点与自动选择的远端巡逻点之间往返
// 发现玩家后追击，丢失目标后搜索，再返回巡逻路线
class PatrolEnemy : public Enemy
{
public:
    PatrolEnemy(
        GridPosition bornGrid,
        Vector3 bornPosition,
        GridPosition patrolTarget   //自主选择的另一端巡逻点
    );

    // 更新巡逻敌人的信息、状态和移动（重写父类虚函数）
    void Update(const EnemyUpdateContext& context, float dt,
        vector<DamageEvent>& damageEvents) override;
    const char* GetStateName() const override;
    EnemyRenderState GetRenderState() const override;

private:
    // 各状态下的移动逻辑
    void UpdatePatrol(const EnemyUpdateContext& context, float dt);
    void UpdateChase(const EnemyUpdateContext& context, float dt);
    void UpdateSearch(const EnemyUpdateContext& context, float dt);
    void UpdateReturn(const EnemyUpdateContext& context, float dt);

    // 刷新到 goal 的路径
    void EnsurePath(const EnemyUpdateContext& context, GridPosition goal, float dt);

    GridPosition patrolTarget_; //巡逻端点
    GridPosition lastSeenGrid_; //最后发现玩家的格子
    EnemyState state_ = EnemyState::Patrol;
    float repathTimer_ = 0.0f;  //重新寻路的计时器
    float searchTimer_ = 0.0f;  //跟丢玩家后的搜索计时器
};

//--------------------------------------------------------------------------

// 追击敌人：空闲时原地等待，发现玩家后快速追击；丢失目标后搜索
// 搜索结束后回到空闲，等待下一次发现玩家
class ChaserEnemy : public Enemy
{
public:
    ChaserEnemy(GridPosition bornGrid, Vector3 bornPosition);

    // 更新追击敌人
    void Update(const EnemyUpdateContext& context, float dt,
        vector<DamageEvent>& damageEvents) override;
    const char* GetStateName() const override;
    EnemyRenderState GetRenderState() const override;

private:
    // 各状态下的移动逻辑
    void UpdateIdle(const EnemyUpdateContext& context, float dt);
    void UpdateChase(const EnemyUpdateContext& context, float dt);
    void UpdateSearch(const EnemyUpdateContext& context, float dt);
    void UpdateReturn(const EnemyUpdateContext& context, float dt);

    // 进入搜索：围绕最后目击格生成附近可走点。
    void BeginSearch(const EnemyUpdateContext& context);
    // 按由近到远收集搜索半径内的可走格，避免只盯着一个格子。
    void BuildSearchTargets(
        const EnemyUpdateContext& context,
        GridPosition center
    );

    // 刷新到 goal 的路径
    void EnsurePath(const EnemyUpdateContext& context, GridPosition goal, float dt);

    GridPosition lastSeenGrid_; //最后发现玩家的格子
    EnemyState state_ = EnemyState::Idle;
    float repathTimer_ = 0.0f;  //重新寻路的计时器
    float searchTimer_ = 0.0f;  //跟丢玩家后的搜索计时器

    // 搜索时在最后目击点附近轮换目标，避免走到一个格子后立刻回 Idle。
    vector<GridPosition> searchTargets_;
    size_t searchTargetIndex_ = 0;
    float searchDwellTimer_ = 0.0f;
};

//-------------------------------------------------------------------------------------------------

// 敌人工厂：把 “类型 + 迷宫 + 出生格” 转换成具体敌人对象
class EnemyFactory
{
public:
    //创建敌人类型
    static unique_ptr<Enemy> Create(EnemyType type, const Maze& maze, GridPosition bornGrid);

private:
    // 选出离出生点曼哈顿距离最远的巡逻目标，即直线最远
    static GridPosition FindPatrolTarget(const Maze& maze, GridPosition bornGrid);
};

//------------------------------------------------------------------------------------------------------------------------

// 敌人管理器：拥有所有敌人，使用后台线程固定约 16 ms 的节拍更新，并通过输入/输出快照与主线程交换玩家状态、敌人渲染状态和伤害事件
class EnemyManager
{
public:
    //构造函数，引用外部迷宫，这里explicit表示不能隐式转换，必须明确创建
    explicit EnemyManager(const Maze& maze);
    ~EnemyManager();

    // 禁止复制，否则出现两个管理器同时操作同一批线程和数据，冲突
    EnemyManager(const EnemyManager&) = delete;
    EnemyManager& operator=(const EnemyManager&) = delete;

    // 创建巡逻敌人和追击敌人
    // 参数 player 用于排除玩家所在格并选择远离玩家的出生点
    void BornInitialEnemies(const PlayerSnapshot& player, int count);

    // 在指定网格生成一个敌人
    void Born(EnemyType type, GridPosition bornGrid);

    // 启动/停止后台更新线程；Start 可重复调用，但有running_原子变量防止重复创建线程
    void Start();
    void Stop();

    // 主线程发布最新玩家快照
    void PublishPlayerSnapshot(const PlayerSnapshot& player);

    // 取出并清空后台线程累积的伤害事件，供主线程处理
    vector<DamageEvent> ConsumeDamageEvents();

    // 取出并清空后台线程累积的脚步事件，供主线程播放音频
    vector<FootstepEvent> ConsumeFootstepEvents();

    // 取走并清空本帧被击败的敌人数量，供主线程播放击败音效
    int ConsumeDefeatedEnemyCount();

    // 主线程发出 “玩家发射光球请求”，光球起点+方向
    void FireOrb(Vector3 origin, Vector3 direction);

    // 返回给主线程一份光球快照：位置+半径
    vector<OrbRenderState> GetOrbRenderStates() const;

    // 肉鸽增益「一击必杀」：为 true 时玩家的光球命中即秒杀（不看敌人血量）
    bool orbOneShot = false;

    // 返回给主线程一份敌人快照
    vector<EnemyRenderState> GetRenderStates() const;

private:
    // 从可行走格子中选择一个不与玩家/已选敌人重叠的出生格
    GridPosition PickBornCell(const PlayerSnapshot& player, const vector<GridPosition>& excluded) const;

    // 遍历迷宫内部所有可行走格子，供出生点选择
    vector<GridPosition> GetWalkableCells() const;

    // 后台线程主循环：等待玩家快照->读取玩家位置->处理玩家发射请求->检查是否打中敌人->更新所有敌人->检查敌人之间的碰撞->处理敌人死亡和复活->生成敌人快照->交给主线程
    // 周期：16ms
    void WorkerLoop();

    // 根据每帧真实移动距离产生脚步事件，避免按固定时间播放导致站立时也有脚步声。
    void AccumulateFootstepEvents(float dt, vector<FootstepEvent>& events);

    // 根据邻居关系计算右行、让行和跟随速度，写入每个敌人的转向提示。
    void ComputeEnemySteering(float dt, const PlayerSnapshot& player);

    // 多轮迭代分离敌人包围盒，防止穿模；允许逐帧累积修正，失败时不回退全部位置。
    void ResolveEnemyCollisions(float dt, const PlayerSnapshot& player);

    // 卡死恢复：在有限半径内寻找可行走且不与其他人重叠的落点。
    bool TryRecoverStuckEnemy(size_t index);
    // 管理器持有 迷宫 + 寻路器
    const Maze& maze_;      //引用外部迷宫
    Pathfinder pathfinder_; //创建一个寻路器，并把同一个迷宫交给它使用

    // 敌人的档案，一个槽位放一种敌人
    struct EnemySlot
    {
        EnemyType type = EnemyType::Patrol;
        GridPosition bornGrid{};
        unique_ptr<Enemy> enemy;
        float rebornTimer = 0.0f;
        // 局部避让提示和速度倍率，由 ComputeEnemySteering 每帧写入。
        Vector3 steeringOffset{ 0.0f, 0.0f, 0.0f };
        float steeringSpeedScale = 1.0f;
        // 卡死看门狗计时、侧向偏移被墙挡住后的翻面计时。
        float stuckTimer = 0.0f;
        float blockedSteerTimer = 0.0f;
        // 按固定间隔比较净位移，来回抖动不会重置卡死计时。
        Vector3 stuckAnchor{ 0.0f, 0.0f, 0.0f };
        float stuckSampleTimer = 0.0f;
        bool stuckAnchorValid = false;
        // 脚步节拍：记录上一次脚步参考位置和累计移动距离。
        Vector3 lastFootstepPosition{ 0.0f, 0.0f, 0.0f };
        float footstepDistance = 0.0f;
        bool footstepPositionValid = false;
    };
    // 主线程的光球发射请求
    struct OrbFireRequest
    {
        Vector3 origin{ 0.0f, 0.0f, 0.0f };
        Vector3 direction{ 0.0f, 0.0f, 0.0f };
    };
    // 光球真实数据，不是光球快照！
    struct OrbState
    {
        Vector3 position{ 0.0f, 0.0f, 0.0f };           //位置
        Vector3 velocity{ 0.0f, 0.0f, 0.0f };           //飞行速度
        float radius = EnemyConfig::ORB_RADIUS;         //半径
        float remainingLife = EnemyConfig::ORB_LIFETIME;//存在时间
    };

    // vector容器，动态数组，存储多个敌人的档案
    vector<EnemySlot> enemies_;
    // 互斥锁：防止多线程同时操作
    // 输出数据锁
    mutable mutex outputMutex_;
    // 保护三个输出容器
    vector<EnemyRenderState> renderStates_;
    vector<OrbRenderState> orbRenderStates_;
    vector<DamageEvent> damageEvents_;
    vector<FootstepEvent> footstepEvents_;
    int defeatedEnemyCount_ = 0;

    // 输入数据锁，主线程->输入玩家快照->唤醒后台线程
    mutex inputMutex_;
    condition_variable condition_;

    thread worker_;                     //后台线程
    atomic<bool> running_{ false };     //原子标志：告诉后台线程什么时候启动/退出

#ifdef MAZE_WEB
    // Web 版没有后台线程：WorkerLoop() 由主线程每帧同步调一次。
    // 上一拍的时间必须跨调用记住，否则每帧算出来的 dt 都接近 0，敌人根本不动。
    chrono::steady_clock::time_point lastTick_{};
#endif

    // 等待后台处理玩家攻击请求
    vector<OrbFireRequest> pendingFireRequests_;
    // 主线程最新发布的玩家快照
    PlayerSnapshot playerSnapshot_;
    // 标志：是否有新的玩家快照，及时更新
    bool playerSnapshotUpdated_ = false;

    vector<OrbState> orbs_;     //后台正在运行的所有光球
    float fireCooldown_ = 0.0f; //光球发射冷却
};
