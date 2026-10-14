#pragma once
// ---------------------------------------------------------------------------
// RlBridge.h : TCP bridge that lets the Python PPO trainer use THIS game as
// the environment (see rl/docs/04_BRIDGE_PROTOCOL.md).
//
// Roles:
//   - the worker thread owns the socket and runs the grid MDP (RlAgent) as
//     fast as Python can ask for actions, completely decoupled from rendering
//   - the game thread owns the Maze object, so whenever the worker needs a
//     fresh maze it posts a request and the game thread regenerates one and
//     hands over a plain copy of the cells
//
// ASCII protocol, one message per line:
//   game -> python : HELLO / MAP / OBS / REWARD
//   python -> game : ACT <i> / RESET / BYE
// ---------------------------------------------------------------------------
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "RLSpec.h"

class Maze;

namespace rl
{
    // Snapshot the render thread reads every frame.
    struct BridgeView
    {
        bool connected = false;
        std::string status = "idle";
        int dispGx = 1, dispGy = 1;
        int lastAction = -1;
        float lastReward = 0.0f;
        int steps = 0, maxSteps = 0, optimalSteps = 0;
        bool done = false, success = false;
        uint64_t episodes = 0;
        double meanReturn = 0.0;
        double bestReturn = 0.0;
        double stepsPerSecond = 0.0;
        uint64_t totalSteps = 0;
        int mazeWidth = 0, mazeHeight = 0;
    };

    class RlBridge
    {
    public:
        RlBridge() = default;
        ~RlBridge();

        bool Start(int port = kDefaultPort, std::string* errOut = nullptr);
        void Stop();
        bool Running() const { return running_.load(); }

        // ---- render thread side ----
        // True once per requested maze; the caller must then regenerate the
        // maze and call PublishMaze().
        bool TakeResetRequest();
        void PublishMaze(const Maze& maze);

        BridgeView Snapshot() const;

    private:
        void WorkerLoop(int port);

        // request / publish mailbox
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        bool resetRequested_ = false;
        uint64_t publishedSeq_ = 0;
        uint64_t servedSeq_ = 0;
        int pubWidth_ = 0, pubHeight_ = 0;
        int pubEntryGx_ = 1, pubEntryGy_ = 1;
        int pubExitGx_ = 0, pubExitGy_ = 0;
        std::vector<int> pubCells_;

        // view state
        mutable std::mutex viewMutex_;
        BridgeView view_;

        std::atomic<bool> running_{ false };
        std::atomic<bool> stopRequested_{ false };
        std::thread worker_;
    };
}