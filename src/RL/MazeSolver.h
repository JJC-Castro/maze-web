#pragma once
// ---------------------------------------------------------------------------
// MazeSolver.h : BFS distance field over the maze (the "known solvable path").
//
// The maze generator produces a perfect maze (recursive backtracker, no loops),
// so the unique path from any cell to the exit is exactly the descending
// gradient of this field. dist == -1 means unreachable (walls / outside).
// ---------------------------------------------------------------------------
#include <string>
#include <vector>

#include "RLSpec.h"

class Maze;

namespace rl
{
    // 把游戏 Maze.h 的 CellType 归一到 RL 的 6 字母表（'W.SAHE'）。
    // 后来又加了 SLOW_ZONE（减速带）和 MINE（地雷），两者都是可走地面：
    // 在 RL 眼里一律当通路(PATH)，训练好的权重含义不变，机器人也不会
    // 把减速带 / 地雷误当成墙去绕路。
    int NormalizeCell(int rawCell);
    class DistField
    {
    public:
        bool Build(const Maze& maze);

        // Same field but built from a raw copy of the grid (CellType values),
        // so a worker thread can own its own maze snapshot without touching
        // the Maze object the render thread is using.
        bool BuildFromCells(const int* cells, int width, int height,
                            int exitGx, int exitGy);

        int Width() const { return width_; }
        int Height() const { return height_; }
        int ExitGx() const { return exitGx_; }
        int ExitGy() const { return exitGy_; }
        int MaxDist() const { return maxDist_; }

        // -1 when unreachable / out of bounds
        int At(int gx, int gy) const;
        int Raw(int gx, int gy) const;              // no bounds check
        bool InBounds(int gx, int gy) const;
        int CellAt(int gx, int gy) const;           // CellType value, -1 if OOB
        bool WalkableAt(int gx, int gy) const;

        // Overwrite one cell in place (a treasure that was just picked up
        // becomes PATH). cells_ and rows_ stay in sync; the distance field is
        // deliberately left alone -- treasures only ever sit on walkable,
        // reachable cells, so removing one can never change the topology.
        bool SetCell(int gx, int gy, int cellType);

        // Steps of the optimal path from (gx, gy) to the exit; -1 if unreachable.
        int OptimalFrom(int gx, int gy) const { return At(gx, gy); }

        // Row of characters in the same 'W.SAHE' alphabet the save files use.
        const std::vector<std::string>& Rows() const { return rows_; }
        bool Ready() const { return ready_; }

    private:
        int width_ = 0;
        int height_ = 0;
        int exitGx_ = 0;
        int exitGy_ = 0;
        int maxDist_ = 0;
        bool ready_ = false;
        std::vector<int> dist_;                     // height * width
        std::vector<int> cells_;                    // CellType as int
        std::vector<std::string> rows_;
    };
}