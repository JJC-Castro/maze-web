#include "RlWalker.h"

#include <cmath>

#include "../Maze.h"
#include "../Player.h"
#include "RlTelemetry.h"
#include "raymath.h"

namespace rl
{
    namespace
    {
        constexpr float kLevelRate = 2.5f;            // rad/s, stand-up speed
        constexpr float kStallRecoverSeconds = 1.5f;  // re-centre after this

        // Arrival tolerance around the target cell centre. A leg must really be
        // a full cell (2 m) long: if we accepted "entered the target cell" the
        // robot would only cover ~1 m per decision and its real pace would be
        // twice the player's, while the reward model pays 0.5 s / cell (walk)
        // or 0.25 s / cell (sprint). Keep this a bit above one frame of travel
        // at 8 m/s (0.13 m @60fps, 0.27 m @30fps).
        constexpr float kArriveRadius = 0.35f;

        // Single-point probe: is there a wall just ahead of the player?
        bool BlockedAhead(const Player& player, const Maze& maze,
                          Vector3 feet, Vector3 dir)
        {
            const float reach = player.halfWidth + 0.08f;
            return maze.IsWorldPosBlocked(Vector3{ feet.x + dir.x * reach,
                                                    feet.y,
                                                    feet.z + dir.z * reach });
        }
    }
    void RlWalker::PlaceAt(Player& player, const Maze& maze, int gx, int gy) const
    {
        const Vector3 centre = maze.GridToWorld(gx, gy);
        player.SetFeetPosition(Vector3{ centre.x, GROUND_Y, centre.z });
        player.SetVelocity(Vector3{ 0.0f, 0.0f, 0.0f });
    }

    void RlWalker::Reset(const Maze& maze, int gx, int gy)
    {
        SetTarget(gx, gy);          // centre lane
        stall_ = 0.0f;
        hold_ = 0.0f;
        lastTgx_ = lastTgy_ = -1;
    }

    void RlWalker::SetTarget(int gx, int gy, float offX, float offZ)
    {
        tgx_ = gx;
        tgy_ = gy;
        offX_ = offX;
        offZ_ = offZ;
        hasTarget_ = true;
    }

    bool RlWalker::Drive(Player& player, const Maze& maze, float dt, bool sprint)
    {
        if (!hasTarget_) return false;

        // Stand up from the randomized spawn pose: pitch/roll ease back to 0.
        // Horizontal motion never depends on the pose, so this is cosmetic.
        player.LevelOut(dt, kLevelRate);

        // Fire action: hold still for one decision cycle (0.5 s) so the step
        // costs exactly what spec.FIRE_STEP_TIME charges.
        if (hold_ > 0.0f)
        {
            hold_ -= dt;
            if (hold_ > 0.0f) return false;
        }

        const Vector3 cellCentre = maze.GridToWorld(tgx_, tgy_);
        const Vector3 centre = Vector3{ cellCentre.x + offX_,
                                       cellCentre.y,
                                       cellCentre.z + offZ_ };
        Vector3 feet = player.GetFeetPosition();

        // ---- corner-wedge recovery ----
        // Same target for too long: drop the lateral drift by re-centring on the
        // current cell axis. Cheap, safe (that cell is walkable) and guarantees
        // a drive can never stall forever, whatever the spawn pose was.
        if (tgx_ == lastTgx_ && tgy_ == lastTgy_) stall_ += dt;
        else { stall_ = 0.0f; lastTgx_ = tgx_; lastTgy_ = tgy_; }
        if (stall_ >= kStallRecoverSeconds)
        {
            int cgx = 0, cgy = 0;
            maze.WorldToGrid(feet, cgx, cgy);
            const Vector3 own = maze.GridToWorld(cgx, cgy);
            player.SetFeetPosition(Vector3{ own.x, feet.y, own.z });
            feet = player.GetFeetPosition();
            stall_ = 0.0f;
            offX_ = offZ_ = 0.0f;   // drop the lane offset too, or we re-wedge
            terminal::Note("walker re-center cell=%d,%d target=%d,%d",
                           cgx, cgy, tgx_, tgy_);
        }

        const float dx = centre.x - feet.x;
        const float dz = centre.z - feet.z;

        // Arrived: inside the target cell *and* on its centre (see
        // kArriveRadius). Snapping the last few centimetres keeps the motion
        // axis-aligned and exact, so the game's speed matches the player's.
        int cgx = 0, cgy = 0;
        maze.WorldToGrid(feet, cgx, cgy);
        if (cgx == tgx_ && cgy == tgy_ &&
            std::sqrt(dx * dx + dz * dz) <= kArriveRadius)
        {
            player.SetFeetPosition(Vector3{ centre.x, feet.y, centre.z });
            stall_ = 0.0f;
            return true;
        }

        // Single-axis motion only. A diagonal push hits both wall faces at once
        // and the AABB wedges on the corner - that is why an off-axis spawn used
        // to freeze the robot. Advance first, use the other axis to escape.
        Vector3 dir = { 0.0f, 0.0f, 0.0f };
        if (std::fabs(dx) > 1e-4f || std::fabs(dz) > 1e-4f)
        {
            // Dogleg: close the lateral gap *inside the current cell* (the
            // diagonal a real player gets from holding W+D), then run straight
            // along the corridor axis at the target lane. Two reasons:
            //   1. the offset must be in place *before* the robot crosses into
            //      the next cell - the bite test is an AABB overlap (0.65 m), so
            //      entering the enemy cell on the centre line, or on a diagonal
            //      still near it, costs a bite whatever lane was picked;
            //   2. it also has to work for the *zero* lane: after hugging a wall
            //      the next "walk the centre" target is 0.68 m off the player's
            //      line, so a pure axial advance can never reach it (arrival needs
            //      0.35 m) and the leg would die in the 1.5 s stall recovery.
            int cgx2 = 0, cgy2 = 0;
            maze.WorldToGrid(feet, cgx2, cgy2);
            Vector3 axis{ 0.0f, 0.0f, 0.0f };
            if (tgx_ != cgx2)      axis.x = (tgx_ > cgx2) ? 1.0f : -1.0f;
            else if (tgy_ != cgy2) axis.z = (tgy_ > cgy2) ? 1.0f : -1.0f;

            if (axis.x == 0.0f && axis.z == 0.0f)
            {
                // Target is our own cell (hold / re-centre): straight run to it.
                const float len = std::sqrt(dx * dx + dz * dz);
                if (len > 1e-4f) dir = Vector3{ dx / len, 0.0f, dz / len };
            }
            else
            {
                const Vector3 own = maze.GridToWorld(cgx2, cgy2);
                const Vector3 lat{ -axis.z, 0.0f, axis.x };   // perpendicular
                const float latNow  = (feet.x - own.x) * lat.x
                                    + (feet.z - own.z) * lat.z;
                const float latWant = offX_ * lat.x + offZ_ * lat.z;
                const float err = latWant - latNow;
                const float s = (err > 0.0f) ? 1.0f : -1.0f;
                if (std::fabs(err) > 0.02f)
                {
                    // Diagonal while the lateral error is still open (mixed with
                    // the advance, like a player holding W+D).
                    dir = Vector3{ axis.x + lat.x * s, 0.0f, axis.z + lat.z * s };
                }
                else if (!BlockedAhead(player, maze, feet, axis))
                {
                    dir = axis;
                }
                else
                {
                    // Corner / off-axis spawn: let the lateral axis break the wedge.
                    dir = !BlockedAhead(player, maze, feet, lat)
                        ? lat : Vector3{ -lat.x, 0.0f, -lat.z };
                }
            }
        }

        player.UpdateAutoMove(dt, maze, dir, sprint);
        return false;
    }
}