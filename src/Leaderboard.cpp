// ===========================================================================
//  Leaderboard.cpp —— 全网榜客户端实现
// ===========================================================================
#include "Leaderboard.h"

#include "UIFont.h"     // Utf8()
#include "Sha256.h"

#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#ifdef MAZE_WEB
#include <emscripten.h>
#include <emscripten/fetch.h>
#include "json.hpp"
#endif

namespace lb
{
    namespace
    {
        // -------------------------------------------------------------------
        //  编译期密钥：和 server/worker.js 里的 LEADERBOARD_SECRET 必须一致。
        //  改这里要同时改服务端，否则签名验证全挂。
        // -------------------------------------------------------------------
        const char* kSignKey = "maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy";

        std::string gBaseUrl;                   // 空 = 同域 /api
        std::string gStatus = "idle";

        // 缓存：key = "<diff>|<board>"，value = 服务端返回的行
        std::mutex gMutex;
        std::map<std::string, std::vector<ScoreRecord>> gCache;
        std::map<std::string, bool> gInflight;

        std::string BoardTag(RankBoard b)
        {
            return (b == RankBoard::Time) ? "time" : "score";
        }

        std::string DiffTag(Difficulty d)
        {
            switch (d)
            {
            case Difficulty::EASY:   return "easy";
            case Difficulty::HARD:   return "hard";
            case Difficulty::HELL:   return "hell";
            case Difficulty::HOLY:   return "holy";
            case Difficulty::HUNGER: return "hunger";
            case Difficulty::ROGUE:  return "rogue";
            default:                 return "normal";
            }
        }

        std::string CacheKey(Difficulty d, RankBoard b)
        {
            return DiffTag(d) + "|" + BoardTag(b);
        }

        std::string Url(const std::string& path)
        {
            return gBaseUrl + path;
        }

        // 把字符串转成 JSON 字面量（转义引号 / 反斜杠 / 控制字符）
        std::string JsonString(const std::string& s)
        {
            std::string out = "\"";
            for (unsigned char c : s)
            {
                switch (c)
                {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
                        out += buf;
                    }
                    else
                    {
                        out.push_back((char)c);     // UTF-8 原样传
                    }
                    break;
                }
            }
            out += "\"";
            return out;
        }
    }

    // =======================================================================
    //  桌面版：全部空实现
    // =======================================================================
#ifndef MAZE_WEB

    void SetBaseUrl(const char*) {}
    void Submit(const ScoreRecord&) {}
    void RequestBoard(Difficulty, RankBoard, int) {}
    void TakeCached(Difficulty, RankBoard, std::vector<ScoreRecord>&) {}
    const char* Status() { return "local"; }
    void Poll() {}

#else   // ======================= Web 版 =========================

    void SetBaseUrl(const char* url)
    {
        gBaseUrl = (url != nullptr) ? url : "";
        // 允许部署时用 JS 全局变量覆盖（比如指向另一个 Worker 域名）
        const char* fromJs = (const char*)EM_ASM_PTR({
            var v = (typeof window !== "undefined" && window.MAZE_API_BASE) ? window.MAZE_API_BASE : "";
            var n = lengthBytesUTF8(v) + 1;
            var p = _malloc(n);
            stringToUTF8(v, p, n);
            return p;
        });
        if (fromJs != nullptr && fromJs[0] != 0)
        {
            gBaseUrl = fromJs;
        }
        if (fromJs != nullptr) free((void*)fromJs);
    }

    // ---- 上传完成回调 ----
    void OnSubmitDone(emscripten_fetch_t* fetch)
    {
        if (fetch->status == 200)
        {
            gStatus = "uploaded";
        }
        else
        {
            // 服务端拒绝（验签失败 / 超出合理范围）时也走到这儿，
            // 本地 log.csv 不受影响，只是这条没进全网榜。
            char buf[64];
            std::snprintf(buf, sizeof(buf), "upload rejected (%d)", (int)fetch->status);
            gStatus = buf;
        }
        emscripten_fetch_close(fetch);
    }

    void OnFetchError(emscripten_fetch_t* fetch)
    {
        gStatus = "offline";
        emscripten_fetch_close(fetch);
    }

    // ---- 榜单拉取完成回调 ----
    void OnBoardDone(emscripten_fetch_t* fetch)
    {
        const std::string body(fetch->data, (size_t)fetch->numBytes);
        const std::string key = (const char*)fetch->userData;
        char url[256] = { 0 };
        std::snprintf(url, sizeof(url), "%s", fetch->url);
        emscripten_fetch_close(fetch);

        // key 形如 "easy|score"
        const size_t bar = key.find('|');
        if (bar == std::string::npos) return;

        try
        {
            const nlohmann::json doc = nlohmann::json::parse(body);
            if (!doc.value("ok", false)) { gStatus = "server rejected"; return; }

            std::vector<ScoreRecord> rows;
            for (const auto& item : doc["rows"])
            {
                ScoreRecord r;
                r.name = item.value("name", std::string("?"));
                r.score = item.value("score", 0);
                r.elapsed = item.value("elapsed", 0.0);
                r.date = item.value("date", std::string());
                r.board = (key.substr(bar + 1) == "time") ? RankBoard::Time : RankBoard::Score;
                r.mode = (item.value("mode", std::string("normal")) == "nailong")
                    ? GameMode::NAILONG : GameMode::NORMAL;

                const std::string d = item.value("difficulty", std::string("normal"));
                if (d == "easy")        r.difficulty = Difficulty::EASY;
                else if (d == "hard")   r.difficulty = Difficulty::HARD;
                else if (d == "hell")   r.difficulty = Difficulty::HELL;
                else if (d == "holy")   r.difficulty = Difficulty::HOLY;
                else if (d == "hunger") r.difficulty = Difficulty::HUNGER;
                else if (d == "rogue")  r.difficulty = Difficulty::ROGUE;
                else                    r.difficulty = Difficulty::NORMAL;

                rows.push_back(r);
            }

            {
                std::lock_guard<std::mutex> lock(gMutex);
                gCache[key] = rows;
                gInflight[key] = false;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "online (%d)", (int)rows.size());
            gStatus = buf;
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gInflight[key] = false;
            gStatus = "bad response";
        }
    }

    void OnBoardError(emscripten_fetch_t* fetch)
    {
        const std::string key = (const char*)fetch->userData;
        if (!key.empty())
        {
            std::lock_guard<std::mutex> lock(gMutex);
            gInflight[key] = false;
        }
        gStatus = "offline";
        emscripten_fetch_close(fetch);
    }

    void Submit(const ScoreRecord& rec)
    {
        // 规范化：字段顺序必须和服务端拼串的顺序完全一致，否则验签必挂
        char scoreBuf[32];
        char timeBuf[32];
        std::snprintf(scoreBuf, sizeof(scoreBuf), "%d", rec.score);
        std::snprintf(timeBuf, sizeof(timeBuf), "%.3f", rec.elapsed);

        const std::string diff = DiffTag(rec.difficulty);
        const std::string board = BoardTag(rec.board);
        const std::string mode = (rec.mode == GameMode::NAILONG) ? "nailong" : "normal";

        const std::string canonical =
            rec.name + "|" + diff + "|" + board + "|" + mode + "|" + scoreBuf + "|" + timeBuf;
        const std::string sig = sha256::HmacHex(kSignKey, canonical);

        const std::string body =
            "{\"name\":" + JsonString(rec.name) +
            ",\"difficulty\":" + JsonString(diff) +
            ",\"board\":" + JsonString(board) +
            ",\"mode\":" + JsonString(mode) +
            ",\"score\":" + scoreBuf +
            ",\"elapsed\":" + timeBuf +
            ",\"sig\":" + JsonString(sig) + "}";

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::snprintf(attr.requestMethod, sizeof(attr.requestMethod), "POST");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.onsuccess = OnSubmitDone;
        attr.onerror = OnFetchError;
        attr.requestData = body.c_str();
        attr.requestDataSize = body.size();
        static const char* headers[] = { "Content-Type", "application/json", nullptr };
        attr.requestHeaders = headers;

        static std::string urlStorage;
        urlStorage = Url("/score");
        emscripten_fetch(&attr, urlStorage.c_str());
    }

    void RequestBoard(Difficulty d, RankBoard board, int limit)
    {
        const std::string key = CacheKey(d, board);
        {
            std::lock_guard<std::mutex> lock(gMutex);
            if (gInflight[key]) return;         // 已经在拉了，别重复发
            gInflight[key] = true;
        }

        char q[192];
        std::snprintf(q, sizeof(q), "/board?difficulty=%s&board=%s&limit=%d",
                      DiffTag(d).c_str(), BoardTag(board).c_str(), limit);

        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        std::snprintf(attr.requestMethod, sizeof(attr.requestMethod), "GET");
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
        attr.onsuccess = OnBoardDone;
        attr.onerror = OnBoardError;

        // userData 由回调读取，必须是常驻内存；这里用静态字符串池
        static std::vector<std::string*> pool;
        pool.push_back(new std::string(key));
        attr.userData = (void*)pool.back()->c_str();

        static std::string urlStorage;
        urlStorage = Url(q);
        emscripten_fetch(&attr, urlStorage.c_str());
    }

    void TakeCached(Difficulty d, RankBoard board, std::vector<ScoreRecord>& out)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        const auto it = gCache.find(CacheKey(d, board));
        if (it == gCache.end()) return;
        out.insert(out.end(), it->second.begin(), it->second.end());
    }

    const char* Status() { return gStatus.c_str(); }
    void Poll() {}      // emscripten_fetch 的回调已经在主线程，不需要额外搬运

#endif
}