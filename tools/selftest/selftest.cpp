// ===========================================================================
//  selftest.cpp —— 纯桌面自检：验自己手写的 SHA-256 / HMAC-SHA256
//
//  为什么需要它：
//    全网榜的防作弊签名是「客户端算 HMAC-SHA256，服务端 Web Crypto 复核」。
//    服务端那边用的是标准库，客户端这份是自己写的 —— 只要有一个字节算错，
//    所有成绩都会被 403 拒掉，而且现象是"本地 log.csv 好好的，就是不上榜"，
//    非常难查。所以这里把它和 Python 的 hashlib 对着比一遍。
//
//  用法：
//    cmake --build build\desktop --config Release --target selftest
//    build\desktop\Release\selftest.exe > out.txt
//    python tools\selftest\check.py out.txt
// ===========================================================================
#include "Sha256.h"

#include <cstdio>
#include <string>

// 排行榜签名用的密钥：必须和 src/Leaderboard.cpp 的 kSignKey 一致
static const char* kSignKey = "maze-web-2f8a1c9d4e6b7a30-change-me-on-deploy";

static std::string Repeat(const std::string& s, int n)
{
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

int main()
{
    // ---- SHA-256：空串 / 短串 / 跨块长串 ----
    printf("SHA  empty           %s\n", sha256::Hex("").c_str());
    printf("SHA  abc             %s\n", sha256::Hex("abc").c_str());
    printf("SHA  block56         %s\n",
           sha256::Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
    printf("SHA  a1000           %s\n", sha256::Hex(Repeat("a", 1000)).c_str());

    // ---- HMAC-SHA256：短 key / 长 key(>块长，要先对 key 做一次 hash) ----
    printf("HMAC fox             %s\n",
           sha256::HmacHex("key", "The quick brown fox jumps over the lazy dog").c_str());
    printf("HMAC jefe            %s\n",
           sha256::HmacHex("Jefe", "what do ya want for nothing?").c_str());
    printf("HMAC key20           %s\n",
           sha256::HmacHex(Repeat("a", 20), "Hi There").c_str());
    printf("HMAC key131          %s\n",
           sha256::HmacHex(Repeat("a", 131),
                           "Test Using Larger Than Block-Size Key - Hash Key First").c_str());
    printf("HMAC utf8            %s\n",
           sha256::HmacHex("maze", "奶龙|神圣模式|排行榜").c_str());

    // ---- 排行榜真实签名原文（字段顺序 = 服务端 canonical） ----
    char elapsed[32];
    snprintf(elapsed, sizeof(elapsed), "%.3f", 123.4567);
    const std::string canonical =
        std::string("JJC") + "|" + "holy" + "|" + "score" + "|" + "nailong" + "|" +
        std::to_string(4242) + "|" + elapsed;
    printf("CANON                %s\n", canonical.c_str());
    printf("SIGN                 %s\n", sha256::HmacHex(kSignKey, canonical).c_str());

    return 0;
}