#pragma once
/* ===========================================================================
 *  emscripten.h（假货）—— 只为了在桌面编译器上"过一遍" MAZE_WEB 分支的语法
 *
 *  为什么需要它：
 *    #ifdef MAZE_WEB 里的代码只有 emcc 才会编。等真正跑到 emcc 才发现写错一个
 *    字母，代价是重跑一次几分钟的 wasm 构建（还得先有那 668MB 的工具链）。
 *    这里把用到的 emscripten API 做成空壳，让 MSVC 先把语法和类型检查做完：
 *
 *      cmake --build build\desktop --config Release --target web_syntax_check
 *
 *  ⚠ 只做语法检查，绝不能链接进任何可执行文件（CMake 里是 EXCLUDE_FROM_ALL
 *    的 OBJECT 库）。EM_ASM/EM_JS 里的 JS 代码在这里是被丢掉的。
 * =========================================================================== */
#include <stddef.h>
#include <stdlib.h>

/* ---- JS 内联宏：真身会把 JS 拼进 wasm，这里直接丢掉 ---- */
#define EM_ASM(...)                    ((void)0)
#define EM_ASM_INT(...)                (0)
#define EM_ASM_DOUBLE(...)             (0.0)
#define EM_ASM_PTR(...)                ((void*)0)
#define EM_JS(ret, name, params, body) ret name params { (void)0; }

#define EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_RESULT_SUCCESS 0

/* ---- Fetch API（leaderboard 用） ---- */
typedef struct emscripten_fetch_t
{
    void*        id;
    const char*  url;
    const char*  data;
    unsigned int numBytes;
    unsigned int status;
    int          statusText;
    int          totalBytes;
    int          dataOffset;
    void*        userData;
    void*        userData2;
} emscripten_fetch_t;

typedef struct emscripten_fetch_attr_t
{
    char         attr[32];
    char         requestMethod[32];
    char         requestUrl[256];
    int          attributes;
    void (*onsuccess)(emscripten_fetch_t* fetch);
    void (*onerror)(emscripten_fetch_t* fetch);
    void (*onprogress)(emscripten_fetch_t* fetch);
    const char*  requestData;
    size_t       requestDataSize;
    const char* const* requestHeaders;
    const char*  overriddenMimeType;
    void*        userData;
} emscripten_fetch_attr_t;

#define EMSCRIPTEN_FETCH_LOAD_TO_MEMORY 0x00000004
#define EMSCRIPTEN_FETCH_STREAM_DATA    0x00000001
#define EMSCRIPTEN_FETCH_SYNCHRONOUS    0x00000002

static inline void emscripten_fetch_attr_init(emscripten_fetch_attr_t* attr)
{
    if (attr != NULL) { attr->attributes = 0; attr->userData = NULL; }
}

static inline emscripten_fetch_t* emscripten_fetch(const emscripten_fetch_attr_t* attr,
                                                   const char* url)
{
    (void)attr; (void)url;
    return NULL;
}

static inline void emscripten_fetch_close(emscripten_fetch_t* fetch) { (void)fetch; }
static inline void emscripten_run_script(const char* script) { (void)script; }
static inline int  emscripten_run_script_int(const char* script) { (void)script; return 0; }