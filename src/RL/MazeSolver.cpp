#include "MazeSolver.h"

#include <algorithm>
#include <deque>
#include <string>

#include "../Maze.h"

namespace rl
{
    // Same alphabet the save files use ('W.SAHE'); index == CellType value.
    static const char kCellChar[] = { 'W', '.', 'S', 'A', 'H', 'E' };

    // Lock the numeric mapping against Maze.h. If somebody reorders the enum
    // these fire and the whole observation encoding would otherwise silently
    // flip (wall <-> exit), which is the classic sign/gradient bug class.
    // 前 5 个必须原样不动，否则训练好的权重全废。EXIT 后面又插了
    // SLOW_ZONE / MINE，所以 EXIT 不再是 5：数值统一由 NormalizeCell 映射回 6 字母表。
    static_assert((int)CellType::WALL == 0, "CellType::WALL must be 0");
    static_assert((int)CellType::PATH == 1, "CellType::PATH must be 1");
    static_assert((int)CellType::TREASURE_SCORE == 2, "TREASURE_SCORE must be 2");
    static_assert((int)CellType::TREASURE_ATTACK == 3, "TREASURE_ATTACK must be 3");
    static_assert((int)CellType::TREASURE_HEAL == 4, "TREASURE_HEAL must be 4");

    int NormalizeCell(int rawCell)
    {
        switch ((CellType)rawCell)
        {
        case CellType::WALL:            return 0;
        case CellType::PATH:            return 1;
        case CellType::TREASURE_SCORE:  return 2;
        case CellType::TREASURE_ATTACK: return 3;
        case CellType::TREASURE_HEAL:   return 4;
        case CellType::EXIT:            return 5;
        // 神圣模式新加的两堵墙在 RL 眼里就是墙：机器人不该穿玻璃墙，
        // 也不指望它会开枪打可破墙（沿用原 6 字母表，训练好的权重含义不变）
        case CellType::GLASS_WALL:      return 0;
        case CellType::BREAK_WALL:      return 0;
        default:                        return 1;   // SLOW_ZONE / MINE -> 通路
        }
    }

    bool DistField::Build(const Maze& maze)
    {
        const int w = maze.grid.width;
        const int h = maze.grid.height;
        if (w <= 0 || h <= 0) { ready_ = false; return false; }

        std::vector<int> cells((size_t)w * (size_t)h, 0);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                cells[(size_t)y * w + x] = (int)maze.grid.at(x, y);

        return BuildFromCells(cells.data(), w, h, maze.exitGx, maze.exitGy);
    }

    bool DistField::BuildFromCells(const int* cells, int width, int height,
                                   int exitGx, int exitGy)
    {
        width_ = width;
        height_ = height;
        ready_ = false;
        if (width_ <= 0 || height_ <= 0 || cells == nullptr) return false;

        exitGx_ = exitGx;
        exitGy_ = exitGy;
        if (!InBounds(exitGx_, exitGy_)) return false;

        const char* kChar = kCellChar;
        cells_.assign((size_t)width_ * (size_t)height_, 0);
        rows_.assign((size_t)height_, std::string());
        for (int y = 0; y < height_; ++y)
        {
            rows_[y].reserve((size_t)width_);
            for (int x = 0; x < width_; ++x)
            {
                const int c = NormalizeCell(cells[(size_t)y * width_ + x]);
                cells_[(size_t)y * width_ + x] = c;
                rows_[y].push_back((c >= 0 && c < 6) ? kChar[c] : '?');
            }
        }

        dist_.assign((size_t)width_ * (size_t)height_, -1);
        dist_[(size_t)exitGy_ * width_ + exitGx_] = 0;

        std::deque<int> queue;
        queue.push_back(exitGy_ * width_ + exitGx_);
        while (!queue.empty())
        {
            const int idx = queue.front();
            queue.pop_front();
            const int gx = idx % width_;
            const int gy = idx / width_;
            const int nd = dist_[idx] + 1;
            for (int a = 0; a < kNDirs; ++a)
            {
                const int nx = gx + kDirDx[a];
                const int ny = gy + kDirDy[a];
                if (!InBounds(nx, ny)) continue;
                const int nidx = ny * width_ + nx;
                if (cells_[nidx] == 0) continue;            // wall
                if (dist_[nidx] != -1) continue;
                dist_[nidx] = nd;
                queue.push_back(nidx);
            }
        }

        maxDist_ = 0;
        for (int d : dist_) maxDist_ = std::max(maxDist_, d);
        ready_ = true;
        return true;
    }

    bool DistField::InBounds(int gx, int gy) const
    {
        return gx >= 0 && gy >= 0 && gx < width_ && gy < height_;
    }

    int DistField::At(int gx, int gy) const
    {
        if (!InBounds(gx, gy)) return -1;
        return dist_[(size_t)gy * width_ + gx];
    }

    int DistField::Raw(int gx, int gy) const
    {
        return dist_[(size_t)gy * width_ + gx];
    }

    int DistField::CellAt(int gx, int gy) const
    {
        if (!InBounds(gx, gy)) return -1;
        return cells_[(size_t)gy * width_ + gx];
    }

    bool DistField::WalkableAt(int gx, int gy) const
    {
        return CellAt(gx, gy) > 0;
    }

    bool DistField::SetCell(int gx, int gy, int cellType)
    {
        if (!InBounds(gx, gy)) return false;
        if (cellType < 0 || cellType > 5) return false;
        cells_[(size_t)gy * width_ + gx] = cellType;
        rows_[(size_t)gy][(size_t)gx] = kCellChar[cellType];
        return true;
    }
}