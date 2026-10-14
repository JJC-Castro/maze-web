#pragma once
// ---------------------------------------------------------------------------
// RlWalker.h : drives the 3D first-person Player from grid-level actions.
//
// The RL brain (RlAgent) decides in *cells*; this class turns a target cell
// into continuous movement: it steers the player's yaw at the target point and
// pushes forward through Player::UpdateAutoMove, which reuses the game's own
// wall collision + gravity. Nothing here bypasses the physics.
//
// v3: the target point carries a lateral *lane* offset in metres, so the robot
// can hug one side of a 1-cell corridor instead of always walking the centre
// line (that is what makes "walk past the enemy on the side" possible).
// HoldFor() freezes the walker for one decision cycle after a fire action, so
// a shot costs the same 0.5 s the trainer charges for it.
// ---------------------------------------------------------------------------
#include "raylib.h"
#include "RLSpec.h"

class Maze;
class Player;

namespace rl
{
    class RlWalker
    {
    public:
        void Reset(const Maze& maze, int gx, int gy);   // snap to a cell centre
        // offX / offZ: lane offset in world metres, applied to the cell centre
        // (see rl::LanePerpX/LanePerpY * kLaneOffsets).
        void SetTarget(int gx, int gy, float offX = 0.0f, float offZ = 0.0f);
        void ClearTarget() { hasTarget_ = false; }
        // Stay put for `seconds` (fire action = one walk step).
        void HoldFor(float seconds) { if (seconds > hold_) hold_ = seconds; }
        void ClearHold() { hold_ = 0.0f; }
        bool HasTarget() const { return hasTarget_; }
        int  TargetGx() const { return tgx_; }
        int  TargetGy() const { return tgy_; }

        // Steer toward the target. Returns true on the frame the player has
        // entered the target cell (caller should then pick the next action).
        // sprint=true holds Shift for this leg: same 8 m/s / stamina rules the
        // keyboard player gets, so the policy learns real sprinting.
        bool Drive(Player& player, const Maze& maze, float dt, bool sprint = false);

        // Place the player exactly at the centre of a cell (episode reset).
        void PlaceAt(Player& player, const Maze& maze, int gx, int gy) const;

    private:
        bool hasTarget_ = false;
        int tgx_ = 0;
        int tgy_ = 0;
        float offX_ = 0.0f;
        float offZ_ = 0.0f;
        float hold_ = 0.0f;

        // Corner-wedge recovery: when the same target cell cannot be reached
        // for kStallRecoverSeconds the walker re-centres the player on its own
        // cell axis (see RlWalker.cpp).
        float stall_ = 0.0f;
        int lastTgx_ = -1;
        int lastTgy_ = -1;
    };
}