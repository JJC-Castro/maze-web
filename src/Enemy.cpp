#include "Enemy.h"
#include "Config.h"
#include "Maze.h"
#include "FootstepAudio.h"
#include "raymath.h"

// 容器/算法、计时、数学、优先队列等
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

// 提前声明：FollowPath 需要在移动前检查候选位置是否完整落在可行走格内。
static bool EnemyPositionWalkable(const Maze& maze, Vector3 position);

// 敌人生命组件
class HealthComponent final : public Component
{
public:
    // maxHealth 敌人初始生命值，同时作为上限
    explicit HealthComponent(float maxHealth): maxHealth_(maxHealth), health_(maxHealth){}

    // 组件框架要求的更新接口；生命组件不需要逐帧主动更新
    void Update(Entity&, float) override{}

    // 扣减生命值，amount 伤害值
    void ApplyDamage(int amount)
    {
        if (amount <= 0 || health_ <= 0.0f)
        {
            return;
        }
        health_ = max(0.0f, health_ - static_cast<float>(amount));
    }

    // 获取当前生命值
    float GetHealth() const
    {
        return health_;
    }

    // 当前生命值大于 0 时视为存活
    bool IsAlive() const
    {
        return health_ > 0.0f;
    }

private:
    float maxHealth_ = 100.0f;
    float health_ = 100.0f;
};

//----------------------------------------------------------------------------------

// 敌人攻击组件：管理攻击冷却和单次攻击伤害
class AttackComponent final : public Component
{
public:
    // cooldown 两次攻击之间的冷却，单位秒
    // damage 每次攻击产生的伤害值
    AttackComponent(float cooldown, int damage) : cooldownInterval_(cooldown)
        , cooldownRemaining_(0.0f), damage_(damage){}

    // 更新递减->剩余冷却时间
    void Update(Entity&, float dt) override
    {
        cooldownRemaining_ = max(0.0f, cooldownRemaining_ - dt);
    }

    // 冷却结束后允许攻击
    bool CanAttack() const
    {
        return cooldownRemaining_ <= 0.0f;
    }

    // 触发一次攻击，重新开始冷却计时
    void Trigger()
    {
        cooldownRemaining_ = cooldownInterval_;
    }

    // 获取单次攻击伤害
    int GetDamage() const
    {
        return damage_;
    }

private:
    float cooldownInterval_ = 1.0f;
    float cooldownRemaining_ = 0.0f;
    int damage_ = 10;
};

//------------------------------------------------------------------------------------------

// 路径规划期间只读迷宫
Pathfinder::Pathfinder(const Maze& maze) : maze_(maze){}

// 使用四方向 A* 在迷宫网格中寻找最短路径
vector<GridPosition> Pathfinder::FindPath(GridPosition start, GridPosition goal) const
{
    vector<GridPosition> path;

    // 起终点越界或不可行走时->返回空路径
    if (!maze_.grid.isInBounds(start.x, start.y) || !maze_.grid.isInBounds(goal.x, goal.y) 
        || !maze_.canWalk(start.x, start.y) || !maze_.canWalk(goal.x, goal.y))
    {
        return path;
    }

    // 当前位置已在目标点->返回空路径
    if (start == goal)
    {
        return path;
    }

    const int width = maze_.grid.width;
    const int cellCount = width * maze_.grid.height;    //表示整个迷宫的总格数
    //初始所有格子都是无穷大，因为还不知道怎么到达这些格子
    constexpr float infinity = numeric_limits<float>::infinity();

    // A* 数据：gScore 为起点到节点的代价，previous 保存父节点，
    // closed 标记已确定最短距离的节点
    vector<float> gScore(cellCount, infinity);  //从起点走到某格子的代价
    vector<int> previous(cellCount, -1);    //每个格子是从哪个格子过来的，最后找到终点就靠它推回起点
    vector<bool> closed(cellCount, false);  //标志->格子是否被处理过

    // 小顶堆优先队列按 fScore 从小到大取节点
    priority_queue<
        pair<float, int>,
        vector<pair<float, int>>,
        greater<pair<float, int>>   //小顶堆取出fScore最小的格子
    > open;

    // 二维迷宫->一维数组
    auto indexOf = [width](GridPosition position)
    {
        // index = y * width +x
        return position.y * width + position.x;
    };

    // 启发函数，返回曼哈顿距离
    auto heuristic = [](GridPosition a, GridPosition b)
    {
        return static_cast<float>(abs(a.x - b.x) + abs(a.y - b.y));
    };

    const int startIndex = indexOf(start);
    const int goalIndex = indexOf(goal);
    gScore[startIndex] = 0.0f;      //起点到自己的代价为0
    open.push({ heuristic(start, goal), startIndex });

    // 上下左右
    const int directions[4][2] =
    {
        { 1, 0 },
        { -1, 0 },
        { 0, 1 },
        { 0, -1 }
    };

    // A* 主循环：每次取出当前 fScore 最小的节点
    while (!open.empty())
    {
        // 取出fScore最小
        const int currentIndex = open.top().second;
        open.pop();

        // 跳过重复的格子
        if (closed[currentIndex])
        {
            continue;
        }
        // 标记已处理
        closed[currentIndex] = true;

        // 到达终点
        if (currentIndex == goalIndex)break;

        // 一维索引->网格坐标
        const GridPosition current
        {
            currentIndex % width,   //x
            currentIndex / width    //y
        };

        for (const auto& direction : directions)
        {
            const GridPosition next
            {
                current.x + direction[0],
                current.y + direction[1]
            };

            // 跳过不能走的邻格，墙体、越界
            if (!maze_.canWalk(next.x, next.y))continue;

            // 若已被处理，跳出当前循环
            const int nextIndex = indexOf(next);
            if (closed[nextIndex])continue;

            // 四方向移动，单步代价固定为 1
            const float tentativeScore = gScore[currentIndex] + 1.0f;

            // 如果新路线比原来长，不更新
            if (tentativeScore >= gScore[nextIndex])continue;

            //如果更短，记录父节点并更新代价
            previous[nextIndex] = currentIndex;
            gScore[nextIndex] = tentativeScore;
            // 入队值为 f，g->tentativeScore，h->heuristic
            open.push({ tentativeScore + heuristic(next, goal), nextIndex });
        }
    }

    // 目标点没有父节点说明还未达到目标，父节点即上一格子
    if (previous[goalIndex] == -1)
    {
        return path;
    }

    // 从终点回溯到起点，得到反向路径
    for (int current = goalIndex; current != startIndex; current = previous[current])
    {
        path.push_back({ current % width, current / width });
    }

    // 反转后即从起点下一步开始到终点的路径
    reverse(path.begin(), path.end());
    return path;
}

//------------------------------------------------------------------------------------------

// 通过在两点之间均匀采样检测墙体；高度统一取地面高度，忽略垂直遮挡。
bool Pathfinder::HasLineOfSight(Vector3 from, Vector3 to) const
{
    const float dx = to.x - from.x;
    const float dz = to.z - from.z;
    const float distance = sqrt(dx * dx + dz * dz);

    if (distance <= EnemyConfig::ENEMY_ARRIVE_DISTANCE)
    {
        return true;
    }

    // 每约 0.25 个世界单位采样一次，保证不会跨过薄墙
    const int steps = max(1, static_cast<int>(ceil(distance / 0.25f)));

    // 只检查中间采样点；起点和终点本身允许位于可走格内。
    for (int step = 1; step < steps; ++step)
    {
        const float t = static_cast<float>(step) / static_cast<float>(steps);
        const Vector3 sample
        {
            from.x + dx * t,
            0.0f,
            from.z + dz * t
        };

        if (maze_.IsWorldPosBlocked(sample))
        {
            return false;
        }
    }

    return true;
}

// 初始化敌人基础数据和组件。具体类型的行为由 PatrolEnemy/ChaserEnemy 更新。
Enemy::Enemy(
    EnemyType type,
    GridPosition bornGrid,
    Vector3 bornPosition
)
    : type_(type)
    , bornGrid_(bornGrid)
    , currentGrid_(bornGrid)
    , position_(bornPosition)
{
    // 生命组件负责承伤，攻击组件负责冷却和伤害数值。
    healthComponent_ = AddComponent<HealthComponent>(EnemyConfig::ENEMY_MAX_HEALTH);
    attackComponent_ = AddComponent<AttackComponent>(
        EnemyConfig::ENEMY_ATTACK_COOLDOWN,
        EnemyConfig::ENEMY_ATTACK_DAMAGE
    );
}

// 生成基础渲染快照；子类会补充具体状态。
EnemyRenderState Enemy::GetRenderState() const
{
    EnemyRenderState state;
    state.type = type_;
    state.position = position_;
    state.bounds = GetBoundingBox();
    state.health = GetHealth();
    state.active = IsActive();
    return state;
}

// 返回敌人种类（Patrol 或 Chaser）。
EnemyType Enemy::GetType() const
{
    return type_;
}

// 返回最近一次同步后的网格坐标。
GridPosition Enemy::GetGridPosition() const
{
    return currentGrid_;
}

// 返回敌人脚底的世界坐标；y 坐标通常保持在地面高度。
Vector3 Enemy::GetPosition() const
{
    return position_;
}

// 以脚底位置为中心构造敌人碰撞盒，向上延伸 ENEMY_HEIGHT。
BoundingBox Enemy::GetBoundingBox() const
{
    return BoundingBox
    {
        {
            position_.x - EnemyConfig::ENEMY_HALF_WIDTH,
            GROUND_Y,
            position_.z - EnemyConfig::ENEMY_HALF_WIDTH
        },
        {
            position_.x + EnemyConfig::ENEMY_HALF_WIDTH,
            GROUND_Y + EnemyConfig::ENEMY_HEIGHT,
            position_.z + EnemyConfig::ENEMY_HALF_WIDTH
        }
    };
}

// 从生命组件读取生命值；组件缺失时安全地返回 0。
float Enemy::GetHealth() const
{
    return healthComponent_ != nullptr
        ? healthComponent_->GetHealth()
        : 0.0f;
}

// 直接更新世界坐标。路径移动会持续调用此函数。
void Enemy::SetPosition(Vector3 position)
{
    position_ = position;
}

void Enemy::SetPosition(Vector3 position, const Maze& maze)
{
    position_ = position;
    UpdateCurrentGrid(maze);
}

// 写入本帧避让提示；偏移长度限制在配置值内，防止多个邻居叠加后偏移过大。
void Enemy::SetSteeringHint(Vector3 offset, float speedScale)
{
    const float length = sqrt(offset.x * offset.x + offset.z * offset.z);
    if (length > EnemyConfig::ENEMY_LANE_OFFSET && length > 1e-5f)
    {
        const float scale = EnemyConfig::ENEMY_LANE_OFFSET / length;
        offset.x *= scale;
        offset.z *= scale;
    }

    // 轻微平滑，避免邻居关系变化时车道左右抖动。
    const float blend = 0.5f;
    steeringOffset_.x = steeringOffset_.x * (1.0f - blend) + offset.x * blend;
    steeringOffset_.z = steeringOffset_.z * (1.0f - blend) + offset.z * blend;
    steeringSpeedScale_ = max(0.0f, min(speedScale, 1.0f));
}

// 碰撞分离可能把敌人推离原路点，作废旧路径让下一次 EnsurePath 重新规划。
void Enemy::InvalidatePath()
{
    path_.clear();
    pathIndex_ = 0;
}

// 返回当前路径的下一个路点，供管理器计算朝向和会车方向。
bool Enemy::GetNextWaypoint(GridPosition& waypoint) const
{
    if (pathIndex_ >= path_.size())
    {
        return false;
    }

    waypoint = path_[pathIndex_];
    return true;
}

// 对存活敌人扣血；生命值归零后设置 inactive，后续更新和伤害都会跳过。
void Enemy::ApplyDamage(int amount)
{
    if (!IsActive() || healthComponent_ == nullptr)
    {
        return;
    }

    healthComponent_->ApplyDamage(amount);
    if (!healthComponent_->IsAlive())
    {
        SetActive(false);
    }
}

// 清空旧路径并从当前网格重新寻路到目标。
void Enemy::Repath(
    const EnemyUpdateContext& context,
    GridPosition goal
)
{
    path_.clear();
    pathIndex_ = 0;

    // 上下文缺少依赖时保持无路径状态，避免解引用空指针。
    if (context.maze == nullptr || context.pathfinder == nullptr)
    {
        return;
    }

    UpdateCurrentGrid(*context.maze);
    path_ = context.pathfinder->FindPath(currentGrid_, goal);
}

// 沿 path_ 前进一帧，返回是否已经完成整条路径。
bool Enemy::FollowPath(const Maze& maze, float dt, float speed)
{
    // 当前没有剩余路点，视为已到达终点。
    if (pathIndex_ >= path_.size())
    {
        return true;
    }

    const Vector3 target =
        maze.GridToWorld(path_[pathIndex_].x, path_[pathIndex_].y);

    auto advanceWaypoint = [&](Vector3 newPosition)
    {
        if (EnemyPositionWalkable(maze, newPosition))
        {
            position_ = newPosition;
        }

        ++pathIndex_;
        UpdateCurrentGrid(maze);

        // 最后一个路点处理完后清空路径，便于下一轮重新规划。
        if (pathIndex_ >= path_.size())
        {
            path_.clear();
            pathIndex_ = 0;
            return true;
        }

        return false;
    };

    const float baseDx = target.x - position_.x;
    const float baseDz = target.z - position_.z;
    const float baseDistance = sqrt(baseDx * baseDx + baseDz * baseDz);
    if (baseDistance <= EnemyConfig::ENEMY_ARRIVE_DISTANCE)
    {
        return advanceWaypoint(target);
    }

    // 避让提示只取垂直于前进方向的分量，避免侧移提示改变实际到点距离。
    Vector3 lateralOffset = steeringOffset_;
    const float dirX = baseDx / baseDistance;
    const float dirZ = baseDz / baseDistance;
    const float forwardAmount =
        lateralOffset.x * dirX + lateralOffset.z * dirZ;
    lateralOffset.x -= dirX * forwardAmount;
    lateralOffset.z -= dirZ * forwardAmount;

    const Vector3 steeredTarget = {
        target.x + lateralOffset.x,
        0.0f,
        target.z + lateralOffset.z
    };

    const float dx = steeredTarget.x - position_.x;
    const float dz = steeredTarget.z - position_.z;
    const float distance = sqrt(dx * dx + dz * dz);
    if (distance <= EnemyConfig::ENEMY_ARRIVE_DISTANCE)
    {
        return advanceWaypoint(steeredTarget);
    }

    // 速度倍率用于队列跟车和让行减速，避免后车持续顶住前车。
    const float scale = max(0.0f, min(steeringSpeedScale_, 1.0f));
    // 减速带：敌人经过时速度同样按难度倍率下降
    const float zoneFactor = maze.SlowFactorAt(position_);
    const float step = min(speed * scale * zoneFactor * dt, distance);
    const Vector3 candidate = {
        position_.x + dx / distance * step,
        0.0f,
        position_.z + dz / distance * step
    };

    // 侧移后如果候选点擦到墙，就分别尝试只走 x 或只走 z，保留沿墙滑动的能力。
    if (EnemyPositionWalkable(maze, candidate))
    {
        position_ = candidate;
    }
    else
    {
        const Vector3 xOnly = { candidate.x, 0.0f, position_.z };
        const Vector3 zOnly = { position_.x, 0.0f, candidate.z };
        if (EnemyPositionWalkable(maze, xOnly))
        {
            position_ = xOnly;
        }
        else if (EnemyPositionWalkable(maze, zOnly))
        {
            position_ = zOnly;
        }
        // 两个方向都被墙挡住时保持原位，由管理器检测卡死并触发恢复。
    }

    UpdateCurrentGrid(maze);
    return false;
}

// 判断玩家是否处于视距内且中间没有墙；只比较水平距离。
bool Enemy::CanSeePlayer(
    const EnemyUpdateContext& context,
    float range
) const
{
    // 玩家死亡或寻路器缺失时不可见。
    if (!context.player.alive || context.pathfinder == nullptr)
    {
        return false;
    }

    const float dx = context.player.feetPosition.x - position_.x;
    const float dz = context.player.feetPosition.z - position_.z;
    const float distanceSquared = dx * dx + dz * dz;

    // 先用距离平方过滤，避免开方；超出 range 后再做视线采样。
    if (distanceSquared > range * range)
    {
        return false;
    }

    return context.pathfinder->HasLineOfSight(
        position_,
        context.player.feetPosition
    );
}

// 判断两个包围盒是否相交，用于接触攻击触发条件。
bool Enemy::IsTouchingPlayer(const EnemyUpdateContext& context) const
{
    return context.player.alive &&
        CheckCollisionBoxes(GetBoundingBox(), context.player.bounds);
}

// 在接触玩家且攻击组件冷却完成时生成伤害事件。
bool Enemy::TryAttack(
    const EnemyUpdateContext& context,
    vector<DamageEvent>& damageEvents
)
{
    if (!IsTouchingPlayer(context) ||
        attackComponent_ == nullptr ||
        !attackComponent_->CanAttack())
    {
        return false;
    }

    // 伤害事件发送给主线程统一处理玩家扣血，避免后台线程直接修改玩家。
    damageEvents.push_back(
        { attackComponent_->GetDamage(), position_ }
    );
    attackComponent_->Trigger();
    return true;
}

// 把敌人脚底世界坐标转换为当前网格坐标。
void Enemy::UpdateCurrentGrid(const Maze& maze)
{
    maze.WorldToGrid(position_, currentGrid_.x, currentGrid_.y);
}

// 巡逻敌人初始化两个往返端点；首次寻路会在 UpdatePatrol 中完成。
PatrolEnemy::PatrolEnemy(
    GridPosition bornGrid,
    Vector3 bornPosition,
    GridPosition patrolTarget
)
    : Enemy(EnemyType::Patrol, bornGrid, bornPosition)
    , patrolTarget_(patrolTarget)
    , lastSeenGrid_(bornGrid)
{
}

// 巡逻敌人的每帧主状态机。
void PatrolEnemy::Update(
    const EnemyUpdateContext& context,
    float dt,
    vector<DamageEvent>& damageEvents
)
{
    // 先推进生命/攻击等组件计时器。
    UpdateComponents(dt);

    // 死亡或上下文无效时停止行为更新。
    if (!IsActive() || context.maze == nullptr || context.pathfinder == nullptr)
    {
        return;
    }

    // 接触玩家优先进入攻击状态；攻击冷却由 TryAttack 统一检查。
    if (IsTouchingPlayer(context))
    {
        state_ = EnemyState::Attack;
        TryAttack(context, damageEvents);
        return;
    }

    // 能看见玩家时记录玩家所在格并进入追击。
    if (CanSeePlayer(context, EnemyConfig::PATROL_VISION_RANGE))
    {
        state_ = EnemyState::Chase;
        context.maze->WorldToGrid(
            context.player.feetPosition,
            lastSeenGrid_.x,
            lastSeenGrid_.y
        );
    }
    // 上一次攻击后玩家已离开接触范围，改回追击而不是立即巡逻。
    else if (state_ == EnemyState::Attack)
    {
        state_ = EnemyState::Chase;
    }

    // 根据状态执行对应行为。
    switch (state_)
    {
    case EnemyState::Patrol:
        UpdatePatrol(context, dt);
        break;
    case EnemyState::Chase:
    case EnemyState::Attack:
        UpdateChase(context, dt);
        break;
    case EnemyState::Search:
        UpdateSearch(context, dt);
        break;
    case EnemyState::Return:
        UpdateReturn(context, dt);
        break;
    case EnemyState::Alert:
    case EnemyState::Idle:
    default:
        state_ = EnemyState::Patrol;
        UpdatePatrol(context, dt);
        break;
    }
}

// 返回巡逻敌人当前状态的可读名称。
const char* PatrolEnemy::GetStateName() const
{
    switch (state_)
    {
    case EnemyState::Patrol: return "Patrol";
    case EnemyState::Alert: return "Alert";
    case EnemyState::Chase: return "Chase";
    case EnemyState::Return: return "Return";
    case EnemyState::Search: return "Search";
    case EnemyState::Attack: return "Attack";
    case EnemyState::Idle: return "Idle";
    default: return "Unknown";
    }
}

// 在基类快照上写入巡逻状态机状态。
EnemyRenderState PatrolEnemy::GetRenderState() const
{
    EnemyRenderState renderState = Enemy::GetRenderState();
    renderState.state = state_;
    return renderState;
}

// 在两个巡逻端点之间移动；到达当前目标后交换两端。
void PatrolEnemy::UpdatePatrol(
    const EnemyUpdateContext& context,
    float dt
)
{
    EnsurePath(context, patrolTarget_, dt);

    // 走完整条路径说明到达巡逻端点，随后改变往返方向。
    if (FollowPath(*context.maze, dt, EnemyConfig::PATROL_SPEED))
    {
        swap(bornGrid_, patrolTarget_);
        repathTimer_ = 0.0f;
    }
}

// 追击玩家；失去视线后转入搜索最近一次发现的网格。
void PatrolEnemy::UpdateChase(
    const EnemyUpdateContext& context,
    float dt
)
{
    // 丢失视线时切换到 Search，并初始化搜索时间和目标。
    if (!CanSeePlayer(context, EnemyConfig::PATROL_VISION_RANGE))
    {
        state_ = EnemyState::Search;
        searchTimer_ = EnemyConfig::ENEMY_SEARCH_TIME;
        repathTimer_ = 0.0f;
        EnsurePath(context, lastSeenGrid_, dt);
        return;
    }

    // 仍能看见玩家时持续刷新最后位置并追踪。
    context.maze->WorldToGrid(
        context.player.feetPosition,
        lastSeenGrid_.x,
        lastSeenGrid_.y
    );
    EnsurePath(context, lastSeenGrid_, dt);
    FollowPath(*context.maze, dt, EnemyConfig::PATROL_SPEED);
}

// 在最后发现玩家的位置附近搜索一段时间。
void PatrolEnemy::UpdateSearch(
    const EnemyUpdateContext& context,
    float dt
)
{
    // 搜索计时耗尽后返回初始巡逻区域。
    searchTimer_ -= dt;
    if (searchTimer_ <= 0.0f)
    {
        state_ = EnemyState::Return;
        repathTimer_ = 0.0f;
        return;
    }

    // 搜索期间继续走向最后发现位置；到达后也提前返回。
    EnsurePath(context, lastSeenGrid_, dt);
    if (FollowPath(*context.maze, dt, EnemyConfig::PATROL_SPEED))
    {
        state_ = EnemyState::Return;
        repathTimer_ = 0.0f;
    }
}

// 返回出生巡逻点；到达后恢复 Patrol。
void PatrolEnemy::UpdateReturn(
    const EnemyUpdateContext& context,
    float dt
)
{
    EnsurePath(context, bornGrid_, dt);

    // 回到出生点后重新开始巡逻循环。
    if (FollowPath(*context.maze, dt, EnemyConfig::PATROL_SPEED))
    {
        state_ = EnemyState::Patrol;
        repathTimer_ = 0.0f;
    }
}

// 保证当前有通往 goal 的有效路径，并按固定间隔刷新路径。
void PatrolEnemy::EnsurePath(
    const EnemyUpdateContext& context,
    GridPosition goal,
    float dt
)
{
    repathTimer_ -= dt;

    // 没有路径或刷新计时到期时重新计算；否则继续沿旧路径移动。
    if (path_.empty() || repathTimer_ <= 0.0f)
    {
        Repath(context, goal);
        repathTimer_ = EnemyConfig::ENEMY_REPATH_INTERVAL;
    }
}

// 追击敌人初始保持 Idle，只有看到玩家后才主动移动。
ChaserEnemy::ChaserEnemy(
    GridPosition bornGrid,
    Vector3 bornPosition
)
    : Enemy(EnemyType::Chaser, bornGrid, bornPosition)
    , lastSeenGrid_(bornGrid)
{
}

// 追击敌人的每帧主状态机。
void ChaserEnemy::Update(
    const EnemyUpdateContext& context,
    float dt,
    vector<DamageEvent>& damageEvents
)
{
    // 推进攻击冷却等组件。
    UpdateComponents(dt);

    // 停用或缺少上下文时不再处理感知和移动。
    if (!IsActive() || context.maze == nullptr || context.pathfinder == nullptr)
    {
        return;
    }

    // 接触玩家时优先攻击，并记录玩家最后出现的格子。
    if (IsTouchingPlayer(context))
    {
        state_ = EnemyState::Attack;
        context.maze->WorldToGrid(
            context.player.feetPosition,
            lastSeenGrid_.x,
            lastSeenGrid_.y
        );
        TryAttack(context, damageEvents);
        return;
    }

    // 发现玩家后从 Idle 进入 Chase，并记录玩家网格位置。
    if (CanSeePlayer(context, EnemyConfig::CHASER_VISION_RANGE))
    {
        state_ = EnemyState::Chase;
        // 重新看到玩家时清掉旧搜索目标，并立即重新规划到玩家的路径。
        searchTargets_.clear();
        searchTargetIndex_ = 0;
        searchDwellTimer_ = 0.0f;
        repathTimer_ = 0.0f;
        context.maze->WorldToGrid(
            context.player.feetPosition,
            lastSeenGrid_.x,
            lastSeenGrid_.y
        );
    }
    // 攻击后玩家离开接触范围，则进入附近搜索而不是原地等待。
    else if (state_ == EnemyState::Attack)
    {
        BeginSearch(context);
    }

    // 执行当前状态对应的行为。
    switch (state_)
    {
    case EnemyState::Chase:
        UpdateChase(context, dt);
        break;
    case EnemyState::Search:
        UpdateSearch(context, dt);
        break;
    case EnemyState::Return:
        UpdateReturn(context, dt);
        break;
    case EnemyState::Idle:
    case EnemyState::Attack:
    default:
        UpdateIdle(context, dt);
        break;
    }
}

// 返回追击敌人当前状态的可读名称。
const char* ChaserEnemy::GetStateName() const
{
    switch (state_)
    {
    case EnemyState::Idle: return "Idle";
    case EnemyState::Search: return "Search";
    case EnemyState::Chase: return "Chase";
    case EnemyState::Attack: return "Attack";
    case EnemyState::Patrol: return "Patrol";
    case EnemyState::Alert: return "Alert";
    case EnemyState::Return: return "Return";
    default: return "Unknown";
    }
}

// 在基类快照上写入追击状态机状态。
EnemyRenderState ChaserEnemy::GetRenderState() const
{
    EnemyRenderState renderState = Enemy::GetRenderState();
    renderState.state = state_;
    return renderState;
}

// 空闲状态不移动，等待下一次看到玩家。
void ChaserEnemy::UpdateIdle(const EnemyUpdateContext&, float)
{
    state_ = EnemyState::Idle;
}

// 追击玩家；丢失视线后搜索最后出现的位置。
void ChaserEnemy::UpdateChase(
    const EnemyUpdateContext& context,
    float dt
)
{
    // 丢失视线时围绕最后目击点建立搜索路线。
    if (!CanSeePlayer(context, EnemyConfig::CHASER_VISION_RANGE))
    {
        BeginSearch(context);
        return;
    }

    // 持续可见时更新最后位置并以更快的速度追踪。
    context.maze->WorldToGrid(
        context.player.feetPosition,
        lastSeenGrid_.x,
        lastSeenGrid_.y
    );
    EnsurePath(context, lastSeenGrid_, dt);
    FollowPath(*context.maze, dt, EnemyConfig::CHASER_SPEED);
}

// 在最后发现玩家的位置附近搜索有限时间。
void ChaserEnemy::UpdateSearch(
    const EnemyUpdateContext& context,
    float dt
)
{
    // 搜索无果后不立即停在任意搜索点，先返回最后目击格再回 Idle。
    searchTimer_ -= dt;
    if (searchTimer_ <= 0.0f || searchTargets_.empty())
    {
        state_ = EnemyState::Return;
        repathTimer_ = 0.0f;
        path_.clear();
        pathIndex_ = 0;
        searchTargets_.clear();
        searchTargetIndex_ = 0;
        searchDwellTimer_ = 0.0f;
        return;
    }

    // 到达一个搜索点后先停留观察一小段时间，再切换到下一个点。
    if (searchDwellTimer_ > 0.0f)
    {
        searchDwellTimer_ -= dt;
        return;
    }

    const GridPosition target = searchTargets_[searchTargetIndex_];
    EnsurePath(context, target, dt);

    // 路径为空有两种情况：已经在目标格，或目标当前不可达。
    // 两种情况都换下一个搜索点，不能像旧逻辑一样直接回 Idle。
    if (path_.empty())
    {
        if (currentGrid_ == target)
        {
            searchDwellTimer_ = EnemyConfig::ENEMY_SEARCH_DWELL_TIME;
        }
        searchTargetIndex_ =
            (searchTargetIndex_ + 1) % searchTargets_.size();
        return;
    }

    if (FollowPath(*context.maze, dt, EnemyConfig::CHASER_SPEED))
    {
        searchDwellTimer_ = EnemyConfig::ENEMY_SEARCH_DWELL_TIME;
        searchTargetIndex_ =
            (searchTargetIndex_ + 1) % searchTargets_.size();
    }
}

// 搜索无果后返回最后目击格，到达后再回到 Idle。
void ChaserEnemy::UpdateReturn(
    const EnemyUpdateContext& context,
    float dt
)
{
    EnsurePath(context, lastSeenGrid_, dt);

    // 已在最后目击格，或该位置当前不可达，都结束搜索。
    if (path_.empty())
    {
        state_ = EnemyState::Idle;
        pathIndex_ = 0;
        return;
    }

    if (FollowPath(*context.maze, dt, EnemyConfig::CHASER_SPEED))
    {
        state_ = EnemyState::Idle;
    }
}

// 进入搜索：围绕最后目击格建立一组由近到远的可走搜索点。
void ChaserEnemy::BeginSearch(const EnemyUpdateContext& context)
{
    state_ = EnemyState::Search;
    searchTimer_ = EnemyConfig::CHASER_SEARCH_TIME;
    repathTimer_ = 0.0f;
    searchDwellTimer_ = 0.0f;
    searchTargetIndex_ = 0;

    // 旧路径属于追击阶段，搜索阶段必须从新的候选点重新规划。
    path_.clear();
    pathIndex_ = 0;

    BuildSearchTargets(context, lastSeenGrid_);
}

// 以 center 为中心按环形向外收集可走格，优先搜索最后目击点附近。
void ChaserEnemy::BuildSearchTargets(
    const EnemyUpdateContext& context,
    GridPosition center
)
{
    searchTargets_.clear();
    if (context.maze == nullptr)
    {
        return;
    }

    for (int ring = 0; ring <= EnemyConfig::ENEMY_SEARCH_RADIUS; ++ring)
    {
        for (int dy = -ring; dy <= ring; ++dy)
        {
            for (int dx = -ring; dx <= ring; ++dx)
            {
                // 只取当前环上的格子，保证按由近到远的顺序加入。
                if (max(abs(dx), abs(dy)) != ring)
                {
                    continue;
                }

                const int gx = center.x + dx;
                const int gy = center.y + dy;
                if (!context.maze->grid.isInBounds(gx, gy) ||
                    !context.maze->canWalk(gx, gy))
                {
                    continue;
                }

                searchTargets_.push_back({ gx, gy });
                if (searchTargets_.size() >=
                    static_cast<size_t>(
                        EnemyConfig::ENEMY_SEARCH_MAX_TARGETS))
                {
                    return;
                }
            }
        }
    }
}

// 保证当前有通往 goal 的有效路径，并按固定间隔刷新路径。
void ChaserEnemy::EnsurePath(
    const EnemyUpdateContext& context,
    GridPosition goal,
    float dt
)
{
    repathTimer_ -= dt;

    // 路径为空或刷新计时到期时重新计算。
    if (path_.empty() || repathTimer_ <= 0.0f)
    {
        Repath(context, goal);
        repathTimer_ = EnemyConfig::ENEMY_REPATH_INTERVAL;
    }
}

// 根据类型创建具体敌人，并统一完成出生点世界坐标换算。
unique_ptr<Enemy> EnemyFactory::Create(
    EnemyType type,
    const Maze& maze,
    GridPosition bornGrid
)
{
    const Vector3 bornPosition =
        maze.GridToWorld(bornGrid.x, bornGrid.y);

    // Chaser 不需要巡逻目标；其余类型按 Patrol 处理。
    if (type == EnemyType::Chaser)
    {
        return unique_ptr<Enemy>(
            new ChaserEnemy(bornGrid, bornPosition)
        );
    }

    return unique_ptr<Enemy>(
        new PatrolEnemy(
            bornGrid,
            bornPosition,
            FindPatrolTarget(maze, bornGrid)
        )
    );
}

// 在迷宫内部格子中寻找距离出生点最远的可行走格子，作为对侧巡逻点。
GridPosition EnemyFactory::FindPatrolTarget(
    const Maze& maze,
    GridPosition bornGrid
)
{
    GridPosition best = bornGrid;
    int bestDistance = -1;

    // 跳过最外层边界，只遍历内部可走区域。
    for (int y = 1; y < maze.grid.height - 1; ++y)
    {
        for (int x = 1; x < maze.grid.width - 1; ++x)
        {
            if (!maze.canWalk(x, y))
            {
                continue;
            }

            const int distance =
                abs(x - bornGrid.x) + abs(y - bornGrid.y);

            // 曼哈顿距离最大者优先；并列时保留先遍历到的格子。
            if (distance > bestDistance)
            {
                bestDistance = distance;
                best = { x, y };
            }
        }
    }

    return best;
}

// 构造管理器；路径规划器绑定到同一个迷宫。
EnemyManager::EnemyManager(const Maze& maze)
    : maze_(maze)
    , pathfinder_(maze)
{
}

// 析构时确保后台线程停止，避免线程访问已销毁成员。
EnemyManager::~EnemyManager()
{
    Stop();
}

// 创建初始敌人：先选巡逻出生点，再从剩余位置选追击出生点。
void EnemyManager::BornInitialEnemies(const PlayerSnapshot& player, int count)
{
    vector<GridPosition> excluded;

    // 至少保证 1 个 Patrol + 1 个 Chaser
    if (count > 0)
    {
        const GridPosition p = PickBornCell(player, excluded);
        Born(EnemyType::Patrol, p);
        excluded.push_back(p);
    }

    if (count > 1)
    {
        const GridPosition c = PickBornCell(player, excluded);
        Born(EnemyType::Chaser, c);
        excluded.push_back(c);
    }

    // 剩下的交替 Patrol / Chaser
    for (int i = 2; i < count; ++i)
    {
        const EnemyType type = (i % 2 == 0) ? EnemyType::Patrol : EnemyType::Chaser;
        const GridPosition b = PickBornCell(player, excluded);
        Born(type, b);
        excluded.push_back(b);
    }
}

// 创建并接管一个敌人对象。
void EnemyManager::Born(EnemyType type, GridPosition bornGrid)
{
    unique_ptr<Enemy> enemy =
        EnemyFactory::Create(type, maze_, bornGrid);

    if (enemy != nullptr)
    {
        EnemySlot slot;
        slot.type = type;
        slot.bornGrid = bornGrid;
        slot.enemy = move(enemy);
        enemies_.push_back(move(slot));
    }
}

// 启动后台更新线程；running_ 的原子交换保证不会重复启动。
void EnemyManager::Start()
{
    if (running_.exchange(true))
    {
        return;
    }

#ifdef MAZE_WEB
    // Web：不创建后台线程，WorkerLoop() 交给 PublishPlayerSnapshot() 每帧同步调用
    lastTick_ = chrono::steady_clock::now();
#else
    worker_ = thread(&EnemyManager::WorkerLoop, this);
#endif
}

// 请求后台线程退出并等待其结束。
void EnemyManager::Stop()
{
    // 已停止时只负责回收可能仍可 join 的线程。
    if (!running_.exchange(false))
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
        return;
    }

    // 唤醒正在等待下一帧的线程，让它及时检查 running_ 并退出。
    condition_.notify_all();

    if (worker_.joinable())
    {
        worker_.join();
    }
}

// 以锁保护的方式发布玩家快照，并唤醒等待中的后台线程。
void EnemyManager::PublishPlayerSnapshot(const PlayerSnapshot& player)
{
    {
        lock_guard<mutex> lock(inputMutex_);
        playerSnapshot_ = player;
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
}

// 原子交换出伤害事件：调用方取得事件，内部队列立即变为空。
vector<DamageEvent> EnemyManager::ConsumeDamageEvents()
{
    lock_guard<mutex> lock(outputMutex_);
    vector<DamageEvent> events;
    events.swap(damageEvents_);
    return events;
}

vector<FootstepEvent> EnemyManager::ConsumeFootstepEvents()
{
    lock_guard<mutex> lock(outputMutex_);
    vector<FootstepEvent> events;
    events.swap(footstepEvents_);
    return events;
}

int EnemyManager::ConsumeDefeatedEnemyCount()
{
    lock_guard<mutex> lock(outputMutex_);
    const int count = defeatedEnemyCount_;
    defeatedEnemyCount_ = 0;
    return count;
}

void EnemyManager::FireOrb(Vector3 origin, Vector3 direction)
{
    if (Vector3Length(direction) < 1e-4f)
    {
        return;
    }

    {
        lock_guard<mutex> lock(inputMutex_);
        if (pendingFireRequests_.size() >= 1)
        {
            pendingFireRequests_.erase(pendingFireRequests_.begin());
        }
        pendingFireRequests_.push_back({ origin, direction });
    }

    condition_.notify_one();
}

// 在输出锁保护下复制渲染快照，避免与后台写入并发。
vector<EnemyRenderState> EnemyManager::GetRenderStates() const
{
    lock_guard<mutex> lock(outputMutex_);
    return renderStates_;
}

vector<OrbRenderState> EnemyManager::GetOrbRenderStates() const
{
    lock_guard<mutex> lock(outputMutex_);
    return orbRenderStates_;
}

// 选择出生格：优先远离玩家，同时尽量与已有敌人拉开距离。
GridPosition EnemyManager::PickBornCell(
    const PlayerSnapshot& player,
    const vector<GridPosition>& excluded
) const
{
    GridPosition playerGrid;
    maze_.WorldToGrid(player.feetPosition, playerGrid.x, playerGrid.y);

    // 默认回退到迷宫入口，保证没有候选时仍返回合法网格。
    GridPosition best = { maze_.entryGx, maze_.entryGy };
    long bestScore = numeric_limits<long>::min();

    // 不满足最小间距时的兜底：行为等同旧版（只按远离玩家 + 分散打分）
    GridPosition fallbackBest = best;
    long fallbackBestScore = numeric_limits<long>::min();

    for (const GridPosition& candidate : GetWalkableCells())
    {
        // 不允许直接出生在玩家所在格。
        if (candidate == playerGrid)
        {
            continue;
        }

        // 排除已经分配给其他敌人的出生格。
        bool isExcluded = false;
        for (const GridPosition& used : excluded)
        {
            if (candidate == used)
            {
                isExcluded = true;
                break;
            }
        }

        if (isExcluded)
        {
            continue;
        }

        // 玩家距离使用平方距离，避免开方且单调性保持一致。
        const long playerDistance =
            static_cast<long>(candidate.x - playerGrid.x) *
            static_cast<long>(candidate.x - playerGrid.x) +
            static_cast<long>(candidate.y - playerGrid.y) *
            static_cast<long>(candidate.y - playerGrid.y);

        // 与已选出生点的平方距离总和作为敌人之间的分离奖励。
        long separationBonus = 0;
        for (const GridPosition& used : excluded)
        {
            separationBonus +=
                static_cast<long>(candidate.x - used.x) *
                static_cast<long>(candidate.x - used.x) +
                static_cast<long>(candidate.y - used.y) *
                static_cast<long>(candidate.y - used.y);
        }

        // 玩家距离权重为 2，使初始生成尽量远离玩家。
        const long score = playerDistance * 2 + separationBonus;
        if (score > fallbackBestScore)
        {
            fallbackBestScore = score;
            fallbackBest = candidate;
        }

        // 硬性最小间距：先隔开格数，再要求直线距离够远，避免一出生就被追击怪看到
        if (abs(candidate.x - playerGrid.x) <= EnemyConfig::ENEMY_SPAWN_MIN_CELLS &&
            abs(candidate.y - playerGrid.y) <= EnemyConfig::ENEMY_SPAWN_MIN_CELLS)
        {
            continue;
        }
        const Vector3 candidateWorld = maze_.GridToWorld(candidate.x, candidate.y);
        const float awayX = candidateWorld.x - player.feetPosition.x;
        const float awayZ = candidateWorld.z - player.feetPosition.z;
        if (awayX * awayX + awayZ * awayZ <
            EnemyConfig::ENEMY_SPAWN_MIN_DISTANCE * EnemyConfig::ENEMY_SPAWN_MIN_DISTANCE)
        {
            continue;
        }

        if (score > bestScore)
        {
            bestScore = score;
            best = candidate;
        }
    }

    // 没有任何候选满足最小间距时，退回旧逻辑选出的点，保证一定能生成
    if (bestScore == numeric_limits<long>::min())
    {
        best = fallbackBest;
    }

    return best;
}

// 收集迷宫内部所有可行走格子，供出生点选择使用。
vector<GridPosition> EnemyManager::GetWalkableCells() const
{
    vector<GridPosition> cells;

    // 最外圈是边界墙/出入口，敌人出生只考虑内部区域。
    for (int y = 1; y < maze_.grid.height - 1; ++y)
    {
        for (int x = 1; x < maze_.grid.width - 1; ++x)
        {
            if (!maze_.canWalk(x, y)) continue;
            // 减速带和地雷虽然可走，但不作为敌人出生点
            const CellType t = maze_.grid.at(x, y);
            if (t == CellType::SLOW_ZONE || t == CellType::MINE) continue;
            cells.push_back({ x, y });
        }
    }

    return cells;
}

// 后台线程主循环：固定约 16 ms 的更新节拍，并同步输入/输出快照。
static bool EnemyBoxesOverlapAt(Vector3 a, Vector3 b)
{
    const float minimumDistance =
        EnemyConfig::ENEMY_HALF_WIDTH * 2.0f +
        EnemyConfig::ENEMY_COLLISION_PADDING;

    return abs(a.x - b.x) < minimumDistance &&
           abs(a.z - b.z) < minimumDistance;
}

static bool EnemyPositionWalkable(const Maze& maze, Vector3 position)
{
    const float halfWidth = EnemyConfig::ENEMY_HALF_WIDTH;
    const Vector3 samples[5] = {
        position,
        { position.x - halfWidth, 0.0f, position.z - halfWidth },
        { position.x + halfWidth, 0.0f, position.z - halfWidth },
        { position.x - halfWidth, 0.0f, position.z + halfWidth },
        { position.x + halfWidth, 0.0f, position.z + halfWidth }
    };

    for (const Vector3& sample : samples)
    {
        if (maze.IsWorldPosBlocked(sample))
        {
            return false;
        }
    }

    return true;
}

static float EnemyDistanceSquaredXZ(Vector3 a, Vector3 b)
{
    const float dx = a.x - b.x;
    const float dz = a.z - b.z;
    return dx * dx + dz * dz;
}

// 取敌人当前路点方向，用于判断会车、同向跟车和超车关系。
static bool EnemyForwardDirection(
    const Enemy& enemy,
    const Maze& maze,
    Vector3& direction)
{
    GridPosition waypoint;
    if (!enemy.GetNextWaypoint(waypoint))
    {
        return false;
    }

    const Vector3 target = maze.GridToWorld(waypoint.x, waypoint.y);
    const float dx = target.x - enemy.GetPosition().x;
    const float dz = target.z - enemy.GetPosition().z;
    const float length = sqrt(dx * dx + dz * dz);
    if (length < 1e-4f)
    {
        return false;
    }

    direction = { dx / length, 0.0f, dz / length };
    return true;
}

// 返回正数表示 first 优先，负数表示 second 优先，0 表示同级。
static int EnemyPriority(
    const Enemy& first,
    const Enemy& second,
    const PlayerSnapshot& player,
    Vector3 firstPosition,
    Vector3 secondPosition)
{
    // 追击怪优先于巡逻怪，保证追击目标的一方不会被巡逻怪反向顶住。
    if (first.GetType() != second.GetType())
    {
        return first.GetType() == EnemyType::Chaser ? 1 : -1;
    }

    // 同为追击怪时，离玩家更近的一方优先，队首不会被队尾反超。
    if (first.GetType() == EnemyType::Chaser)
    {
        const float firstDistance =
            EnemyDistanceSquaredXZ(firstPosition, player.feetPosition);
        const float secondDistance =
            EnemyDistanceSquaredXZ(secondPosition, player.feetPosition);

        if (firstDistance + 1e-4f < secondDistance)
        {
            return 1;
        }
        if (secondDistance + 1e-4f < firstDistance)
        {
            return -1;
        }
    }

    return 0;
}

static float EnemyChebyshevDistance(Vector3 a, Vector3 b)
{
    return max(abs(a.x - b.x), abs(a.z - b.z));
}

// 同时评估两个分离轴，选择能让这一对分开最多的可行方案。
// 只看穿透最浅的轴时，多个敌人贴墙会一直挤在同一条窄缝里；
// 改走走廊方向往往能更快让出空间。
static bool TrySeparatePair(
    Enemy& first,
    Enemy& second,
    const Maze& maze,
    float maxCorrection)
{
    const Vector3 firstPosition = first.GetPosition();
    const Vector3 secondPosition = second.GetPosition();

    const float minimumDistance =
        EnemyConfig::ENEMY_HALF_WIDTH * 2.0f +
        EnemyConfig::ENEMY_COLLISION_PADDING;
    const float dx = secondPosition.x - firstPosition.x;
    const float dz = secondPosition.z - firstPosition.z;
    const float overlapX = minimumDistance - abs(dx);
    const float overlapZ = minimumDistance - abs(dz);

    if (overlapX <= 0.0f || overlapZ <= 0.0f)
    {
        return false;
    }

    const float currentSeparation =
        EnemyChebyshevDistance(firstPosition, secondPosition);
    float bestSeparation = currentSeparation;
    Vector3 bestFirstPosition = firstPosition;
    Vector3 bestSecondPosition = secondPosition;

    const bool axisOrder[2] = {
        overlapX <= overlapZ,
        overlapX > overlapZ
    };

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        const bool axisX = axisOrder[attempt];
        const float penetration = axisX ? overlapX : overlapZ;
        const float correction = min(penetration * 0.6f, maxCorrection);
        if (correction <= 1e-5f)
        {
            continue;
        }

        const float delta = axisX ? dx : dz;
        const float sign = delta >= 0.0f ? 1.0f : -1.0f;
        const Vector3 axis = axisX
            ? Vector3{ 1.0f, 0.0f, 0.0f }
            : Vector3{ 0.0f, 0.0f, 1.0f };

        // 方案A：双方各退一半。
        const Vector3 halfFirst = Vector3Add(
            firstPosition,
            Vector3Scale(axis, -sign * correction * 0.5f)
        );
        const Vector3 halfSecond = Vector3Add(
            secondPosition,
            Vector3Scale(axis, sign * correction * 0.5f)
        );
        if (EnemyPositionWalkable(maze, halfFirst) &&
            EnemyPositionWalkable(maze, halfSecond))
        {
            const float separation =
                EnemyChebyshevDistance(halfFirst, halfSecond);
            if (separation > bestSeparation)
            {
                bestSeparation = separation;
                bestFirstPosition = halfFirst;
                bestSecondPosition = halfSecond;
            }
        }

        // 方案B：一方贴墙时，让能移动的一方承担整段修正。
        const Vector3 fullFirst = Vector3Add(
            firstPosition,
            Vector3Scale(axis, -sign * correction)
        );
        if (EnemyPositionWalkable(maze, fullFirst))
        {
            const float separation =
                EnemyChebyshevDistance(fullFirst, secondPosition);
            if (separation > bestSeparation)
            {
                bestSeparation = separation;
                bestFirstPosition = fullFirst;
                bestSecondPosition = secondPosition;
            }
        }

        const Vector3 fullSecond = Vector3Add(
            secondPosition,
            Vector3Scale(axis, sign * correction)
        );
        if (EnemyPositionWalkable(maze, fullSecond))
        {
            const float separation =
                EnemyChebyshevDistance(firstPosition, fullSecond);
            if (separation > bestSeparation)
            {
                bestSeparation = separation;
                bestFirstPosition = firstPosition;
                bestSecondPosition = fullSecond;
            }
        }
    }

    if (bestSeparation <= currentSeparation + 1e-5f)
    {
        return false;
    }

    first.SetPosition(bestFirstPosition, maze);
    second.SetPosition(bestSecondPosition, maze);
    return true;
}

// 判断敌人当前状态是否应该产生脚步。
static bool EnemyStateNeedsFootsteps(EnemyState state)
{
    return state == EnemyState::Patrol ||
           state == EnemyState::Chase ||
           state == EnemyState::Search ||
           state == EnemyState::Return;
}

// 根据每帧移动距离产生脚步事件。脚步按距离触发，敌人站住不动时不会继续播放。
void EnemyManager::AccumulateFootstepEvents(
    float dt,
    vector<FootstepEvent>& events)
{
    for (EnemySlot& slot : enemies_)
    {
        Enemy* enemy = slot.enemy.get();
        if (enemy == nullptr || !enemy->IsActive())
        {
            slot.footstepPositionValid = false;
            slot.footstepDistance = 0.0f;
            continue;
        }

        const EnemyState state = enemy->GetRenderState().state;
        if (!EnemyStateNeedsFootsteps(state))
        {
            slot.footstepPositionValid = false;
            slot.footstepDistance = 0.0f;
            continue;
        }

        const Vector3 position = enemy->GetPosition();
        if (!slot.footstepPositionValid)
        {
            slot.lastFootstepPosition = position;
            slot.footstepPositionValid = true;
            slot.footstepDistance = 0.0f;
            continue;
        }

        const float dx = position.x - slot.lastFootstepPosition.x;
        const float dz = position.z - slot.lastFootstepPosition.z;
        const float moved = sqrt(dx * dx + dz * dz);
        slot.lastFootstepPosition = position;

        // 过滤碰撞求解带来的极小抖动，避免敌人站着不动也不断触发脚步。
        if (moved < 0.0005f)
        {
            continue;
        }

        slot.footstepDistance += moved;
        const float stride = (slot.type == EnemyType::Chaser)
            ? FootstepConfig::CHASER_STRIDE
            : FootstepConfig::PATROL_STRIDE;

        if (slot.footstepDistance >= stride)
        {
            // 保留超过一个步伐的余数，保证连续移动时脚步节奏不会丢拍。
            slot.footstepDistance -= stride;
            const float speed = (dt > 1e-4f) ? moved / dt : 0.0f;
            events.push_back({ slot.type, position, speed });
        }
    }
}

void EnemyManager::ComputeEnemySteering(float dt, const PlayerSnapshot& player)
{
    // 先清空上一帧提示；blockedSteerTimer 保留，用于判断同一方向是否连续被墙挡住。
    for (EnemySlot& slot : enemies_)
    {
        slot.steeringOffset = Vector3{ 0.0f, 0.0f, 0.0f };
        slot.steeringSpeedScale = 1.0f;
    }

    auto addOffset = [this](size_t index, Vector3 offset)
    {
        if (index >= enemies_.size())
        {
            return;
        }

        enemies_[index].steeringOffset.x += offset.x;
        enemies_[index].steeringOffset.z += offset.z;
    };

    const size_t count = enemies_.size();
    for (size_t i = 0; i < count; ++i)
    {
        Enemy* first = enemies_[i].enemy.get();
        if (first == nullptr || !first->IsActive())
        {
            continue;
        }

        Vector3 firstForward;
        const bool firstHasForward =
            EnemyForwardDirection(*first, maze_, firstForward);

        for (size_t j = i + 1; j < count; ++j)
        {
            Enemy* second = enemies_[j].enemy.get();
            if (second == nullptr || !second->IsActive())
            {
                continue;
            }

            const Vector3 firstPosition = first->GetPosition();
            const Vector3 secondPosition = second->GetPosition();
            const float distanceSquared =
                EnemyDistanceSquaredXZ(firstPosition, secondPosition);
            if (distanceSquared >
                EnemyConfig::ENEMY_NEIGHBOR_RADIUS *
                EnemyConfig::ENEMY_NEIGHBOR_RADIUS)
            {
                continue;
            }

            const float distance = sqrt(max(distanceSquared, 1e-8f));
            Vector3 relative = {
                secondPosition.x - firstPosition.x,
                0.0f,
                secondPosition.z - firstPosition.z
            };
            if (distance > 1e-4f)
            {
                relative.x /= distance;
                relative.z /= distance;
            }
            else
            {
                relative = { 1.0f, 0.0f, 0.0f };
            }

            Vector3 secondForward;
            const bool secondHasForward =
                EnemyForwardDirection(*second, maze_, secondForward);

            const float forwardDot =
                firstHasForward && secondHasForward
                    ? firstForward.x * secondForward.x +
                        firstForward.z * secondForward.z
                    : 0.0f;

            // 迎面相遇：双方按右侧通行规则偏移，形成稳定的会车车道。
            if (firstHasForward && secondHasForward && forwardDot < -0.25f)
            {
                const float firstAhead =
                    relative.x * firstForward.x +
                    relative.z * firstForward.z;
                const float secondAhead =
                    -relative.x * secondForward.x -
                    relative.z * secondForward.z;

                if (firstAhead > 0.0f && secondAhead > 0.0f)
                {
                    const Vector3 firstRight = {
                        firstForward.z,
                        0.0f,
                        -firstForward.x
                    };
                    const Vector3 secondRight = {
                        secondForward.z,
                        0.0f,
                        -secondForward.x
                    };
                    addOffset(
                        i,
                        Vector3Scale(
                            firstRight,
                            EnemyConfig::ENEMY_LANE_OFFSET
                        )
                    );
                    addOffset(
                        j,
                        Vector3Scale(
                            secondRight,
                            EnemyConfig::ENEMY_LANE_OFFSET
                        )
                    );

                    // 优先级低的一方略减速，给高优先级一方留出通过窗口。
                    const int priority = EnemyPriority(
                        *first,
                        *second,
                        player,
                        firstPosition,
                        secondPosition
                    );
                    if (priority > 0)
                    {
                        enemies_[j].steeringSpeedScale = min(
                            enemies_[j].steeringSpeedScale,
                            0.85f
                        );
                    }
                    else if (priority < 0)
                    {
                        enemies_[i].steeringSpeedScale = min(
                            enemies_[i].steeringSpeedScale,
                            0.85f
                        );
                    }
                }
            }
            // 同向跟随：后车减速；追兵从后方接近巡逻怪时，前怪靠右、追兵靠左形成超车通道。
            else if (firstHasForward && secondHasForward && forwardDot > 0.25f)
            {
                const float projection =
                    relative.x * firstForward.x +
                    relative.z * firstForward.z;
                const bool secondIsAhead = projection > 0.0f;
                const size_t frontIndex = secondIsAhead ? j : i;
                const size_t rearIndex = secondIsAhead ? i : j;
                Enemy* front = enemies_[frontIndex].enemy.get();
                Enemy* rear = enemies_[rearIndex].enemy.get();

                if (front != nullptr && rear != nullptr)
                {
                    const bool overtake =
                        front->GetType() == EnemyType::Patrol &&
                        rear->GetType() == EnemyType::Chaser;

                    if (overtake)
                    {
                        Vector3 frontForward;
                        Vector3 rearForward;
                        if (EnemyForwardDirection(*front, maze_, frontForward) &&
                            EnemyForwardDirection(*rear, maze_, rearForward))
                        {
                            const Vector3 frontRight = {
                                frontForward.z,
                                0.0f,
                                -frontForward.x
                            };
                            const Vector3 rearRight = {
                                rearForward.z,
                                0.0f,
                                -rearForward.x
                            };
                            addOffset(
                                frontIndex,
                                Vector3Scale(
                                    frontRight,
                                    EnemyConfig::ENEMY_LANE_OFFSET
                                )
                            );
                            addOffset(
                                rearIndex,
                                Vector3Scale(
                                    rearRight,
                                    -EnemyConfig::ENEMY_LANE_OFFSET
                                )
                            );
                            enemies_[frontIndex].steeringSpeedScale = min(
                                enemies_[frontIndex].steeringSpeedScale,
                                0.9f
                            );
                        }
                    }
                    else
                    {
                        enemies_[rearIndex].steeringSpeedScale = min(
                            enemies_[rearIndex].steeringSpeedScale,
                            0.72f
                        );
                    }
                }
            }

            // 软个人空间提前分流，避免等到硬碰撞才开始处理。
            if (distance < EnemyConfig::ENEMY_PERSONAL_SPACE)
            {
                const float push =
                    (EnemyConfig::ENEMY_PERSONAL_SPACE - distance) * 0.35f;
                addOffset(i, Vector3Scale(relative, -push));
                addOffset(j, Vector3Scale(relative, push));
            }
        }
    }

    // 限制最终偏移长度；若目标点落在墙里，保持一小段时间后翻面，避免每帧左右抖动。
    for (size_t i = 0; i < enemies_.size(); ++i)
    {
        EnemySlot& slot = enemies_[i];
        if (slot.enemy == nullptr || !slot.enemy->IsActive())
        {
            continue;
        }

        Vector3 offset = slot.steeringOffset;
        float length = sqrt(offset.x * offset.x + offset.z * offset.z);

        // 只有附近确实存在其他敌人时，才需要打破多敌人对称；
        // 单独巡逻时保持原始格心路线，不产生无意义的横向漂移。
        bool nearOther = false;
        for (size_t j = 0; j < enemies_.size(); ++j)
        {
            if (j == i)
            {
                continue;
            }

            Enemy* other = enemies_[j].enemy.get();
            if (other == nullptr || !other->IsActive())
            {
                continue;
            }

            if (EnemyDistanceSquaredXZ(
                    slot.enemy->GetPosition(),
                    other->GetPosition()) <=
                EnemyConfig::ENEMY_NEIGHBOR_RADIUS *
                EnemyConfig::ENEMY_NEIGHBOR_RADIUS)
            {
                nearOther = true;
                break;
            }
        }

        // 三个以上敌人时，成对的右行偏移可能互相抵消。
        // 这里给接近零的合成偏移一个按索引固定的侧向分量，打破对称僵局。
        if (nearOther && length < 0.08f)
        {
            Vector3 forward;
            if (EnemyForwardDirection(*slot.enemy, maze_, forward))
            {
                const float side = (i % 2 == 0) ? 1.0f : -1.0f;
                offset = {
                    forward.z * side * EnemyConfig::ENEMY_LANE_FALLBACK_OFFSET,
                    0.0f,
                    -forward.x * side * EnemyConfig::ENEMY_LANE_FALLBACK_OFFSET
                };
                length = EnemyConfig::ENEMY_LANE_FALLBACK_OFFSET;
            }
        }

        if (length > EnemyConfig::ENEMY_LANE_OFFSET && length > 1e-5f)
        {
            const float scale = EnemyConfig::ENEMY_LANE_OFFSET / length;
            offset.x *= scale;
            offset.z *= scale;
            length = EnemyConfig::ENEMY_LANE_OFFSET;
        }

        if (length > 1e-5f)
        {
            const Vector3 probe =
                Vector3Add(slot.enemy->GetPosition(), offset);
            if (!EnemyPositionWalkable(maze_, probe))
            {
                slot.blockedSteerTimer += dt;
                if (slot.blockedSteerTimer >= 0.35f)
                {
                    offset.x = -offset.x;
                    offset.z = -offset.z;
                    slot.blockedSteerTimer = 0.0f;
                }
            }
            else
            {
                slot.blockedSteerTimer = 0.0f;
            }
        }
        else
        {
            slot.blockedSteerTimer = 0.0f;
        }

        slot.steeringOffset = offset;
        slot.enemy->SetSteeringHint(offset, slot.steeringSpeedScale);
    }
}

void EnemyManager::ResolveEnemyCollisions(
    float dt,
    const PlayerSnapshot& player)
{
    const size_t count = enemies_.size();

    // 多轮迭代分离：后面的修正可能重新影响前面的配对，因此不能用单遍处理。
    // 每轮只修正一部分穿透，允许跨帧继续收敛，不再出现失败后整体回退。
    for (int iteration = 0;
        iteration < EnemyConfig::ENEMY_SEPARATION_ITERATIONS;
        ++iteration)
    {
        bool correctedAny = false;

        for (size_t i = 0; i < count; ++i)
        {
            Enemy* first = enemies_[i].enemy.get();
            if (first == nullptr || !first->IsActive())
            {
                continue;
            }

            for (size_t j = i + 1; j < count; ++j)
            {
                Enemy* second = enemies_[j].enemy.get();
                if (second == nullptr || !second->IsActive())
                {
                    continue;
                }

                const GridPosition beforeFirstGrid = first->GetGridPosition();
                const GridPosition beforeSecondGrid = second->GetGridPosition();
                if (!TrySeparatePair(
                        *first,
                        *second,
                        maze_,
                        EnemyConfig::ENEMY_SEPARATION_MAX_STEP))
                {
                    continue;
                }

                correctedAny = true;

                // 被推入相邻网格后作废旧路径，避免继续追过期路点；
                // 同一格内的小幅分离保留路径和转向，减少频繁重算。
                if (first->GetGridPosition() != beforeFirstGrid)
                {
                    first->InvalidatePath();
                }
                if (second->GetGridPosition() != beforeSecondGrid)
                {
                    second->InvalidatePath();
                }
            }
        }

        if (!correctedAny)
        {
            break;
        }
    }

    // 卡死看门狗：按固定间隔比较净位移。
    // 用锚点而不是单帧位移，避免敌人被来回推开时误判为已经前进。
    for (size_t i = 0; i < count; ++i)
    {
        EnemySlot& slot = enemies_[i];
        Enemy* enemy = slot.enemy.get();
        if (enemy == nullptr || !enemy->IsActive())
        {
            slot.stuckAnchorValid = false;
            slot.stuckSampleTimer = 0.0f;
            continue;
        }

        const Vector3 position = enemy->GetPosition();
        if (!slot.stuckAnchorValid)
        {
            slot.stuckAnchor = position;
            slot.stuckSampleTimer = 0.0f;
            slot.stuckAnchorValid = true;
        }

        slot.stuckSampleTimer += dt;
        if (slot.stuckSampleTimer < EnemyConfig::ENEMY_STUCK_SAMPLE_TIME)
        {
            continue;
        }

        const float progressSquared = EnemyDistanceSquaredXZ(
            slot.stuckAnchor,
            position
        );
        const float progressLimitSquared =
            EnemyConfig::ENEMY_STUCK_PROGRESS_DISTANCE *
            EnemyConfig::ENEMY_STUCK_PROGRESS_DISTANCE;

        const EnemyState state = enemy->GetRenderState().state;
        const bool expectsMovement =
            state == EnemyState::Patrol ||
            state == EnemyState::Chase ||
            state == EnemyState::Search ||
            state == EnemyState::Return;

        // 只有附近还有其他敌人才属于“互锁卡死”；单独等待时不做强制恢复。
        bool closeToOther = false;
        for (size_t j = 0; j < enemies_.size(); ++j)
        {
            if (j == i)
            {
                continue;
            }

            Enemy* other = enemies_[j].enemy.get();
            if (other == nullptr || !other->IsActive())
            {
                continue;
            }

            if (EnemyDistanceSquaredXZ(position, other->GetPosition()) <=
                EnemyConfig::ENEMY_NEIGHBOR_RADIUS *
                EnemyConfig::ENEMY_NEIGHBOR_RADIUS)
            {
                closeToOther = true;
                break;
            }
        }

        if (expectsMovement && closeToOther &&
            progressSquared < progressLimitSquared)
        {
            slot.stuckTimer += slot.stuckSampleTimer;
            if (slot.stuckTimer >= EnemyConfig::ENEMY_STUCK_TIME)
            {
                // 连续无位移时做一次有限范围恢复：只移动不到半格，不穿墙、不与其他敌人重叠。
                if (!TryRecoverStuckEnemy(i))
                {
                    slot.stuckTimer = EnemyConfig::ENEMY_STUCK_TIME * 0.6f;
                }
            }
        }
        else
        {
            slot.stuckTimer = 0.0f;
        }

        slot.stuckAnchor = enemy->GetPosition();
        slot.stuckSampleTimer = 0.0f;
    }
}

bool EnemyManager::TryRecoverStuckEnemy(size_t index)
{
    if (index >= enemies_.size())
    {
        return false;
    }

    EnemySlot& slot = enemies_[index];
    Enemy* enemy = slot.enemy.get();
    if (enemy == nullptr || !enemy->IsActive())
    {
        return false;
    }

    const Vector3 position = enemy->GetPosition();
    Vector3 preferred = slot.steeringOffset;
    preferred.y = 0.0f;

    float preferredLength = sqrt(
        preferred.x * preferred.x +
        preferred.z * preferred.z
    );

    // 没有转向提示时，优先背离最近敌人，尽快打破互锁。
    if (preferredLength < 1e-4f)
    {
        float nearestSquared = numeric_limits<float>::max();
        for (size_t j = 0; j < enemies_.size(); ++j)
        {
            if (j == index)
            {
                continue;
            }

            Enemy* other = enemies_[j].enemy.get();
            if (other == nullptr || !other->IsActive())
            {
                continue;
            }

            const Vector3 otherPosition = other->GetPosition();
            const float distanceSquared =
                EnemyDistanceSquaredXZ(position, otherPosition);
            if (distanceSquared < nearestSquared)
            {
                nearestSquared = distanceSquared;
                preferred.x = position.x - otherPosition.x;
                preferred.z = position.z - otherPosition.z;
            }
        }

        preferredLength = sqrt(
            preferred.x * preferred.x +
            preferred.z * preferred.z
        );
    }

    if (preferredLength < 1e-4f)
    {
        preferred = { 1.0f, 0.0f, 0.0f };
        preferredLength = 1.0f;
    }

    preferred.x /= preferredLength;
    preferred.z /= preferredLength;

    const Vector3 directions[6] = {
        preferred,
        { -preferred.x, 0.0f, -preferred.z },
        { 1.0f, 0.0f, 0.0f },
        { -1.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f }
    };
    const float distances[3] = { 0.3f, 0.6f, 0.9f };

    for (const float distance : distances)
    {
        for (const Vector3& direction : directions)
        {
            const Vector3 candidate = {
                position.x + direction.x * distance,
                0.0f,
                position.z + direction.z * distance
            };

            if (!EnemyPositionWalkable(maze_, candidate))
            {
                continue;
            }

            bool blocked = false;
            for (size_t j = 0; j < enemies_.size(); ++j)
            {
                if (j == index)
                {
                    continue;
                }

                Enemy* other = enemies_[j].enemy.get();
                if (other == nullptr || !other->IsActive())
                {
                    continue;
                }

                if (EnemyBoxesOverlapAt(candidate, other->GetPosition()))
                {
                    blocked = true;
                    break;
                }
            }

            if (blocked)
            {
                continue;
            }

            enemy->SetPosition(candidate, maze_);
            enemy->InvalidatePath();
            enemy->SetSteeringHint(
                Vector3{ 0.0f, 0.0f, 0.0f },
                1.0f
            );
            slot.stuckTimer = 0.0f;
            slot.blockedSteerTimer = 0.0f;
            return true;
        }
    }

    return false;
}

void EnemyManager::WorkerLoop()
{
    using Clock = chrono::steady_clock;

    const auto tickDuration = chrono::milliseconds(16);
    auto nextTick = Clock::now() + tickDuration;
#ifdef MAZE_WEB
    // Web：dt 从"上一次被调用"算起（lastTick_ 是成员，跨调用保留）
    auto& lastTick = lastTick_;
#else
    auto lastTick = Clock::now();
#endif

    while (running_.load())
    {
        PlayerSnapshot player;
        vector<OrbFireRequest> fireRequests;

        {
            unique_lock<mutex> lock(inputMutex_);
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

            if (playerSnapshotUpdated_)
            {
                player = playerSnapshot_;
                playerSnapshotUpdated_ = false;
            }

            fireRequests.swap(pendingFireRequests_);
        }

        const auto now = Clock::now();
        float dt = chrono::duration<float>(now - lastTick).count();
        dt = max(0.0f, min(dt, 0.05f));
        lastTick = now;

        fireCooldown_ = max(0.0f, fireCooldown_ - dt);
        int newDefeatedEnemyCount = 0;
        if (!fireRequests.empty() && fireCooldown_ <= 0.0f)
        {
            const Vector3 direction = Vector3Normalize(fireRequests.front().direction);
            if (Vector3Length(direction) > 1e-4f)
            {
                OrbState orb;
                orb.position = Vector3Add(
                    fireRequests.front().origin,
                    Vector3Scale(direction, EnemyConfig::ORB_RADIUS + 0.05f)
                );
                orb.velocity = Vector3Scale(direction, EnemyConfig::ORB_SPEED);
                orb.radius = EnemyConfig::ORB_RADIUS;
                orb.remainingLife = EnemyConfig::ORB_LIFETIME;
                orbs_.push_back(orb);
                fireCooldown_ = EnemyConfig::ORB_FIRE_COOLDOWN;
            }
        }

        for (auto orb = orbs_.begin(); orb != orbs_.end();)
        {
            orb->remainingLife -= dt;
            orb->position = Vector3Add(
                orb->position,
                Vector3Scale(orb->velocity, dt)
            );

            bool removeOrb = orb->remainingLife <= 0.0f;
            if (!removeOrb && maze_.IsWorldPosBlocked(orb->position))
            {
                removeOrb = true;
            }

            if (!removeOrb)
            {
                for (EnemySlot& slot : enemies_)
                {
                    if (slot.enemy == nullptr || !slot.enemy->IsActive())
                    {
                        continue;
                    }

                    if (CheckCollisionBoxSphere(
                            slot.enemy->GetBoundingBox(),
                            orb->position,
                            orb->radius))
                    {
                        // 肉鸽增益「一击必杀」：直接秒杀；否则走常规光球伤害
                        slot.enemy->ApplyDamage(
                            orbOneShot ? 100000 : EnemyConfig::ORB_DAMAGE);
                        if (!slot.enemy->IsActive())
                        {
                            slot.rebornTimer = EnemyConfig::ENEMY_REborn_TIME;
                            ++newDefeatedEnemyCount;
                        }
                        removeOrb = true;
                        break;
                    }
                }
            }

            if (removeOrb)
            {
                orb = orbs_.erase(orb);
            }
            else
            {
                ++orb;
            }
        }

        EnemyUpdateContext context;
        context.maze = &maze_;
        context.pathfinder = &pathfinder_;
        context.player = player;

        vector<DamageEvent> newDamageEvents;
        vector<FootstepEvent> newFootstepEvents;
        vector<EnemyRenderState> newRenderStates;
        newRenderStates.reserve(enemies_.size());

        // 先根据上一帧位置计算右行/让行/队列速度，再由敌人自身推进位置。
        ComputeEnemySteering(dt, player);

        for (size_t i = 0; i < enemies_.size(); ++i)
        {
            EnemySlot& slot = enemies_[i];
            if (slot.enemy == nullptr)
            {
                continue;
            }

            if (slot.enemy->IsActive())
            {
                slot.enemy->Update(context, dt, newDamageEvents);
                if (!slot.enemy->IsActive())
                {
                    slot.rebornTimer = EnemyConfig::ENEMY_REborn_TIME;
                    slot.stuckTimer = 0.0f;
                    slot.stuckSampleTimer = 0.0f;
                    slot.stuckAnchorValid = false;
                    slot.blockedSteerTimer = 0.0f;
                    slot.steeringOffset = Vector3{ 0.0f, 0.0f, 0.0f };
                    slot.steeringSpeedScale = 1.0f;
                    slot.footstepPositionValid = false;
                    slot.footstepDistance = 0.0f;
                }
            }
            else
            {
                slot.rebornTimer -= dt;
                if (slot.rebornTimer <= 0.0f)
                {
                    const Vector3 bornPosition = maze_.GridToWorld(
                        slot.bornGrid.x,
                        slot.bornGrid.y
                    );
                    bool bornBlocked = false;
                    for (size_t j = 0; j < enemies_.size(); ++j)
                    {
                        Enemy* other = enemies_[j].enemy.get();
                        if (j == i || other == nullptr || !other->IsActive())
                        {
                            continue;
                        }

                        if (EnemyBoxesOverlapAt(
                                bornPosition,
                                other->GetPosition()))
                        {
                            bornBlocked = true;
                            break;
                        }
                    }

                    if (!bornBlocked)
                    {
                        slot.enemy = EnemyFactory::Create(
                            slot.type,
                            maze_,
                            slot.bornGrid
                        );
                        slot.rebornTimer = 0.0f;
                        slot.stuckTimer = 0.0f;
                        slot.stuckSampleTimer = 0.0f;
                        slot.stuckAnchorValid = false;
                        slot.blockedSteerTimer = 0.0f;
                        slot.steeringOffset = Vector3{ 0.0f, 0.0f, 0.0f };
                        slot.steeringSpeedScale = 1.0f;
                        slot.footstepPositionValid = false;
                        slot.footstepDistance = 0.0f;
                    }
                }
            }
        }

        AccumulateFootstepEvents(dt, newFootstepEvents);
        ResolveEnemyCollisions(dt, player);

        // 分离求解可能修正敌人位置，同步脚步参考点，避免下一帧把挤压位移算成脚步。
        for (EnemySlot& slot : enemies_)
        {
            if (slot.enemy != nullptr && slot.enemy->IsActive())
            {
                slot.lastFootstepPosition = slot.enemy->GetPosition();
            }
            else
            {
                slot.footstepPositionValid = false;
                slot.footstepDistance = 0.0f;
            }
        }

        for (const EnemySlot& slot : enemies_)
        {
            if (slot.enemy != nullptr && slot.enemy->IsActive())
            {
                newRenderStates.push_back(slot.enemy->GetRenderState());
            }
        }

        vector<OrbRenderState> newOrbRenderStates;
        newOrbRenderStates.reserve(orbs_.size());
        for (const OrbState& orb : orbs_)
        {
            OrbRenderState state;
            state.position = orb.position;
            state.radius = orb.radius;
            newOrbRenderStates.push_back(state);
        }

        {
            lock_guard<mutex> lock(outputMutex_);
            renderStates_ = move(newRenderStates);
            orbRenderStates_ = move(newOrbRenderStates);
            damageEvents_.insert(
                damageEvents_.end(),
                newDamageEvents.begin(),
                newDamageEvents.end()
            );
            footstepEvents_.insert(
                footstepEvents_.end(),
                newFootstepEvents.begin(),
                newFootstepEvents.end()
            );
            defeatedEnemyCount_ += newDefeatedEnemyCount;
        }

        nextTick += tickDuration;
        if (nextTick < Clock::now())
        {
            nextTick = Clock::now() + tickDuration;
        }

#ifdef MAZE_WEB
        break;      // Web：主线程每帧只调一次，跑完这一拍就交还控制权
#endif
    }
}