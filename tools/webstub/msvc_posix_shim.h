#pragma once
/* ===========================================================================
 *  msvc_posix_shim.h —— 只在「Web 分支语法自检」目标里被 /FI 强制包含
 *
 *  MAZE_WEB 分支写的是 POSIX / Emscripten 那套（localtime_r、::mkdir(path, 0777)），
 *  MSVC 没有这些符号。为了能用 MSVC 先把语法过一遍，这里补两个等价物。
 *  真正编 Web 版时这个头根本不会被包含（只有 web_syntax_check 目标 /FI 它）。
 * =========================================================================== */
#if defined(_MSC_VER)
#include <direct.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

/* MSVC 的 localtime_s 参数顺序反着来：localtime_s(dst, src) */
#ifndef MAZE_WEBSTUB_LOCALTIME_R
#define MAZE_WEBSTUB_LOCALTIME_R
static __inline struct tm* localtime_r(const time_t* t, struct tm* out)
{
    if (t == 0 || out == 0) return 0;
    return (localtime_s(out, t) == 0) ? out : 0;
}

/* POSIX: mkdir(path, mode)；MSVC: _mkdir(path) */
static __inline int mkdir(const char* path, int mode)
{
    (void)mode;
    return _mkdir(path);
}
#endif
#endif