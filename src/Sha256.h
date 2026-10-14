#pragma once
// ===========================================================================
//  Sha256.h —— 自带的最小 SHA-256 + HMAC-SHA256
//
//  为什么要自己写：
//    排行榜要防"随手改个数字就提交"。服务端（Worker）用 Web Crypto 验签，
//    客户端就得有能算 HMAC-SHA256 的东西。Emscripten 里当然可以绕到 JS 的
//    crypto.subtle，但那是异步 API，会污染提交逻辑；而拉一个 OpenSSL 进来
//    只为算个 HMAC 又太重。所以这里放一份 ~120 行的自包含实现。
//
//  ⚠ 安全边界（必须写清楚，别自欺欺人）：
//    密钥编译进 wasm，逆向出来只是时间问题。这套签名只把「随手改数字」提升到
//    「得先逆向前端」，**不是**真正的服务端权威。要彻底防作弊，唯一办法是
//    服务端重放验证（拿 seed 重跑一遍），那是另一个量级的工程量。
// ===========================================================================
#include <string>

namespace sha256
{
    // 十六进制小写摘要
    std::string Hex(const std::string& data);

    // HMAC-SHA256，返回十六进制小写
    std::string HmacHex(const std::string& key, const std::string& message);
}