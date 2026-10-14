#include "Account.h"
#include "UIFont.h"
#ifdef MAZE_WEB
#include "Leaderboard.h"    // 全网榜：通关后把成绩传给服务端
#include "WebPlatform.h"    // IndexedDB 持久化
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace
{
    // log.csv 固定放在游戏根目录：exe 位于 <根目录>\codes\x64\<Config>\codes.exe，
    // 从 exe 路径上溯三层就是根目录 —— 无论从哪个工作目录启动游戏，记录都写在同一个文件里。
    std::string BuildLogPath()
    {
#ifdef MAZE_WEB
        // Web：/maze 是挂到 IndexedDB 上的持久化目录（见 WebPlatform.cpp）。
        // 写在这儿的数据刷新页面还在；没挂载成功时它退化成内存 FS，刷新即丢。
        return std::string("/maze/log.csv");
#else
        char* exePath = nullptr;
        _get_pgmptr(&exePath);                                  // 当前 exe 的完整路径
        const std::string exe = exePath ? std::string(exePath) : std::string();
        const size_t slash = exe.find_last_of("\\/");
        if (slash == std::string::npos) return std::string("log.csv");

        std::string dir = exe.substr(0, slash);
        for (int i = 0; i < 3; ++i)          // 去掉 codes\x64\<Config> 三层
        {
            const size_t up = dir.find_last_of("\\/");
            if (up == std::string::npos) return std::string("log.csv");
            dir = dir.substr(0, up);
        }
        return dir + "\\log.csv";
#endif
    }

    const std::string& LogFile()
    {
        static const std::string path = BuildLogPath();
        return path;
    }

    const char* DiffTag(Difficulty d)
    {
        switch (d)
        {
        case Difficulty::EASY: return "easy";
        case Difficulty::HARD: return "hard";
        case Difficulty::HELL: return "hell";
        case Difficulty::HOLY: return "holy";
        case Difficulty::HUNGER: return "hunger";
        case Difficulty::ROGUE: return "rogue";
        default:               return "normal";
        }
    }

    bool ParseDiff(const std::string& tag, Difficulty& out)
    {
        if (tag == "easy")   { out = Difficulty::EASY;   return true; }
        if (tag == "normal") { out = Difficulty::NORMAL; return true; }
        if (tag == "hard")   { out = Difficulty::HARD;   return true; }
        if (tag == "hell")   { out = Difficulty::HELL;   return true; }
        if (tag == "holy")   { out = Difficulty::HOLY;   return true; }
        if (tag == "hunger") { out = Difficulty::HUNGER; return true; }
        if (tag == "rogue")  { out = Difficulty::ROGUE;  return true; }
        return false;
    }

    // 模式在 log.csv 里的标记：normal / nailong
    const char* ModeTag(GameMode m)
    {
        return (m == GameMode::NAILONG) ? "nailong" : "normal";
    }

    bool ParseMode(const std::string& tag, GameMode& out)
    {
        if (tag == "nailong") { out = GameMode::NAILONG; return true; }
        if (tag == "normal")  { out = GameMode::NORMAL;  return true; }
        return false;
    }

    std::string Trim(const std::string& s)
    {
        size_t begin = 0;
        size_t end = s.size();
        while (begin < end && (unsigned char)s[begin] <= ' ') ++begin;
        while (end > begin && (unsigned char)s[end - 1] <= ' ') --end;
        return s.substr(begin, end - begin);
    }

    std::vector<std::string> Split(const std::string& line)
    {
        std::vector<std::string> out;
        size_t start = 0;
        while (true)
        {
            const size_t comma = line.find(',', start);
            if (comma == std::string::npos)
            {
                out.push_back(line.substr(start));
                break;
            }
            out.push_back(line.substr(start, comma - start));
            start = comma + 1;
        }
        return out;
    }

    // 同一条记录的身份：用户名 + 难度。两个排行榜分别维护各自的最好成绩。
    bool SameRecordKey(const ScoreRecord& a, const ScoreRecord& b)
    {
        return a.name == b.name && a.difficulty == b.difficulty &&
               a.mode == b.mode;      // 正常 / 奶龙 各自保留最好的一次
    }

    // 最高分榜：分数高优先，同分用时短优先。
    bool BetterByScore(const ScoreRecord& a, const ScoreRecord& b)
    {
        if (a.score != b.score) return a.score > b.score;
        return a.elapsed < b.elapsed;
    }

    // 最快用时榜：用时短优先，用时相同分数高优先。
    bool BetterByTime(const ScoreRecord& a, const ScoreRecord& b)
    {
        if (fabs(a.elapsed - b.elapsed) > 1e-9) return a.elapsed < b.elapsed;
        return a.score > b.score;
    }

    // 排行榜类型和 log.csv 第六列之间的转换。
    const char* BoardTag(RankBoard board)
    {
        switch (board)
        {
        case RankBoard::Score: return "score";
        case RankBoard::Time: return "time";
        default: return "legacy";
        }
    }

    bool ParseBoard(const std::string& tag, RankBoard& out)
    {
        if (tag == "score") { out = RankBoard::Score; return true; }
        if (tag == "time") { out = RankBoard::Time; return true; }
        if (tag == "legacy") { out = RankBoard::Legacy; return true; }
        return false;
    }

    // 把记录插入某个榜单，同名同难度只保留该榜单里最好的一条。
    void InsertBest(
        std::vector<ScoreRecord>& records,
        const ScoreRecord& candidate,
        bool (*better)(const ScoreRecord&, const ScoreRecord&))
    {
        for (ScoreRecord& current : records)
        {
            if (!SameRecordKey(current, candidate)) continue;
            if (better(candidate, current))
            {
                current = candidate;
            }
            return;
        }
        records.push_back(candidate);
    }

    std::string ToText(double value)        // 用时保留 3 位小数
    {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.3f", value);
        return std::string(buf);
    }

    std::string ToLine(const ScoreRecord& r)
    {
        return r.name + "," + DiffTag(r.difficulty) + "," +
            std::to_string(r.score) + "," + ToText(r.elapsed) + "," +
            r.date + "," + BoardTag(r.board) + "," + ModeTag(r.mode) + "\r\n";
    }

    void WriteAll(const std::vector<ScoreRecord>& records)
    {
        std::ofstream file(LogFile().c_str(), std::ios::binary | std::ios::trunc);
        if (!file) return;

        // UTF-8 BOM + 表头（Excel 打开不乱码）
        const char bom[3] = { (char)0xEF, (char)0xBB, (char)0xBF };
        file.write(bom, 3);
        file << Utf8("用户名,难度,分数,用时,日期,榜单,模式") << "\r\n";

        for (const ScoreRecord& r : records)
            file << ToLine(r);
    }
}

const char* Account::LogPath()
{
    return LogFile().c_str();
}

std::string Account::NowText()
{
    const time_t now = time(nullptr);
    tm local{};
#ifdef MAZE_WEB
    localtime_r(&now, &local);      // MSVC 的 *_s 版本 Emscripten 里没有
#else
    localtime_s(&local, &now);
#endif

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return std::string(buf);
}

std::vector<ScoreRecord> Account::LoadAll()
{
    std::vector<ScoreRecord> out;

    std::ifstream file(LogFile().c_str(), std::ios::binary);
    if (!file) return out;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string text = buffer.str();

    if (text.size() >= 3 &&
        (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB &&
        (unsigned char)text[2] == 0xBF)
    {
        text.erase(0, 3);
    }

    size_t start = 0;
    while (start < text.size())
    {
        const size_t newline = text.find('\n', start);
        std::string line = (newline == std::string::npos)
            ? text.substr(start)
            : text.substr(start, newline - start);
        start = (newline == std::string::npos) ? text.size() : newline + 1;

        line = Trim(line);
        if (line.empty()) continue;

        const std::vector<std::string> fields = Split(line);
        if (fields.size() < 5) continue;                 // 表头 / 坏行

        Difficulty diff = Difficulty::NORMAL;
        if (!ParseDiff(Trim(fields[1]), diff)) continue; // 表头会走到这里

        ScoreRecord record;
        record.name = Trim(fields[0]);
        record.difficulty = diff;
        record.score = atoi(Trim(fields[2]).c_str());
        record.elapsed = atof(Trim(fields[3]).c_str());
        record.date = Trim(fields[4]);
        record.board = RankBoard::Legacy; // 旧文件没有第六列，按旧记录处理

        if (fields.size() >= 6)
        {
            RankBoard board = RankBoard::Legacy;
            if (ParseBoard(Trim(fields[5]), board))
            {
                record.board = board;
            }
        }

        // 第 7 列：模式。老记录没有这一列，一律按正常模式读，不回写老行
        if (fields.size() >= 7)
        {
            GameMode mode = GameMode::NORMAL;
            if (ParseMode(Trim(fields[6]), mode))
            {
                record.mode = mode;
            }
        }

        out.push_back(record);
    }

    return out;
}

bool Account::SubmitRecord(const ScoreRecord& rec)
{
    const std::vector<ScoreRecord> all = LoadAll();

    // 旧版记录同时参与两个榜单，写入时会被规范成显式的 score/time 两条。
    std::vector<ScoreRecord> scoreRecords;
    std::vector<ScoreRecord> timeRecords;
    for (const ScoreRecord& r : all)
    {
        if (r.board == RankBoard::Score || r.board == RankBoard::Legacy)
        {
            ScoreRecord copy = r;
            copy.board = RankBoard::Score;
            InsertBest(scoreRecords, copy, BetterByScore);
        }
        if (r.board == RankBoard::Time || r.board == RankBoard::Legacy)
        {
            ScoreRecord copy = r;
            copy.board = RankBoard::Time;
            InsertBest(timeRecords, copy, BetterByTime);
        }
    }

    bool scoreUpdated = false;
    bool timeUpdated = false;

    // 新成绩单独更新最高分榜。
    {
        const ScoreRecord* best = nullptr;
        for (const ScoreRecord& r : scoreRecords)
        {
            if (SameRecordKey(r, rec) && (!best || BetterByScore(r, *best)))
            {
                best = &r;
            }
        }

        if (!best || BetterByScore(rec, *best))
        {
            ScoreRecord copy = rec;
            copy.board = RankBoard::Score;
            scoreRecords.erase(
                std::remove_if(
                    scoreRecords.begin(),
                    scoreRecords.end(),
                    [&](const ScoreRecord& r) { return SameRecordKey(r, rec); }
                ),
                scoreRecords.end()
            );
            scoreRecords.push_back(copy);
            scoreUpdated = true;
        }
    }

    // 新成绩单独更新最快用时榜。
    {
        const ScoreRecord* best = nullptr;
        for (const ScoreRecord& r : timeRecords)
        {
            if (SameRecordKey(r, rec) && (!best || BetterByTime(r, *best)))
            {
                best = &r;
            }
        }

        if (!best || BetterByTime(rec, *best))
        {
            ScoreRecord copy = rec;
            copy.board = RankBoard::Time;
            timeRecords.erase(
                std::remove_if(
                    timeRecords.begin(),
                    timeRecords.end(),
                    [&](const ScoreRecord& r) { return SameRecordKey(r, rec); }
                ),
                timeRecords.end()
            );
            timeRecords.push_back(copy);
            timeUpdated = true;
        }
    }

    if (!scoreUpdated && !timeUpdated)
    {
        return false;
    }

    std::vector<ScoreRecord> kept;
    kept.reserve(scoreRecords.size() + timeRecords.size());
    kept.insert(kept.end(), scoreRecords.begin(), scoreRecords.end());
    kept.insert(kept.end(), timeRecords.begin(), timeRecords.end());

    WriteAll(kept);

#ifdef MAZE_WEB
    // Web：本地 log.csv 写进 IndexedDB 目录后要先刷盘（否则刷新页面就没了）；
    // 再顺手把这一条传给全网榜。两个榜（分数 / 用时）都要传。
    web::FlushFileSystem();
    {
        ScoreRecord scoreBoard = rec;
        scoreBoard.board = RankBoard::Score;
        lb::Submit(scoreBoard);

        ScoreRecord timeBoard = rec;
        timeBoard.board = RankBoard::Time;
        lb::Submit(timeBoard);
    }
#endif

    return true;
}

std::vector<ScoreRecord> Account::RankByDifficulty(
    Difficulty d,
    RankBoard board,
    int maxCount)
{
    std::vector<ScoreRecord> out;

#ifdef MAZE_WEB
    // Web：本地 log.csv 只在这台机器上，真正的「全网」在服务端。
    //   · RequestBoard() 是异步的 —— 这一帧拿不到，但会缓存下来，
    //     所以"第一次进这一页看本地、第二次进就看全网"是预期行为（不卡主线程）。
    //   · TakeCached() 把已经缓存到的云端成绩并进来，跟着本地榜一起排序。
    //     同名同模式的两条会由下面的 InsertBest 去重，留更好的那条。
    lb::RequestBoard(d, board, maxCount > 0 ? maxCount : RANK_LIST_MAX);
    lb::TakeCached(d, board, out);
#endif

    bool (*better)(const ScoreRecord&, const ScoreRecord&) =
        (board == RankBoard::Time) ? BetterByTime : BetterByScore;

    for (const ScoreRecord& r : LoadAll())
    {
        if (r.difficulty != d) continue;
        if (r.board != board && r.board != RankBoard::Legacy) continue;
        InsertBest(out, r, better);
    }

    std::sort(out.begin(), out.end(), better);

    if (maxCount > 0 && (int)out.size() > maxCount)
    {
        out.resize((size_t)maxCount);
    }

    return out;
}