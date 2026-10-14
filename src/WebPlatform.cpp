// ===========================================================================
//  WebPlatform.cpp —— IndexedDB 持久化（IDBFS）
// ===========================================================================
#include "WebPlatform.h"

#ifdef MAZE_WEB
#include <emscripten.h>
#endif

namespace web
{
    namespace
    {
#ifdef MAZE_WEB
        bool gReady = false;
#endif
    }

#ifndef MAZE_WEB

    // 桌面版：真实文件系统本身就是持久的，什么都不用做
    void InitFileSystem() {}
    void FlushFileSystem() {}
    bool Ready() { return true; }

#else

    // -----------------------------------------------------------------------
    //  syncfs(populate)
    //    populate = true  : IndexedDB -> 内存（启动时读回来）
    //    populate = false : 内存 -> IndexedDB（保存）
    //  两个方向都是异步的，回调里把错误打到 console 就够了 ——
    //  存档失败不该把游戏搞崩，顶多这一局没留住。
    // -----------------------------------------------------------------------
    EM_JS(void, JsSyncFs, (int populate), {
        try {
            FS.syncfs(populate !== 0, function (err) {
                if (err) console.warn('[maze] IDBFS sync failed:', err);
            });
        } catch (e) {
            console.warn('[maze] IDBFS unavailable:', e);
        }
    });

    void InitFileSystem()
    {
        // 挂载点必须先存在；重复 Init 时 mkdir 会抛，吞掉就行
        EM_ASM({
            try { FS.mkdir('/maze'); } catch (e) { }
            try { FS.mount(IDBFS, {}, '/maze'); } catch (e) { }
            // 子目录要在挂载之后建，否则挂载会把内存里的空目录冲掉
            try { FS.mkdir('/maze/saves'); } catch (e) { }
            try { FS.mkdir('/maze/rl_log'); } catch (e) { }
        });
        JsSyncFs(1);            // 把上一次的记录读回来
        gReady = true;
    }

    void FlushFileSystem()
    {
        if (!gReady) return;
        JsSyncFs(0);            // 交还给 IndexedDB
    }

    bool Ready() { return gReady; }

#endif
}