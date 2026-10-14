// ===========================================================================
//  Sha256.cpp —— SHA-256 + HMAC-SHA256 的最小实现（FIPS 180-4 / RFC 2104）
//
//  只为了给排行榜提交做签名，不追求吞吐，追求"读得懂、没依赖、跨平台"。
//  桌面版和 Web 版编的是同一份代码。
// ===========================================================================
#include "Sha256.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace sha256
{
    namespace
    {
        // ---- SHA-256 常量 ----
        const uint32_t K[64] =
        {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
        };

        inline uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

        // 一次完整的 SHA-256：输入任意长度字节，输出 32 字节摘要
        void Digest(const uint8_t* data, size_t len, uint8_t out[32])
        {
            uint32_t h[8] =
            {
                0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
            };

            // 补位：0x80 + 若干 0 + 64 位长度（大端）
            std::vector<uint8_t> msg(data, data + len);
            const uint64_t bitLen = (uint64_t)len * 8u;
            msg.push_back(0x80);
            while (msg.size() % 64 != 56) msg.push_back(0x00);
            for (int i = 7; i >= 0; --i)
                msg.push_back((uint8_t)((bitLen >> (i * 8)) & 0xFF));

            uint32_t w[64];
            for (size_t off = 0; off < msg.size(); off += 64)
            {
                for (int i = 0; i < 16; ++i)
                {
                    w[i] = ((uint32_t)msg[off + i * 4] << 24) |
                           ((uint32_t)msg[off + i * 4 + 1] << 16) |
                           ((uint32_t)msg[off + i * 4 + 2] << 8) |
                           ((uint32_t)msg[off + i * 4 + 3]);
                }
                for (int i = 16; i < 64; ++i)
                {
                    const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                    const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
                }

                uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
                uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

                for (int i = 0; i < 64; ++i)
                {
                    const uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
                    const uint32_t ch = (e & f) ^ (~e & g);
                    const uint32_t t1 = hh + S1 + ch + K[i] + w[i];
                    const uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
                    const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
                    const uint32_t t2 = S0 + maj;

                    hh = g; g = f; f = e; e = d + t1;
                    d = c; c = b; b = a; a = t1 + t2;
                }

                h[0] += a; h[1] += b; h[2] += c; h[3] += d;
                h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
            }

            for (int i = 0; i < 8; ++i)
            {
                out[i * 4]     = (uint8_t)(h[i] >> 24);
                out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
                out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
                out[i * 4 + 3] = (uint8_t)(h[i]);
            }
        }

        std::string ToHex(const uint8_t* bytes, size_t n)
        {
            static const char* kHex = "0123456789abcdef";
            std::string s;
            s.reserve(n * 2);
            for (size_t i = 0; i < n; ++i)
            {
                s.push_back(kHex[bytes[i] >> 4]);
                s.push_back(kHex[bytes[i] & 0x0F]);
            }
            return s;
        }
    }

    std::string Hex(const std::string& data)
    {
        uint8_t out[32];
        Digest((const uint8_t*)data.data(), data.size(), out);
        return ToHex(out, 32);
    }

    std::string HmacHex(const std::string& key, const std::string& message)
    {
        // HMAC = H((K ^ opad) || H((K ^ ipad) || msg))
        const size_t kBlockSize = 64;
        std::string k = key;
        if (k.size() > kBlockSize)
        {
            uint8_t kh[32];
            Digest((const uint8_t*)k.data(), k.size(), kh);
            k.assign((const char*)kh, 32);
        }
        k.resize(kBlockSize, '\0');

        std::string inner(kBlockSize, '\0');
        std::string outer(kBlockSize, '\0');
        for (size_t i = 0; i < kBlockSize; ++i)
        {
            inner[i] = (char)((uint8_t)k[i] ^ 0x36);
            outer[i] = (char)((uint8_t)k[i] ^ 0x5C);
        }

        inner += message;
        uint8_t innerHash[32];
        Digest((const uint8_t*)inner.data(), inner.size(), innerHash);

        std::string outerFull = outer;
        outerFull.append((const char*)innerHash, 32);

        uint8_t out[32];
        Digest((const uint8_t*)outerFull.data(), outerFull.size(), out);
        return ToHex(out, 32);
    }
}