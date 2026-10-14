#include "RlBridge.h"

// Winsock first, and windows.h tamed exactly like UIFont.cpp does: without
// NOUSER / NOGDI, winuser.h and wingdi.h get pulled in and clash with raylib
// (DrawTextEx macro, Rectangle type). Maze.h -> raylib.h is included after.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOUSER
#define NOUSER
#endif
#ifndef NOGDI
#define NOGDI
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "../Maze.h"
#include "RlAgent.h"
#include "RlTelemetry.h"

#pragma comment(lib, "ws2_32.lib")

namespace rl
{
    namespace
    {
        // ------------------------------------------------------------ line IO
        class LineChannel
        {
        public:
            explicit LineChannel(SOCKET s) : s_(s) {}

            bool SendLine(const std::string& line)
            {
                std::string out = line;
                out.push_back('\n');
                size_t sent = 0;
                while (sent < out.size())
                {
                    const int n = ::send(s_, out.data() + sent,
                                         (int)(out.size() - sent), 0);
                    if (n <= 0) return false;
                    sent += (size_t)n;
                }
                return true;
            }

            // Returns false on disconnect. Line must not exceed 64 KiB.
            bool RecvLine(std::string* line)
            {
                line->clear();
                for (;;)
                {
                    const size_t pos = buf_.find('\n', scanPos_);
                    if (pos != std::string::npos)
                    {
                        *line = buf_.substr(0, pos);
                        buf_.erase(0, pos + 1);
                        scanPos_ = 0;
                        if (!line->empty() && line->back() == '\r') line->pop_back();
                        return true;
                    }
                    scanPos_ = buf_.size();
                    char chunk[4096];
                    const int n = ::recv(s_, chunk, (int)sizeof(chunk), 0);
                    if (n <= 0) return false;
                    buf_.append(chunk, (size_t)n);
                    if (buf_.size() > 65536) return false;
                }
            }

        private:
            SOCKET s_ = INVALID_SOCKET;
            std::string buf_;
            size_t scanPos_ = 0;
        };

        std::string FormatFloat(float v)
        {
            char b[32];
            std::snprintf(b, sizeof(b), "%.6g", (double)v);
            return std::string(b);
        }

        void EnableNodelay(SOCKET s)
        {
            BOOL one = TRUE;
            ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
        }
    }

    RlBridge::~RlBridge() { Stop(); }

    bool RlBridge::Start(int port, std::string* errOut)
    {
        if (running_.load()) return true;
        stopRequested_ = false;
        running_ = true;
        worker_ = std::thread(&RlBridge::WorkerLoop, this, port);

        // Give the listener a moment so Start() can report bind failures.
        for (int i = 0; i < 50; ++i)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (errOut)
            {
                std::lock_guard<std::mutex> lock(viewMutex_);
                if (view_.status.rfind("error:", 0) == 0) { *errOut = view_.status; return false; }
            }
            if (!errOut) break;
        }
        return true;
    }

    void RlBridge::Stop()
    {
        if (!running_.load() && !worker_.joinable()) return;
        stopRequested_ = true;
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        running_ = false;
    }

    bool RlBridge::TakeResetRequest()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!resetRequested_) return false;
        resetRequested_ = false;
        return true;
    }

    void RlBridge::PublishMaze(const Maze& maze)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const int w = maze.grid.width;
            const int h = maze.grid.height;
            pubWidth_ = w;
            pubHeight_ = h;
            pubEntryGx_ = maze.entryGx;
            pubEntryGy_ = maze.entryGy;
            pubExitGx_ = maze.exitGx;
            pubExitGy_ = maze.exitGy;
            pubCells_.assign((size_t)w * (size_t)h, 0);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    pubCells_[(size_t)y * w + x] = NormalizeCell((int)maze.grid.at(x, y));
            ++publishedSeq_;
        }
        cv_.notify_all();
    }

    BridgeView RlBridge::Snapshot() const
    {
        std::lock_guard<std::mutex> lock(viewMutex_);
        return view_;
    }

    // -----------------------------------------------------------------------
    void RlBridge::WorkerLoop(int port)
    {
        const auto setStatus = [&](const std::string& s) {
            std::lock_guard<std::mutex> lock(viewMutex_);
            view_.status = s;
        };
        const auto setConnected = [&](bool c) {
            std::lock_guard<std::mutex> lock(viewMutex_);
            view_.connected = c;
        };

        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            setStatus("error: WSAStartup failed");
            running_ = false;
            return;
        }

        SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET)
        {
            setStatus("error: socket() failed");
            WSACleanup();
            running_ = false;
            return;
        }

        const BOOL yes = TRUE;
        ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

        if (::bind(listener, (sockaddr*)&addr, sizeof(addr)) != 0 ||
            ::listen(listener, 1) != 0)
        {
            char b[128];
            std::snprintf(b, sizeof(b), "error: bind/listen failed on port %d", port);
            setStatus(b);
            ::closesocket(listener);
            WSACleanup();
            running_ = false;
            return;
        }

        char ready[128];
        std::snprintf(ready, sizeof(ready), "listening on 127.0.0.1:%d", port);
        setStatus(ready);
        terminal::SetMode("bridge");
        terminal::Note("bridge listening 127.0.0.1:%d, waiting for python trainer", port);

        while (!stopRequested_.load())
        {
            // Non-blocking accept with a short poll so Stop() stays responsive.
            fd_set set;
            FD_ZERO(&set);
            FD_SET(listener, &set);
            timeval tv{ 0, 200000 };
            const int sel = ::select(0, &set, nullptr, nullptr, &tv);
            if (sel <= 0) continue;

            SOCKET client = ::accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            EnableNodelay(client);
            setConnected(true);
            setStatus("connected: training session active");
            terminal::Note("bridge client connected");

            LineChannel chan(client);
            RlAgent agent;

            {
                std::ostringstream hs;
                hs << "HELLO " << kProtocolVersion << " " << kObsDim << " " << kNActions;
                chan.SendLine(hs.str());
            }

            int episode = 0;
            double epReturn = 0.0;
            std::chrono::steady_clock::time_point episodeT0 = std::chrono::steady_clock::now();

            bool alive = true;
            while (alive && !stopRequested_.load())
            {
                // ---- obtain a maze (request one from the game thread) ----
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (publishedSeq_ == servedSeq_)
                    {
                        resetRequested_ = true;
                        cv_.wait_for(lock, std::chrono::milliseconds(500));
                        if (publishedSeq_ == servedSeq_)
                        {
                            // Try again; also re-post the request so the game
                            // thread notices it on the next frame.
                            resetRequested_ = true;
                            continue;
                        }
                    }
                    agent.BindCells(pubCells_, pubWidth_, pubHeight_,
                                    pubEntryGx_, pubEntryGy_,
                                    pubExitGx_, pubExitGy_);
                    ++servedSeq_;

                    // New episode: the trainer asked for a fresh maze.
                    ++episode;
                    epReturn = 0.0;
                    episodeT0 = std::chrono::steady_clock::now();
                    terminal::Reset(episode, pubWidth_, pubHeight_,
                                    pubEntryGx_, pubEntryGy_,
                                    pubExitGx_, pubExitGy_,
                                    agent.OptimalSteps(), agent.MaxSteps());
                }
                if (!agent.Ready()) { continue; }

                // ---- MAP + first observation ----
                {
                    std::ostringstream map;
                    map << "MAP " << agent.Field().Width() << " "
                        << agent.Field().Height() << " "
                        << agent.EntryGx() << " " << agent.EntryGy() << " "
                        << agent.Field().ExitGx() << " " << agent.Field().ExitGy() << " "
                        << agent.MaxSteps() << " " << agent.OptimalSteps();
                    for (const std::string& row : agent.Field().Rows())
                        map << " " << row;
                    if (!chan.SendLine(map.str())) break;
                }

                float obs[kObsDim] = { 0.0f };
                agent.EncodeObs(obs);
                {
                    std::string line = "OBS";
                    for (int i = 0; i < kObsDim; ++i)
                    {
                        line.push_back(' ');
                        line += FormatFloat(obs[i]);
                    }
                    if (!chan.SendLine(line)) break;
                }

                {
                    std::lock_guard<std::mutex> viewLock(viewMutex_);
                    view_.dispGx = agent.Gx();
                    view_.dispGy = agent.Gy();
                    view_.mazeWidth = agent.Field().Width();
                    view_.mazeHeight = agent.Field().Height();
                    view_.steps = 0;
                    view_.maxSteps = agent.MaxSteps();
                    view_.optimalSteps = agent.OptimalSteps();
                    view_.done = false;
                    view_.success = false;
                }

                // ---- action / reset loop ----
                const auto t0 = std::chrono::steady_clock::now();
                uint64_t windowSteps = 0;
                bool needNewMaze = false;

                while (!needNewMaze && !stopRequested_.load())
                {
                    std::string msg;
                    if (!chan.RecvLine(&msg)) { alive = false; break; }

                    if (msg == "BYE")
                    {
                        alive = false;
                        break;
                    }
                    if (msg == "RESET")
                    {
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            resetRequested_ = true;
                        }
                        cv_.notify_all();
                        needNewMaze = true;
                        break;
                    }
                    int action = 0;
                    if (sscanf_s(msg.c_str(), "ACT %d", &action) != 1) continue;
                    if (action < 0 || action >= kNActions) action = 0;

                    bool illegal = false, success = false;
                    const float reward = agent.ApplyAction(action, &illegal, &success);
                    ++windowSteps;
                    epReturn += (double)reward;

                    {
                        std::string line = "REWARD ";
                        line += FormatFloat(reward);
                        line += agent.Done() ? " 1 " : " 0 ";
                        line += success ? "1 " : "0 ";
                        line += std::to_string(agent.Gx()) + " " + std::to_string(agent.Gy());
                        if (!chan.SendLine(line)) { alive = false; break; }
                    }

                    agent.EncodeObs(obs);
                    {
                        std::string line = "OBS";
                        for (int i = 0; i < kObsDim; ++i)
                        {
                            line.push_back(' ');
                            line += FormatFloat(obs[i]);
                        }
                        if (!chan.SendLine(line)) { alive = false; break; }
                    }

                    // Sampling export to the terminal: training runs thousands
                    // of steps per second, logging every step would flood it.
                    if (agent.Steps() % terminal::kBridgeStepInterval == 0)
                        terminal::Step(episode, agent.Steps(), agent.MaxSteps(), action,
                                       agent.Gx(), agent.Gy(),
                                       agent.Field().At(agent.Gx(), agent.Gy()),
                                       reward, epReturn, agent.Done(), success);

                    // ---- publish view / episode bookkeeping ----
                    {
                        std::lock_guard<std::mutex> lock(viewMutex_);
                        view_.dispGx = agent.Gx();
                        view_.dispGy = agent.Gy();
                        view_.lastAction = action;
                        view_.lastReward = reward;
                        view_.steps = agent.Steps();
                        view_.maxSteps = agent.MaxSteps();
                        view_.optimalSteps = agent.OptimalSteps();
                        view_.done = agent.Done();
                        view_.success = agent.Success();
                        view_.totalSteps += 1;
                        if (agent.Done()) view_.episodes += 1;
                        const auto now = std::chrono::steady_clock::now();
                        const double secs = std::chrono::duration<double>(now - t0).count();
                        if (secs > 0.25)
                            view_.stepsPerSecond = (double)windowSteps / secs;
                    }

                    if (agent.Done())
                    {
                        const std::chrono::duration<double> elapsed =
                            std::chrono::steady_clock::now() - episodeT0;
                        const double secs = elapsed.count();
                        terminal::EpisodeEnd(episode, agent.Steps(),
                                             agent.OptimalSteps(), epReturn, success,
                                             (secs > 1e-6)
                                                 ? (double)agent.Steps() / secs : 0.0);
                        // Wait for the trainer to send RESET (it may want to
                        // read the terminal observation first).
                        continue;
                    }
                }
            }

            ::closesocket(client);
            setConnected(false);
            setStatus("client disconnected");
            terminal::Note("bridge client disconnected after %d episodes", episode);
        }

        ::closesocket(listener);
        WSACleanup();
        running_ = false;
    }
}