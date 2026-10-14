#pragma once
// ===========================================================================
//  Leaderboard.h —— 全网榜客户端（Web 版专用）
//
//  两条链路：
//    上传：玩家通关 -> Account::SubmitRecord() 写本地 log.csv -> lb::Submit() 打给服务端
//    拉取：打开「记录排名」页 -> lb::RequestBoard() 异步拉 -> 缓存
//          -> 下一次进这一页时把云端成绩并进本地榜一起排序
//
//  为什么是"下一次"：HTTP 是异步的，进去那一下拿不到。缓存下来后第二次打开就有了，
//  也没必要为了"立刻显示"把主线程卡在同步请求上。
//
//  签名：提交时带上 HMAC-SHA256（见 Sha256.h），服务端用 Web Crypto 验。
//        ⚠ 密钥在 wasm 里，只能抬高作弊门槛，不能根除 —— 详见 Sha256.h 的说明。
//
//  桌面版：全部是空实现（桌面版没有"全网"可言，log.csv 就是它的榜）。
// ===========================================================================
#include "Account.h"

#include <string>
#include <vector>

namespace lb
{
    // 后端地址。默认空 = 用同域 "/api"（Cloudflare Pages 上最省事的部署方式）。
    void SetBaseUrl(const char* url);

    // 上传一条通关成绩（异步，不阻塞主线程）
    void Submit(const ScoreRecord& rec);

    // 拉取某个难度的榜单（异步）。limit 建议 >= 排行榜要显示的行数。
    void RequestBoard(Difficulty d, RankBoard board, int limit);

    // 把已经缓存的云端成绩追加到 out（去重 / 排序交给调用方的 InsertBest）
    void TakeCached(Difficulty d, RankBoard board, std::vector<ScoreRecord>& out);

    // 给 UI 用的一句话状态（"云端 OK" / "离线" / "桌面版不支持"...）
    const char* Status();

    // 主线程每帧调一次：处理回调搬运与超时清理
    void Poll();
}