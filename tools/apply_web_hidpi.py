# -*- coding: utf-8 -*-
"""apply_web_hidpi.py —— 给 raylib 的 Web 后端补两件浏览器里缺的事。

raylib 源码是 vendored 进 third_party/ 的（已经提交），这个脚本留着是为了：
  · 可复现 —— 将来重新 vendor 一份 raylib，跑一次就恢复
  · 可检查 —— `--check` 只验证锚点，可以当 CI 守卫
只动 rcore_web.c（Web 后端），桌面后端 rcore_desktop_glfw.c 一行不改。

  1. HiDPI：canvas 的绘制缓冲区从来只等于"逻辑像素"。屏幕缩放 150% 时，
     浏览器把这张小位图放大 1.5 倍铺到屏幕像素上 —— 整幅画面（尤其中文小字）全糊。
     这里照桌面版 FLAG_WINDOW_HIGHDPI 的三件套补齐：
         screen      = 逻辑像素（鼠标坐标、UI 布局都用它）
         render/FBO  = 物理像素 = 逻辑尺寸 x devicePixelRatio
         screenScale = 逻辑 -> 物理 的缩放矩阵（BeginDrawing 里会挂上去）
     CSS 尺寸仍钉在逻辑像素，所以鼠标坐标和 UI 布局都不用跟着改。

  2. 指针锁定状态外露：Web 的 DisableCursor() 只调 emscripten_request_pointerlock()，
     从不动 CORE.Input.Mouse.cursorHidden —— 而游戏侧一直拿 IsCursorHidden() 当
     "鼠标锁着"的标志用，于是浏览器里它恒为 false，视角永远拖不动。
     这里顺手记一个 Module.mazeWantPointerLock，给 shell.html 判断
     "这一下点击要不要补一次 requestPointerLock()"用。

用法：
    python tools/apply_web_hidpi.py --check
    python tools/apply_web_hidpi.py
"""
import io
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, 'third_party', 'raylib', 'src', 'platforms', 'rcore_web.c')


class Patch:
    """一处精确替换：old 必须命中恰好 count 次，否则整体失败。"""

    def __init__(self, old, new, note, count=1):
        self.old = old.replace('\n', '\r\n')
        self.new = new.replace('\n', '\r\n')
        self.note = note
        self.count = count


PATCHES = [

    # ----------------------------------------------------------- 函数前置声明
    Patch(
"""static void WindowContentScaleCallback(GLFWwindow *window, float scalex, float scaley); // GLFW3 Window Content Scale Callback, runs when window changes scale""",
"""static void WindowContentScaleCallback(GLFWwindow *window, float scalex, float scaley); // GLFW3 Window Content Scale Callback, runs when window changes scale
static void WindowFramebufferSizeCallback(GLFWwindow *window, int width, int height);  // GLFW3 Window Framebuffer Size Callback, runs when the drawing buffer changes size
static void ApplyCanvasHiDPI(void);                                                    // Web: 按 devicePixelRatio 把 canvas 绘制缓冲区提到物理像素（等价于桌面版的 HiDPI 缩放）""",
        'rcore_web.c: 加 ApplyCanvasHiDPI / WindowFramebufferSizeCallback 的前置声明'),

    # --------------------------------------------------------------- 实现本体
    Patch(
"""// GLFW3: Called on window content (framebuffer) scaled
static void WindowContentScaleCallback(GLFWwindow *window, float scalex, float scaley)
{
    CORE.Window.screenScale = MatrixScale(scalex, scaley, 1.0f);
}""",
"""// GLFW3: Called on window content (framebuffer) scaled
static void WindowContentScaleCallback(GLFWwindow *window, float scalex, float scaley)
{
    CORE.Window.screenScale = MatrixScale(scalex, scaley, 1.0f);
}

// Web: 当前屏幕缩放系数（Windows 150% 缩放的系统上是 1.5）
static float WebDevicePixelRatio(void)
{
    float dpr = (float)EM_ASM_DOUBLE({ return (window.devicePixelRatio || 1.0); });

    if (dpr < 1.0f) dpr = 1.0f;
    if (dpr > 2.0f) dpr = 2.0f;     // 上限 2x：再高只是白烧 GPU，肉眼基本看不出差别

    return dpr;
}

// Web: 按 devicePixelRatio 把 canvas 的绘制缓冲区提到"物理像素"。
//
// 桌面版对应的是 FLAG_WINDOW_HIGHDPI 那三件套：
//     screen      = 逻辑像素（鼠标坐标、UI 布局都用它）
//     render/FBO  = 物理像素（真正的绘制缓冲区）
//     screenScale = 逻辑 -> 物理 的缩放矩阵（BeginDrawing 里挂上去）
// 浏览器里 raylib 三件都没做过：缓冲区永远等于逻辑像素，于是 150% 缩放的屏幕上
// 浏览器把整幅画面放大 1.5 倍再铺到屏幕像素 —— 整幅（尤其中文小字）全糊。
// CSS 尺寸仍钉在逻辑像素，所以鼠标坐标、UI 布局都不用跟着改。
static void ApplyCanvasHiDPI(void)
{
    if (!CORE.Window.ready) return;

    const float dpr = WebDevicePixelRatio();

    const int logicalWidth = (int)CORE.Window.screen.width;
    const int logicalHeight = (int)CORE.Window.screen.height;
    if ((logicalWidth <= 0) || (logicalHeight <= 0)) return;

    const int bufferWidth = (int)((logicalWidth*dpr) + 0.5f);
    const int bufferHeight = (int)((logicalHeight*dpr) + 0.5f);

    // 读出来不一样才写：给 canvas.width/height 赋值会重建整块绘制缓冲区，代价不小
    EM_ASM({
        var canvas = Module.canvas;
        if (!canvas) return;

        if (canvas.width != $0) canvas.width = $0;          // 绘制缓冲区 = 物理像素
        if (canvas.height != $1) canvas.height = $1;
        if (canvas.style.width != ($2 + "px")) canvas.style.setProperty("width", $2 + "px", "important");
        if (canvas.style.height != ($3 + "px")) canvas.style.setProperty("height", $3 + "px", "important");
    }, bufferWidth, bufferHeight, logicalWidth, logicalHeight);

    CORE.Window.currentFbo.width = bufferWidth;
    CORE.Window.currentFbo.height = bufferHeight;
    CORE.Window.screenScale = MatrixScale(dpr, dpr, 1.0f);

    // 视口和正交投影跟着缓冲区走（SetupViewport 会把 render.width/height 设成物理像素）
    if ((CORE.Window.render.width != bufferWidth) || (CORE.Window.render.height != bufferHeight))
    {
        SetupViewport(bufferWidth, bufferHeight);
    }
}

// GLFW3: Called when the drawing buffer (framebuffer) size changes
// NOTE: 故意不用回调参数 —— 浏览器里 framebuffer 尺寸由 devicePixelRatio 决定，
//       而 Emscripten 的 GLFW 在非 HiDPI 感知模式下报上来的其实是窗口逻辑尺寸。
//       统一交给 ApplyCanvasHiDPI() 按当前 dpr 重算，对上层永远是幂等的。
static void WindowFramebufferSizeCallback(GLFWwindow *window, int width, int height)
{
    (void)window;
    (void)width;
    (void)height;

    ApplyCanvasHiDPI();

    CORE.Window.resizedLastFrame = true;
}""",
        'rcore_web.c: 补 ApplyCanvasHiDPI + framebuffer 尺寸回调'),

    # ----------------------------------------------------- 注册这两个回调
    Patch(
"""    // Set window callback events
    glfwSetWindowSizeCallback(platform.handle, WindowSizeCallback);
    glfwSetWindowIconifyCallback(platform.handle, WindowIconifyCallback);
    glfwSetWindowFocusCallback(platform.handle, WindowFocusCallback);
    glfwSetDropCallback(platform.handle, WindowDropCallback);

    if (FLAG_IS_SET(CORE.Window.flags, FLAG_WINDOW_HIGHDPI))
    {
        // Window content (framebuffer) scale callback
        glfwSetWindowContentScaleCallback(platform.handle, WindowContentScaleCallback);
    }""",
"""    // Set window callback events
    glfwSetWindowSizeCallback(platform.handle, WindowSizeCallback);
    glfwSetWindowIconifyCallback(platform.handle, WindowIconifyCallback);
    glfwSetWindowFocusCallback(platform.handle, WindowFocusCallback);
    glfwSetDropCallback(platform.handle, WindowDropCallback);

    // Web 上和"分辨率"有关的两个回调无条件注册，跟 FLAG_WINDOW_HIGHDPI 无关：
    //   绘制缓冲区（物理像素）变了要重算 canvas 尺寸和视口；
    //   浏览器缩放（devicePixelRatio）变了也一样。
    glfwSetFramebufferSizeCallback(platform.handle, WindowFramebufferSizeCallback);
    glfwSetWindowContentScaleCallback(platform.handle, WindowContentScaleCallback);""",
        'rcore_web.c: 无条件注册 framebuffer / content-scale 回调'),

    # --------------------------------------------- 启动时先按 dpr 提一次分辨率
    Patch(
"""    // Trigger resize callback to force initial size
    EmscriptenResizeCallback(EMSCRIPTEN_EVENT_RESIZE, NULL, NULL);""",
"""    // Trigger resize callback to force initial size
    EmscriptenResizeCallback(EMSCRIPTEN_EVENT_RESIZE, NULL, NULL);

    // 上一步只把 canvas 缓冲区设成逻辑尺寸，这里按 devicePixelRatio 提到物理像素
    ApplyCanvasHiDPI();""",
        'rcore_web.c: 初始化后按 dpr 提一次分辨率'),

    # --------------------------------- 窗口尺寸变化后，缓冲区也要回到物理像素
    Patch(
"""    glfwSetWindowSize(platform.handle, CORE.Window.screen.width, CORE.Window.screen.height);""",
"""    glfwSetWindowSize(platform.handle, CORE.Window.screen.width, CORE.Window.screen.height);

    // glfwSetWindowSize() 会把 canvas 缓冲区改回逻辑尺寸，这里再按 dpr 提到物理像素
    ApplyCanvasHiDPI();""",
        'rcore_web.c: resize 之后重算物理像素缓冲区'),

    # ------------------------------------------------- 记录"游戏想要锁定鼠标"
    Patch(
"""void EnableCursor(void)
{
    emscripten_exit_pointerlock();""",
"""void EnableCursor(void)
{
    emscripten_exit_pointerlock();

    // 告诉 shell.html：游戏现在是明确"解锁"状态，别再补请求 pointer lock 了
    // （菜单里把指针锁住的话鼠标坐标会卡在画面中央，菜单就点不动）
    EM_ASM({ Module.mazeWantPointerLock = false; });""",
        'rcore_web.c: EnableCursor 记录"不想要锁定"'),

    Patch(
"""void DisableCursor(void)
{
    emscripten_request_pointerlock(platform.canvasId, 1);""",
"""void DisableCursor(void)
{
    // 记下"游戏想要锁定鼠标"：真正锁上还得靠用户手势，被浏览器拒的时候
    // shell.html 会在下一次点击画面时补一次 requestPointerLock()
    EM_ASM({ Module.mazeWantPointerLock = true; });

    emscripten_request_pointerlock(platform.canvasId, 1);""",
        'rcore_web.c: DisableCursor 记录"想要锁定"'),]


def apply(check_only=False):
    done = 0
    for p in PATCHES:
        with io.open(TARGET, encoding='utf-8', newline='') as f:
            text = f.read()

        if text.count(p.new) == p.count:
            print('[skip] ' + p.note + ' (已经打过了)')
            done += 1
            continue

        n = text.count(p.old)
        if n != p.count:
            raise SystemExit('[ANCHOR FAIL] 期望 %d 处，实际 %d 处\n--- 锚点 ---\n%s'
                             % (p.count, n, p.old[:500]))

        if check_only:
            print('[ok  ] ' + p.note)
        else:
            text = text.replace(p.old, p.new, p.count)
            with io.open(TARGET, 'w', encoding='utf-8', newline='') as f:
                f.write(text)
            print('[fix ] ' + p.note)
        done += 1

    print('---')
    print('%d 个补丁%s' % (done, '锚点全部命中' if check_only else '处理完毕'))


if __name__ == '__main__':
    apply('--check' in sys.argv)