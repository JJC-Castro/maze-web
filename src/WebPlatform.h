#pragma once
// ===========================================================================
//  WebPlatform.h —— 浏览器环境适配
//
//  只做一件事：把一份"刷新页面还在"的目录挂进 Emscripten 的虚拟文件系统。
//
//  为什么需要：
//    浏览器里 std::ofstream 写的是内存 FS（MEMFS），页面一刷新全没了。
//    log.csv 和三个存档槽都写在这上面，不挂载的话玩家会以为"游戏没保存"。
//    Emscripten 提供 IDBFS —— 把 IndexedDB 当成一个目录挂进来，
//    手动 syncfs() 就能在内存 FS 和 IndexedDB 之间来回搬。
//
//  桌面版：全部空实现。
// ===========================================================================
namespace web
{
    // 挂载 IndexedDB 目录到 /maze，并把上次存的数据读回内存（异步，不阻塞启动）
    void InitFileSystem();

    // 把内存里的写入同步回 IndexedDB。写完 log.csv / 存档后调一次。
    void FlushFileSystem();

    // 挂载是否已经建立（只表示"调用过"，不表示数据已读完）
    bool Ready();
}