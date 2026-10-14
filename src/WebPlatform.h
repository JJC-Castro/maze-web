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

    // 每帧调一次：让 raylib 的 drawing buffer 跟着浏览器窗口走。
    // 不这么做的话，Emscripten 的 GLFW 只改 canvas 的 CSS 尺寸（noResize），
    // 真正的渲染分辨率还停在初始的 1280x720，浏览器把它拉伸到 1080p/2K 窗口
    // —— 画面就是这么糊掉的。桌面版是空实现。
    void SyncCanvasToWindow();

    // 鼠标"真的"被锁定了吗（游戏拿它当"能不能转视角"的开关）
    //
    // 为什么不能直接用 raylib 的 IsCursorHidden()：
    //   桌面版的 DisableCursor() 会把 CORE.Input.Mouse.cursorHidden 置 true，
    //   于是 IsCursorHidden() 恰好等于"锁定状态"；
    //   但 Web 后端的 DisableCursor() 只调 emscripten_request_pointerlock()，
    //   从不动 cursorHidden —— 浏览器里它恒为 false，游戏就以为鼠标永远没锁上，
    //   鼠标怎么拖视角都不动。所以 Web 版直接问浏览器：
    //   document.pointerLockElement 是不是我们这块 canvas。
    bool CursorLocked();
}