#pragma once
// ---------------------------------------------------------------------------
// RLSpec.h : C++ mirror of rl/rl/spec.py (the single source of truth).
// Every number here MUST match spec.py. If you change one side, change both
// and re-run rl/tools/consistency_test.py.
//
// v3: the MDP is "walk the maze, eat treasures, avoid DEATH by enemies":
//   - action space = 24 moves + 1 fire (25)
//       move index = dir * 6 + sprint * 3 + lane
//         lane 0 = cell centre, 1 / 2 = +-kLaneOffsets metres off-axis
//       kActFire = auto-aim the nearest visible enemy, 25 damage, 1 ammo
//   - observation gains hp ratio, nearest enemy distance and ammo ratio
//   - getting bitten costs HP (150/100/70 by difficulty, 10 per bite);
//     HP <= 0 ends the episode and pays kRDeath
// ---------------------------------------------------------------------------

namespace rl
{
    // ---- action space (25 = 24 moves + 1 fire) ----
    // dir 0 = UP    (grid -Y, world -Z)    1 = DOWN  (grid +Y, world +Z)
    // dir 2 = LEFT  (grid -X, world -X)    3 = RIGHT (grid +X, world +X)
    constexpr int kNDirs = 4;
    constexpr int kNLanes = 3;
    constexpr int kMoveActionCount = kNDirs * 2 * kNLanes;      // 24
    constexpr int kActFire = kMoveActionCount;                  // 24
    constexpr int kNActions = kMoveActionCount + 1;             // 25

    constexpr int kDirDx[kNDirs] = { 0, 0, -1,  1 };
    constexpr int kDirDy[kNDirs] = { -1, 1,  0,  0 };

    // Lateral lanes for "squeeze past the enemy along the wall".
    // The offset MUST exceed kEnemyContactHalf (0.65 m): the bite test is an
    // AABB overlap (player half 0.3 + enemy half 0.35), so the old 0.6 m lane
    // still overlapped and the robot got bitten on every lane it picked.
    // Upper bound: 2 m corridor - 0.3 m player half = 0.7 m. 0.68 leaves ~3 cm
    // to the wall and ~3 cm clear of the bite box.
    constexpr float kLaneOffsets[kNLanes] = { 0.0f, -0.68f, 0.68f };  // metres

    // The collision half-width the lane / bite geometry above is built on
    // (spec.py: corridor 2 m, player half 0.3 m, upper bound 0.7 m). The stock
    // game uses HALFWIDTH = 0.64 m as a proxy for the character mesh; with that
    // the player has only 1.0 - 0.64 = 0.36 m of lateral freedom, so every lane
    // target is unreachable and the walker burns its 1.5 s stall recovery on
    // each lane change. RL mode swaps in this value instead.
    constexpr float kPlayerHalfWidth = 0.30f;

    inline int  ActDir(int a) { return (a >= 0 && a < kMoveActionCount) ? (a / 6) : 0; }
    inline int  ActSub(int a) { return (a >= 0 && a < kMoveActionCount) ? (a % 6) : 0; }
    inline int  ActLane(int a) { return (a >= 0 && a < kMoveActionCount) ? (a % 3) : 0; }
    inline bool ActSprint(int a) { return (a >= 0 && a < kMoveActionCount) && ((a % 6) / 3 != 0); }
    inline bool ActIsFire(int a) { return a == kActFire; }
    inline float ActLaneOffset(int a) { return kLaneOffsets[ActLane(a)]; }
    // Lane axis: perpendicular to the direction of travel (grid units, +-1).
    inline int  LanePerpX(int dir) { return -kDirDy[dir]; }
    inline int  LanePerpY(int dir) { return  kDirDx[dir]; }

    // Per-action names (HUD / telemetry / bridge indexing). Order matches
    // spec.ACTION_NAMES: dir*6 + sprint*3 + lane, then FIRE.
    constexpr const char* kActName[kNActions] = {
        "UP", "UP-left", "UP-right", "UP+SHIFT", "UP+SHIFT-left", "UP+SHIFT-right",
        "DOWN", "DOWN-left", "DOWN-right", "DOWN+SHIFT", "DOWN+SHIFT-left", "DOWN+SHIFT-right",
        "LEFT", "LEFT-left", "LEFT-right", "LEFT+SHIFT", "LEFT+SHIFT-left", "LEFT+SHIFT-right",
        "RIGHT", "RIGHT-left", "RIGHT-right", "RIGHT+SHIFT", "RIGHT+SHIFT-left", "RIGHT+SHIFT-right",
        "FIRE"
    };

    // ---- observation ----
    constexpr int kObsWindow = 5;
    constexpr int kObsHalf = kObsWindow / 2;
    // 0 = walkable, 1 = exit, 2 = treasure, 3 = distance heat, 4 = enemy
    constexpr int kObsPlanes = 5;
    constexpr int kObsWindowDim = kObsPlanes * kObsWindow * kObsWindow;   // 125
    // 0 dx, 1 dy, 2 own dist, 3 stamina ratio, 4 sprinting,
    // 5 hp ratio, 6 nearest enemy dist / kFireRange, 7 ammo ratio
    constexpr int kObsExtraDim = 8;
    constexpr int kObsDim = kObsWindowDim + kObsExtraDim;                 // 133
    constexpr int kObsHp = 5;
    constexpr int kObsEnemyDist = 6;
    constexpr int kObsAmmo = 7;

    // Heat-map scale. plane3 stores (cell distance - player distance) / kDistScale,
    // i.e. a *relative* gradient. An absolute distance / maxDist encoding would
    // have a local gradient of only 1/maxDist (~0.0025 on a 31x31 maze) and the
    // tanh MLP cannot resolve it. Keep this equal to spec.DIST_SCALE.
    constexpr float kDistScale = 8.0f;

    constexpr int kDistPlane = 3;
    constexpr int kEnemyPlane = 4;
    constexpr int kDistBegin = kDistPlane * kObsWindow * kObsWindow;      // 75
    constexpr int kDistEnd = (kDistPlane + 1) * kObsWindow * kObsWindow;  // 100

    // ---- speed / stamina (Config.h, Player.cpp) ----
    constexpr float kWalkSpeed = 4.0f;                 // m/s
    constexpr float kSprintSpeed = 8.0f;               // m/s
    constexpr float kSprintMaxSeconds = 5.0f;
    constexpr float kSprintRecoverSeconds = 15.0f;
    constexpr float kStaminaRecoverPerSec = kSprintMaxSeconds / kSprintRecoverSeconds;
    // One decision covers exactly one cell: 0.5 s walking, 0.25 s sprinting.
    // Firing holds the player still for one walk-step (ORB_FIRE_COOLDOWN = 0.5 s).
    // (kWalkStepTime is defined after kCellSize below)
    constexpr float kFireStepTime = 0.5f;

    // ---- reward ----
    constexpr float kRTimePerSec = -0.4f;   // walk step -0.2, sprint step -0.1
    constexpr float kRProgress = 0.2f;      // +0.2 per cell closer to the exit
    constexpr float kRTreasure = 2.0f;      // per treasure picked up
    constexpr float kRExit = 10.0f;
    constexpr float kRIllegal = -0.5f;
    constexpr float kREnemyHit = -8.0f;     // per bite
    constexpr float kRFire = -0.1f;         // per shot (plus the time cost)
    constexpr float kRFireWaste = -0.5f;    // FIRE pressed with no ammo: pure waste
    constexpr float kRKill = 1.0f;          // per enemy killed
    constexpr float kRDeath = -25.0f;       // HP hit zero -> episode over

    // legacy aliases (older telemetry/docs referenced these names)
    constexpr float kROnPath = kRProgress;
    constexpr float kROffPath = -kRProgress;

    constexpr int kMaxStepFactor = 4;
    constexpr int kMinMaxSteps = 100;

    // ---- enemies (Enemy.h EnemyConfig; the game reads live positions) ----
    constexpr int kMaxEnemies = 6;
    // AABB contact: |dx| < 0.65 and |dz| < 0.65
    //   = HALFWIDTH (0.30) + ENEMY_HALF_WIDTH (0.35)
    constexpr float kEnemyContactHalf = 0.65f;

    // ---- player HP / ammo / firing (Config.h, Enemy.h) ----
    constexpr float kEnemyMaxHealth = 100.0f;   // ENEMY_MAX_HEALTH
    constexpr int kEnemyBiteDamage = 10;        // ENEMY_ATTACK_DAMAGE
    constexpr int kOrbDamage = 25;              // ORB_DAMAGE
    constexpr int kTreasureHealAmount = 10;     // main.cpp: heal * 10
    constexpr int kTreasureAmmoAmount = 3;      // main.cpp: attack * 3
    constexpr float kFireRange = 12.0f;         // m, conservative (orb flies ~21 m)
    // Config.h GetDifficultyConfig: { 15x15, 150 HP, 15 shots } easy,
    // { 21x21, 100 HP, 10 } normal, { 31x31, 70 HP, 6 } hard.
    constexpr int kPlayerMaxHealth[3] = { 150, 100, 70 };
    constexpr int kPlayerAttackLimit[3] = { 15, 10, 6 };

    // CellType values (Maze.h) used by the treasure pickup mirror.
    constexpr int kCellTreasureAttack = 3;
    constexpr int kCellTreasureHeal = 4;

    // ---- maze / world ----
    constexpr float kCellSize = 2.0f;
    constexpr float kWalkStepTime = kCellSize / kWalkSpeed;   // 0.5 s per cell

    // ---- wire protocol / weight file ----
    constexpr int kProtocolVersion = 3;        // game <-> python bridge
    constexpr int kWeightFormatVersion = 3;    // model.py::export_binary layout
    constexpr int kDefaultPort = 45871;

    inline int DifficultyOfSize(int size)
    {
        if (size >= 31) return 2;              // hard
        if (size >= 21) return 1;              // normal
        return 0;                              // easy
    }

    inline int MaxHealthForSize(int size) { return kPlayerMaxHealth[DifficultyOfSize(size)]; }
    inline int AttackLimitForSize(int size) { return kPlayerAttackLimit[DifficultyOfSize(size)]; }

    inline bool Walkable(int cell) { return cell != 0; }        // 0 == CellType::WALL
    inline bool IsExitCell(int cell) { return cell == 5; }      // 5 == CellType::EXIT
    inline bool IsTreasureCell(int cell) { return cell >= 2 && cell <= 4; }
}
