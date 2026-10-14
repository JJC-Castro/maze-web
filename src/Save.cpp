
#include "Save.h"
#include "Player.h"
#include <fstream>
#include <vector>
#include "json.hpp"

using json = nlohmann::json;

// ============================================================
// 辅助：CellType ? char
// ============================================================
static char CellToChar(CellType t)
{
    switch (t)
    {
    case CellType::WALL:            return 'W';
    case CellType::PATH:            return '.';
    case CellType::TREASURE_SCORE:  return 'S';
    case CellType::TREASURE_ATTACK: return 'A';
    case CellType::TREASURE_HEAL:   return 'H';
    case CellType::SLOW_ZONE:       return 'Z';
    case CellType::MINE:            return 'M';
    case CellType::GLASS_WALL:      return 'G';   // 神圣：玻璃墙
    case CellType::BREAK_WALL:      return 'B';   // 神圣：可破墙
    case CellType::EXIT:            return 'E';
    default:                        return '?';
    }
}

static CellType CharToCell(char c)
{
    switch (c)
    {
    case 'W': return CellType::WALL;
    case '.': return CellType::PATH;
    case 'S': return CellType::TREASURE_SCORE;
    case 'A': return CellType::TREASURE_ATTACK;
    case 'H': return CellType::TREASURE_HEAL;
    case 'Z': return CellType::SLOW_ZONE;
    case 'M': return CellType::MINE;
    case 'G': return CellType::GLASS_WALL;
    case 'B': return CellType::BREAK_WALL;
    case 'E': return CellType::EXIT;
    default:  return CellType::PATH;
    }
}

// ============================================================
// 保存
// ============================================================
bool GameSave::Save(const char* path, const Maze& maze, const Player& player, const Data& data)
{
    json j;

    // ---- 迷宫 ----
    j["maze"]["width"] = maze.grid.width;
    j["maze"]["height"] = maze.grid.height;
    j["maze"]["entryGx"] = maze.entryGx;
    j["maze"]["entryGy"] = maze.entryGy;
    j["maze"]["exitGx"] = maze.exitGx;
    j["maze"]["exitGy"] = maze.exitGy;
    j["maze"]["treasureCount"] = maze.treasureCount;

    std::vector<std::string> rows;
    for (int y = 0; y < maze.grid.height; ++y)
    {
        std::string row;
        row.reserve(maze.grid.width);
        for (int x = 0; x < maze.grid.width; ++x)
            row.push_back(CellToChar(maze.grid.at(x, y)));
        rows.push_back(std::move(row));
    }
    j["maze"]["cells"] = rows;

    // ---- 玩家 ----
    const Vector3 p = player.GetPosition();   // 眼睛位置
    j["player"]["x"] = p.x;
    j["player"]["y"] = p.y;
    j["player"]["z"] = p.z;

    j["player"]["yaw"] = player.GetYaw();       // 直接取角色朝向
    j["player"]["pitch"] = player.GetPitch();   // 直接取视线俯仰

    // ---- 游戏数据 ----
    j["data"]["playerHealth"] = data.playerHealth;
    j["data"]["playerMaxHealth"] = data.playerMaxHealth;
    j["data"]["playerAttacksRemaining"] = data.playerAttacksRemaining;
    j["data"]["score"] = data.score;
    j["data"]["elapsed"] = data.elapsed;

    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << j.dump(2);
    return out.good();
}

// ============================================================
// 加载
// ============================================================
bool GameSave::Load(const char* path, Maze& maze, Player& player, Data& outData)
{
    std::ifstream in(path);
    if (!in.is_open()) return false;

    json j;
    try { in >> j; }
    catch (...) { return false; }

    if (!j.contains("maze") || !j.contains("player") || !j.contains("data"))
        return false;

    // ---- 迷宫 ----
    const auto& mj = j["maze"];
    const int w = mj.value("width", 0);
    const int h = mj.value("height", 0);
    if (w <= 0 || h <= 0 || w > 200 || h > 200) return false;

    maze.grid = Grid<CellType>(w, h);
    maze.entryGx = mj.value("entryGx", 1);
    maze.entryGy = mj.value("entryGy", 1);
    maze.exitGx = mj.value("exitGx", w - 2);
    maze.exitGy = mj.value("exitGy", h - 2);
    maze.treasureCount = mj.value("treasureCount", 0);

    const auto& rows = mj["cells"];
    if ((int)rows.size() != h) return false;
    for (int y = 0; y < h; ++y)
    {
        const std::string row = rows[y].get<std::string>();
        for (int x = 0; x < w; ++x)
        {
            char c = (x < (int)row.size()) ? row[x] : '.';
            maze.grid.at(x, y) = CharToCell(c);
        }
    }

    // ---- 玩家 ----
    const auto& pj = j["player"];
    const Vector3 p = {
        pj.value("x", 0.0f),
        pj.value("y", 0.0f),
        pj.value("z", 0.0f)
    };
    player.SetPosition(p);

    // 用 yaw/pitch 恢复朝向
    const float yaw = pj.value("yaw", 0.0f);
    const float pitch = pj.value("pitch", 0.0f);
    const Vector3 forward = {
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    };
    player.LookAt(Vector3{ p.x + forward.x, p.y + forward.y, p.z + forward.z });

    // ---- 游戏数据 ----
    const auto& dj = j["data"];
    outData.playerHealth = dj.value("playerHealth", 100);
    outData.playerMaxHealth = dj.value("playerMaxHealth", 100);
    outData.playerAttacksRemaining = dj.value("playerAttacksRemaining", 10);
    outData.score = dj.value("score", 0);
    outData.elapsed = dj.value("elapsed", 0.0);

    return true;
}