#pragma once
// ---------------------------------------------------------------------------
// RlAgent.h : the maze-solving MDP in C++, mirroring rl/rl/env_vec.py.
//
//    obs      : kObsDim floats (see RLSpec.h / spec.encode_obs)
//                5 planes of 5x5 -- walkable / exit / treasure / dist heat / enemy
//                + 8 scalars -- dx, dy, own dist, stamina, sprinting,
//                  hp ratio, nearest enemy distance, ammo ratio
//    action   : 25 = 24 grid moves (dir * 6 + sprint * 3 + lane) + 1 fire
//    reward   : time-based, treasures pay, bites cost HP, death ends the run
//               (exactly spec.compute_reward)
//    episode  : starts on the entry cell, ends at the exit, at HP <= 0 or
//               at MaxSteps()
//
// The agent works purely on grid cells. The 3D player is driven separately by
// RlWalker, so training speed never depends on the physics or the frame rate.
// The live game feeds it two things the grid alone cannot know: the enemy
// cells (for the enemy plane) and the player's real stamina / sprint state
// (so the time cost of every step matches what the player actually did).
// ---------------------------------------------------------------------------
#include <string>
#include <vector>
#include "RLSpec.h"
#include "MazeSolver.h"

class Maze;

namespace rl
{
    class RLNet;      // defined in RLNet.h (same namespace)

    class RlAgent
    {
    public:
        // ---- setup -------------------------------------------------------
        bool BindMaze(const Maze& maze);          // rebuild dist field + reset

        // Same, but from a raw CellType snapshot (used by the training bridge
        // worker thread, which owns its own copy of the grid).
        bool BindCells(const std::vector<int>& cells, int width, int height,
                       int entryGx, int entryGy, int exitGx, int exitGy);
        void SetNet(const RLNet* net) { net_ = net; }
        const RLNet* Net() const { return net_; }
        const DistField& Field() const { return field_; }
        bool Ready() const { return field_.Ready(); }

        // ---- grid state --------------------------------------------------
        void ResetToEntry();

        // Reposition the agent on another cell of the same maze (keeps the
        // distance field, clears the per-episode counters).
        void SetCell(int gx, int gy);
        int  Gx() const { return gx_; }
        int  Gy() const { return gy_; }
        int  EntryGx() const { return entryGx_; }
        int  EntryGy() const { return entryGy_; }
        int  Steps() const { return steps_; }
        int  OptimalSteps() const { return optimal_; }
        int  MaxSteps() const { return maxSteps_; }
        bool Done() const { return done_; }
        bool Success() const { return success_; }
        int  LastAction() const { return lastAction_; }
        float LastReward() const { return lastReward_; }
        int  IllegalCount() const { return illegalCount_; }
        int  AtExit() const { return field_.At(gx_, gy_) == 0; }

        // ---- v2/v3: what only the live game knows ------------------------
        // Live enemy positions, flattened as {gx0, gy0, gx1, gy1, ...} in
        // *continuous* maze-local grid coordinates (world / kCellSize). The
        // enemy plane floors them; the nearest-enemy scalar uses the exact
        // value, so it matches spec.encode_obs bit for bit.
        void SetEnemyGridPositions(const std::vector<float>& flatGxGy);
        int  EnemyCount() const { return (int)(enemyGrid_.size() / 2); }

        // The player's real HP / ammo, pushed every frame by the live game so
        // the grid-level MDP cannot drift away from what actually happened.
        void SetHealth(int hp, int maxHp);
        void SetAmmo(int ammo, int limit = 0);   // limit <= 0 keeps the maze's own
        int  Health() const { return hp_; }
        int  MaxHealth() const { return maxHp_; }
        float HealthRatio() const
        {
            return (maxHp_ > 0) ? (float)hp_ / (float)maxHp_ : 1.0f;
        }
        int  Ammo() const { return ammo_; }
        int  AmmoLimit() const { return ammoMax_; }
        float AmmoRatio() const
        {
            return (ammoMax_ > 0) ? (float)ammo_ / (float)ammoMax_ : 0.0f;
        }

        // The player's real stamina (0..1) and whether it is sprinting right
        // now. Drives obs extras 3/4 and the step's time cost.
        void SetSprintState(float staminaRatio, bool sprinting);
        bool LastSprinting() const { return lastSprinting_; }
        float LastDt() const { return lastDt_; }

        // Reported by the game when an enemy actually lands a bite. The
        // penalty is folded into the next ApplyAction reward so the telemetry
        // line and the training reward always agree.
        void NotifyEnemyHit(int times = 1);
        int  EnemyHits() const { return enemyHits_; }
        // Reported by the game when an enemy actually dies (the orb landed the
        // killing blow). Folded into the next ApplyAction reward, same as hits.
        void NotifyKill(int times = 1);
        int  Kills() const { return kills_; }
        // Did the last ApplyAction fire / kill / die? (telemetry)
        bool LastFired() const { return lastFired_; }
        int  LastKills() const { return lastKills_; }
        bool Died() const { return died_; }
        int  TreasuresCollected() const { return treasuresCollected_; }
        double Seconds() const { return seconds_; }

        // ---- observation -------------------------------------------------
        void EncodeObs(float* out) const;

        // ---- policy ------------------------------------------------------
        // Uses the loaded network; falls back to the distance-descent move when
        // no network is available so the mode still works without weights.
        int PolicyAction(float* outValue = nullptr, float* outLogits = nullptr) const;
        int GreedyAction() const;                 // dist descent (reference move)

        // ---- transition --------------------------------------------------
        // Applies one discrete action on the grid, updates the state and
        // returns the reward. `outIllegal` / `outSuccess` are filled in.
        float ApplyAction(int action, bool* outIllegal = nullptr,
                          bool* outSuccess = nullptr);

        // Same formula as spec.compute_reward; kept as one place so the wire
        // protocol, the fast trainer and the live walker cannot drift apart.
        static float ScoreTransition(int prevDist, int newDist, bool illegal,
                                     bool success, float dt, int treasures,
                                     int enemyHits, bool fired, bool wasted, int kills,
                                     bool died);
        static int MaxStepsFor(int optimalSteps);

        // ---- diagnostics -------------------------------------------------
        std::string Describe() const;

    private:
        DistField field_;
        const RLNet* net_ = nullptr;

        int gx_ = 1, gy_ = 1;
        int entryGx_ = 1, entryGy_ = 1;
        int steps_ = 0;
        int optimal_ = 0;
        int maxSteps_ = kMinMaxSteps;
        int lastAction_ = -1;
        float lastReward_ = 0.0f;
        bool done_ = false;
        bool success_ = false;
        int illegalCount_ = 0;

        // v2 / v3 live-game inputs + episode counters
        std::vector<float> enemyGrid_;      // {gx0, gy0, gx1, gy1, ...}
        float staminaRatio_ = 1.0f;
        bool sprinting_ = false;
        bool lastSprinting_ = false;
        float lastDt_ = kCellSize / kWalkSpeed;
        int pendingHits_ = 0;
        int pendingKills_ = 0;
        int enemyHits_ = 0;
        int treasuresCollected_ = 0;
        double seconds_ = 0.0;
        int hp_ = 100;
        int maxHp_ = 100;
        int ammo_ = 0;
        int ammoMax_ = 0;
        int kills_ = 0;
        bool died_ = false;
        bool lastFired_ = false;
        int lastKills_ = 0;
    };
}
