// ===========================================================================
//  WebPlatform.cpp —— IndexedDB 持久化（IDBFS）
// ===========================================================================
#include "WebPlatform.h"

#include "raylib.h"     // GetScreenWidth / SetWindowSize（SyncCanvasToWindow 用）

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
    void SyncCanvasToWindow() {}       // 桌面版：窗口就是窗口，没什么要同步的
    bool CursorLocked() { return IsCursorHidden(); }   // 桌面版：cursorHidden 就是锁定状态

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

    // -----------------------------------------------------------------------
    //  1 个渲染像素 = 1 个 CSS 像素
    //    Emscripten 的 GLFW 处理窗口 resize 时调的是 Browser.setCanvasSize(w, h, true)
    //    —— 那个 true 表示"只改 CSS 尺寸，别动 canvas 的像素尺寸"。于是 drawing
    //    buffer 一直是 InitWindow 时的尺寸，浏览器把这张小位图放大铺满窗口 → 糊。
    //    这里每帧对一次尺寸，不一致就让 raylib 重设窗口（它内部会重设 canvas 像素
    //    尺寸并重算视口）。对比的是 raylib 自己记的尺寸，所以不会抖。
    // -----------------------------------------------------------------------
    void SyncCanvasToWindow()
    {
        const int w = (int)EM_ASM_INT({
            return (typeof window === 'undefined') ? 0 : (window.innerWidth | 0);
        });
        const int h = (int)EM_ASM_INT({
            return (typeof window === 'undefined') ? 0 : (window.innerHeight | 0);
        });
        if (w < 320 || h < 240) return;                 // 窗口最小化时别跟着缩到 0
        if (w == GetScreenWidth() && h == GetScreenHeight()) return;
        SetWindowSize(w, h);
    }

    // -----------------------------------------------------------------------
    //  鼠标锁定状态
    //    raylib 内部的 EmscriptenPointerlockCallback 看的就是
    //    document.pointerLockElement，游戏侧够不着那个内部字段，所以这里
    //    问浏览器同一个事实。
    // -----------------------------------------------------------------------
    bool CursorLocked()
    {
        return EM_ASM_INT({ return (document.pointerLockElement != null) ? 1 : 0; }) != 0;
    }

#endif
}