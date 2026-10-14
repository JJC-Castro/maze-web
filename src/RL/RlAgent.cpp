#include "RlAgent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../Maze.h"
#include "RLNet.h"

namespace rl
{
    bool RlAgent::BindMaze(const Maze& maze)
    {
        if (!field_.Build(maze)) return false;
        entryGx_ = maze.entryGx;
        entryGy_ = maze.entryGy;
        maxHp_ = MaxHealthForSize(field_.Width());
        ammoMax_ = AttackLimitForSize(field_.Width());
        ResetToEntry();
        return true;
    }

    bool RlAgent::BindCells(const std::vector<int>& cells, int width, int height,
                            int entryGx, int entryGy, int exitGx, int exitGy)
    {
        if (cells.size() < (size_t)width * (size_t)height) return false;
        if (!field_.BuildFromCells(cells.data(), width, height, exitGx, exitGy))
            return false;
        entryGx_ = entryGx;
        entryGy_ = entryGy;
        maxHp_ = MaxHealthForSize(width);
        ammoMax_ = AttackLimitForSize(width);
        ResetToEntry();
        return true;
    }

    void RlAgent::ResetToEntry()
    {
        gx_ = entryGx_;
        gy_ = entryGy_;
        steps_ = 0;
        lastAction_ = -1;
        lastReward_ = 0.0f;
        done_ = false;
        success_ = false;
        illegalCount_ = 0;
        pendingHits_ = 0;
        pendingKills_ = 0;
        enemyHits_ = 0;
        treasuresCollected_ = 0;
        seconds_ = 0.0;
        hp_ = maxHp_;
        ammo_ = ammoMax_;
        kills_ = 0;
        died_ = false;
        lastFired_ = false;
        lastKills_ = 0;
        staminaRatio_ = 1.0f;
        sprinting_ = false;
        lastSprinting_ = false;
        lastDt_ = kCellSize / kWalkSpeed;
        optimal_ = std::max(0, field_.At(entryGx_, entryGy_));
        maxSteps_ = MaxStepsFor(optimal_);
    }

    void RlAgent::SetCell(int gx, int gy)
    {
        if (!field_.InBounds(gx, gy)) return;
        gx_ = gx;
        gy_ = gy;
        steps_ = 0;
        lastAction_ = -1;
        lastReward_ = 0.0f;
        done_ = false;
        success_ = false;
        illegalCount_ = 0;
        hp_ = maxHp_;
        ammo_ = ammoMax_;
        kills_ = 0;
        died_ = false;
        pendingHits_ = 0;
        pendingKills_ = 0;
        lastFired_ = false;
        lastKills_ = 0;
    }

    void RlAgent::SetEnemyGridPositions(const std::vector<float>& flatGxGy)
    {
        enemyGrid_ = flatGxGy;
    }

    void RlAgent::SetHealth(int hp, int maxHp)
    {
        if (maxHp > 0) maxHp_ = maxHp;
        hp_ = std::max(0, std::min(hp, maxHp_));
    }

    void RlAgent::SetAmmo(int ammo, int limit)
    {
        if (limit > 0) ammoMax_ = limit;
        ammo_ = std::max(0, ammo);
    }

    void RlAgent::SetSprintState(float staminaRatio, bool sprinting)
    {
        staminaRatio_ = std::max(0.0f, std::min(1.0f, staminaRatio));
        sprinting_ = sprinting;
    }

    void RlAgent::NotifyEnemyHit(int times)
    {
        if (times > 0) pendingHits_ += times;
    }

    void RlAgent::NotifyKill(int times)
    {
        if (times > 0) pendingKills_ += times;
    }

    int RlAgent::MaxStepsFor(int optimalSteps)
    {
        return std::max(kMinMaxSteps, optimalSteps * kMaxStepFactor);
    }

    void RlAgent::EncodeObs(float* out) const
    {
        const int w = field_.Width();
        const int h = field_.Height();
        const float invMax = 1.0f / (float)(field_.MaxDist() > 0 ? field_.MaxDist() : 1);
        const int plane = kObsWindow * kObsWindow;
        const int ownRaw = field_.At(gx_, gy_);
        const int own = (ownRaw < 0) ? 0 : ownRaw;

        for (int i = 0; i < kObsDim; ++i) out[i] = 0.0f;

        for (int wy = 0; wy < kObsWindow; ++wy)
        {
            const int cy = gy_ + wy - kObsHalf;
            for (int wx = 0; wx < kObsWindow; ++wx)
            {
                const int cx = gx_ + wx - kObsHalf;
                const int base = wy * kObsWindow + wx;

                if (cx < 0 || cy < 0 || cx >= w || cy >= h)
                {
                    out[kDistPlane * plane + base] = 1.0f;   // outside == far away
                    continue;
                }
                const int c = field_.CellAt(cx, cy);
                if (Walkable(c)) out[base] = 1.0f;
                if (IsExitCell(c)) out[plane + base] = 1.0f;
                if (IsTreasureCell(c)) out[2 * plane + base] = 1.0f;

                const int cd = field_.At(cx, cy);
                if (cd < 0)
                {
                    out[kDistPlane * plane + base] = 1.0f;   // wall / outside
                }
                else
                {
                    const float rel = (float)(cd - own) / kDistScale;
                    out[kDistPlane * plane + base] =
                        std::max(-1.0f, std::min(1.0f, rel));
                }
            }
        }

        // ---- enemy plane + nearest-enemy scalar ----
        float nearestEnemy = -1.0f;          // < 0 == no enemy known
        for (size_t i = 0; i + 1 < enemyGrid_.size(); i += 2)
        {
            const float fx = enemyGrid_[i];
            const float fy = enemyGrid_[i + 1];
            const int ex = (int)std::floor(fx) - gx_;
            const int ey = (int)std::floor(fy) - gy_;
            if (ex >= -kObsHalf && ex <= kObsHalf &&
                ey >= -kObsHalf && ey <= kObsHalf)
            {
                const int wi = ey + kObsHalf;
                const int wj = ex + kObsHalf;
                out[kEnemyPlane * plane + wi * kObsWindow + wj] = 1.0f;
            }
            // spec.encode_obs measures from the player's *cell centre*
            const float ddx = fx - ((float)gx_ + 0.5f);
            const float ddy = fy - ((float)gy_ + 0.5f);
            const float dm = std::sqrt(ddx * ddx + ddy * ddy) * kCellSize;
            if (nearestEnemy < 0.0f || dm < nearestEnemy) nearestEnemy = dm;
        }

        const float spanX = (float)std::max(1, w - 1);
        const float spanY = (float)std::max(1, h - 1);
        out[kObsWindowDim + 0] = (float)(field_.ExitGx() - gx_) / spanX;
        out[kObsWindowDim + 1] = (float)(field_.ExitGy() - gy_) / spanY;

        const int d = field_.At(gx_, gy_);
        out[kObsWindowDim + 2] = (d < 0) ? 1.0f
            : std::min(1.0f, (float)d * invMax);
        out[kObsWindowDim + 3] = staminaRatio_;
        out[kObsWindowDim + 4] = sprinting_ ? 1.0f : 0.0f;
        out[kObsWindowDim + kObsHp] = HealthRatio();
        out[kObsWindowDim + kObsEnemyDist] =
            (nearestEnemy < 0.0f) ? 1.0f
            : std::min(1.0f, nearestEnemy / kFireRange);
        out[kObsWindowDim + kObsAmmo] =
            std::max(0.0f, std::min(1.0f, AmmoRatio()));
    }

    int RlAgent::GreedyAction() const
    {
        const int d = field_.At(gx_, gy_);
        int best = -1;
        int bestDist = 0;
        for (int dir = 0; dir < kNDirs; ++dir)
        {
            const int nd = field_.At(gx_ + kDirDx[dir], gy_ + kDirDy[dir]);
            if (nd < 0) continue;                       // wall / unreachable
            if (best < 0 || nd < bestDist) { best = dir; bestDist = nd; }
        }
        if (best < 0) return 0;
        (void)d;
        return best * 2;                                // walk, never sprint
    }

    int RlAgent::PolicyAction(float* outValue, float* outLogits) const
    {
        if (!net_ || !net_->Ready()) return GreedyAction();
        float obs[kObsDim] = { 0.0f };
        EncodeObs(obs);
        return net_->Argmax(obs, outValue, outLogits);
    }

    float RlAgent::ScoreTransition(int prevDist, int newDist, bool illegal,
                                   bool success, float dt, int treasures,
                                   int enemyHits, bool fired, bool wasted, int kills,
                                   bool died)
    {
        float r = kRTimePerSec * dt
            + kRTreasure * (float)treasures
            + kREnemyHit * (float)enemyHits;
        if (fired) r += kRFire;
        if (wasted) r += kRFireWaste;
        if (kills > 0) r += kRKill * (float)kills;
        if (died) return r + kRDeath;
        if (illegal) return r + kRIllegal;
        if (success) return r + kRExit;
        if (prevDist < 0 || newDist < 0) return r - kRProgress;
        return r + kRProgress * (float)(prevDist - newDist);
    }

    float RlAgent::ApplyAction(int action, bool* outIllegal, bool* outSuccess)
    {
        if (action < 0 || action >= kNActions) action = 0;
        const bool fire = ActIsFire(action);
        const int dir = ActDir(action);

        const int prevDist = field_.At(gx_, gy_);
        int nx = gx_, ny = gy_;
        if (!fire)                                // firing does not move
        {
            nx += kDirDx[dir];
            ny += kDirDy[dir];
        }
        const bool illegal = !fire && !field_.WalkableAt(nx, ny);

        if (!illegal) { gx_ = nx; gy_ = ny; }
        const int newDist = field_.At(gx_, gy_);
        const bool success = (newDist == 0);

        // ---- treasure pickup (mirror of the game's Treasure::Collect) ----
        int treasures = 0;
        if (!illegal && !fire && IsTreasureCell(field_.CellAt(gx_, gy_)))
        {
            const int cellType = field_.CellAt(gx_, gy_);
            treasures = 1;
            if (cellType == kCellTreasureHeal)
                hp_ = std::min(maxHp_, hp_ + kTreasureHealAmount);
            else if (cellType == kCellTreasureAttack)
                ammo_ += kTreasureAmmoAmount;
            field_.SetCell(gx_, gy_, 1);            // CellType::PATH
            ++treasuresCollected_;
        }

        // ---- firing: 1 ammo per shot, no hit resolution here (the live game
        // owns the orb, the trainer owns its own damage model) ----
        const bool fired = fire && (ammo_ > 0);
        const bool wasted = fire && !fired;      // 没弹还按开火：空放
        if (fired) --ammo_;

        // ---- time cost comes from what the player really did ----
        lastSprinting_ = !fire && sprinting_;
        lastDt_ = fire ? kFireStepTime
                       : (sprinting_ ? (kCellSize / kSprintSpeed)
                                     : (kCellSize / kWalkSpeed));
        seconds_ += lastDt_;

        const int hits = pendingHits_;
        pendingHits_ = 0;
        if (hits > 0)
        {
            enemyHits_ += hits;
            hp_ = std::max(0, hp_ - kEnemyBiteDamage * hits);
        }
        const int kills = pendingKills_;
        pendingKills_ = 0;
        if (kills > 0) kills_ += kills;

        died_ = (hp_ <= 0);
        lastFired_ = fired;
        lastKills_ = kills;

        lastAction_ = action;
        lastReward_ = ScoreTransition(prevDist, newDist, illegal, success,
                                      lastDt_, treasures, hits, fired, wasted, kills,
                                      died_);
        ++steps_;
        if (illegal) ++illegalCount_;
        success_ = success && !died_;
        done_ = success_ || died_ || steps_ >= maxSteps_;

        if (outIllegal) *outIllegal = illegal;
        if (outSuccess) *outSuccess = success;
        return lastReward_;
    }

    std::string RlAgent::Describe() const
    {
        char buf[320];
        std::snprintf(buf, sizeof(buf),
            "cell(%d,%d) steps %d/%d optimal %d dist %d done %d success %d "
            "illegal %d treas %d hits %d kills %d hp %d/%d ammo %d died %d %.1fs",
            gx_, gy_, steps_, maxSteps_, optimal_, field_.At(gx_, gy_),
            done_ ? 1 : 0, success_ ? 1 : 0, illegalCount_,
            treasuresCollected_, enemyHits_, kills_, hp_, maxHp_, ammo_,
            died_ ? 1 : 0, seconds_);
        return std::string(buf);
    }
}
