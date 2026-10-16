cmake_minimum_required(VERSION 3.16)
project(maze_web LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
#  raylib：源码内置，桌面版和 Web 版共用同一份，保证两边行为一致
# ---------------------------------------------------------------------------
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
set(SUPPORT_FILEFORMAT_OBJ ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/raylib EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
#  游戏源码
#
#  ⚠ RL/RlBridge.cpp 是 Windows 裸 Winsock 写的 TCP 服务端（WSAStartup/socket/
#    accept）。浏览器既没有裸 socket、也不能当 server，所以 Web 版不编译它。
#    RL 的离线推理（RLNet / RlAgent / RlWalker）是纯 C++，原样保留 ——
#    「机器人代打」在网页版里照常可用，只有「训练桥」被砍掉。
# ---------------------------------------------------------------------------
set(MAZE_SOURCES
    src/main.cpp
    src/Account.cpp
    src/Enemy.cpp
    src/Entity.cpp
    src/FootstepAudio.cpp
    src/GameTimer.cpp
    src/LightSource.cpp
    src/Maze.cpp
    src/Menu.cpp
    src/Nailong.cpp
    src/NailongRig.cpp
    src/Player.cpp
    src/Save.cpp
    src/SfxAudio.cpp
    src/TextInput.cpp
    src/TPCamera.cpp
    src/Treasure.cpp
    src/UIFont.cpp
    src/Leaderboard.cpp
    src/Sha256.cpp
    src/WebPlatform.cpp
    src/RL/MazeSolver.cpp
    src/RL/RLNet.cpp
    src/RL/RlAgent.cpp
    src/RL/RlTelemetry.cpp
    src/RL/RlWalker.cpp
)


add_executable(maze ${MAZE_SOURCES})
target_include_directories(maze PRIVATE src)
# raylib 的头文件（raylib.h / raymath.h）在源码树里，显式加进来，
# 不依赖 raylib 自己的 target 是否把 BUILD_INTERFACE 传出来
target_include_directories(maze PRIVATE third_party/raylib/src)

# 真正把 raylib 链进来（桌面 = 源码静态库；Web = emcc 静态库）
target_link_libraries(maze PRIVATE raylib)

# ---------------------------------------------------------------------------
#  RlBridge 换实现：
#    桌面版 = RlBridge.cpp      （Windows 裸 Winsock 的 TCP 服务端）
#    Web 版 = RlBridge_web.cpp  （同接口的桩，Start() 永远返回 false）
#  头文件 RlBridge.h 是纯 std 的，两边共用，所以 main.cpp 里一行都不用改。
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    target_sources(maze PRIVATE src/RL/RlBridge_web.cpp)
else()
    target_sources(maze PRIVATE src/RL/RlBridge.cpp)
endif()

# ---------------------------------------------------------------------------
#  Web（Emscripten）
# ---------------------------------------------------------------------------
if(EMSCRIPTEN)
    # MAZE_WEB 打开后：关掉后台线程（改成同步一拍）、走 IndexedDB 存档、字体用内置 TTF
    target_compile_definitions(maze PRIVATE MAZE_WEB=1)

    set_target_properties(maze PROPERTIES
        SUFFIX       ".html"
        OUTPUT_NAME  "index"
    )

    target_link_libraries(maze PRIVATE "-sUSE_GLFW=3")

    # 组装 Web 用的资源目录（不污染仓库，直接组到 build 目录里）：
    #   audio  -> tools/make_web_assets.py 生成的瘦身版（48kHz 立体声 PCM 降到
    #             22kHz 单声道/BGM 立体声，10.3MB -> 3.4MB，听感基本无损）
    #   其余   -> 原样复制
    # 桌面版完全不受影响：桌面版 preload 的还是原版 assets/。
    set(MAZE_WEB_ASSETS "${CMAKE_CURRENT_BINARY_DIR}/web_assets")
    add_custom_target(maze_web_assets
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${MAZE_WEB_ASSETS}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets_web/audio"   "${MAZE_WEB_ASSETS}/audio"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/models"      "${MAZE_WEB_ASSETS}/models"
        # assets_web/models 里是瘦身过的模型（内嵌死贴图已摘），覆盖上去。
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets_web/models" "${MAZE_WEB_ASSETS}/models"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"       "${MAZE_WEB_ASSETS}/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/assets/rl"          "${MAZE_WEB_ASSETS}/rl"
        COMMENT "组装 Web 资源目录（音频用瘦身版）"
        VERBATIM
    )
    add_dependencies(maze maze_web_assets)

    target_link_options(maze PRIVATE
        "-sUSE_GLFW=3"
        "-sALLOW_MEMORY_GROWTH=1"       # 迷宫 + 模型 + 音频，16MB 默认堆不够
        "-sINITIAL_MEMORY=134217728"
        "-sSTACK_SIZE=5242880"
        "-sFORCE_FILESYSTEM=1"          # 让 std::ofstream 有 / 可写，交给 IDBFS
        "-sEXIT_RUNTIME=0"
        "-sASSERTIONS=0"
                "-sGL_ENABLE_GET_PROC_ADDRESS=1"
        # WebGL 版本：raylib for Web 编的是 GLES2 代码，WebGL1/2 上下文都能跑。
        # 写死 1~2 让 Emscripten 按浏览器实际能力挑，别把只有 WebGL1 的机器挡在门外。
        "-sMIN_WEBGL_VERSION=1"
        "-sMAX_WEBGL_VERSION=2"
        # ⚠ 必须开 ASYNCIFY：raylib 的 WebAudio 后端（miniaudio）在 InitAudioDevice()
        #   里是这么等初始化的 —— while (initResult == MA_BUSY) emscripten_sleep(1);
        #   没开 ASYNCIFY 时 emscripten_sleep 直接 abort（浏览器里表现为"引擎启动失败"）。
        #   代价：wasm 变大一点、慢一点，但换来音频能用。
        "-sASYNCIFY"
        "-sASYNCIFY_IMPORTS=emscripten_sleep"
        "-sASYNCIFY_IGNORE_INDIRECT"
        "-sFETCH=1"                     # 全网榜：emscripten_fetch 走 XHR
        "-lidbfs.js"                    # IndexedDB 持久化（log.csv / 存档）
        # 首屏时长 = 下载 index.data 的时长（国内到 GitHub Pages 实测只有 82KB/s）。
        # -sLZ4=1：整个资源包先 LZ4 压一道，浏览器下完再解压，能省两成左右。
        # 注意 --preload-file 只能在 target_link_options 里出现一次（CMake 会把
        # 重复的链接选项去重），所以资源统一指向构建期组装出来的 web_assets/。
        "-sLZ4=1"
        "--preload-file" "${MAZE_WEB_ASSETS}@assets"
        "--shell-file"   "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
    )

    # 改 shell.html 也要让它重新链接一次：emcc 是在**链接**阶段把 shell 注进
    # index.html 的，CMake 默认不知道这层依赖 —— 只改 shell.html 时 ninja 认为
    # 无事可做，本地预览就还是旧页面（榜单页/指针锁定那些前端改动会被"吃掉"）。
    set_property(TARGET maze APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")

    # index.html 里引的是同目录的 config.js，构建完把它放到产物旁边
    add_custom_command(TARGET maze POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/web/config.js"
                "$<TARGET_FILE_DIR:maze>/config.js")
else()
    # 桌面版：直接双击 exe 就能跑，资源按相对路径找
    if(MSVC)
        target_compile_options(maze PRIVATE /utf-8 /W3)
    endif()
    add_custom_command(TARGET maze POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/assets"
                "$<TARGET_FILE_DIR:maze>/assets")
endif()

# ---------------------------------------------------------------------------
#  探针：只依赖 raylib 的最小三角，用来单独验证 emcc 工具链
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
#  Web 分支语法自检（纯桌面）：用 tools/webstub 里的假 emscripten 头，
#  让 MSVC 把 #ifdef MAZE_WEB 里的代码也编一遍。等 emcc 才发现拼写错误太贵。
#    cmake --build build\desktop --config Release --target web_syntax_check
#  注意：这是个 OBJECT 库，只编不链，绝不能进可执行文件。
# ---------------------------------------------------------------------------
add_library(web_syntax_check OBJECT EXCLUDE_FROM_ALL
    ${MAZE_SOURCES}
    src/RL/RlBridge_web.cpp
)
target_compile_definitions(web_syntax_check PRIVATE MAZE_WEB=1)
target_include_directories(web_syntax_check PRIVATE tools/webstub src third_party/raylib/src)
if(MSVC)
    # /FI：把 POSIX 垫片塞进来，MSVC 才有 localtime_r / ::mkdir(path, mode)
    target_compile_options(web_syntax_check PRIVATE /utf-8 /FI${CMAKE_CURRENT_SOURCE_DIR}/tools/webstub/msvc_posix_shim.h)
endif()

# ---------------------------------------------------------------------------
#  自检（纯桌面）：验自己手写的 SHA-256 / HMAC-SHA256 跟 Python hashlib 一致。
#  排行榜签名全靠它，算错了成绩会被服务端 403 拒收，所以单独留个靶子。
#    cmake --build build\desktop --config Release --target selftest
#    build\desktop\Release\selftest.exe > out.txt
#    python tools\selftest\check.py out.txt
# ---------------------------------------------------------------------------
add_executable(selftest EXCLUDE_FROM_ALL
    tools/selftest/selftest.cpp
    src/Sha256.cpp
)
target_include_directories(selftest PRIVATE src)
if(MSVC)
    target_compile_options(selftest PRIVATE /utf-8)
endif()

if(EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/probe/probe.cpp")
    add_executable(probe probe/probe.cpp)
    set_target_properties(probe PROPERTIES SUFFIX ".html" OUTPUT_NAME "probe")
    target_link_libraries(probe PRIVATE raylib "-sUSE_GLFW=3")
    target_link_options(probe PRIVATE
        "-sUSE_GLFW=3" "-sALLOW_MEMORY_GROWTH=1"
        "-sMIN_WEBGL_VERSION=1" "-sMAX_WEBGL_VERSION=2"
        "-sGL_ENABLE_GET_PROC_ADDRESS=1"
        "--preload-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets/models@assets/models"
        "--shell-file"   "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
endif()