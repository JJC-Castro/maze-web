# ===========================================================================
#  build_web.ps1 —— 构建 Web 版（Emscripten）
#
#  两种 emsdk 布局都认：
#    A) emsdk 正常装出来的：<EmsdkRoot>\emsdk_env.ps1 存在 -> 调它 activate
#    B) 手动解包：<EmsdkRoot>\upstream\emscripten\emcc.py 存在
#       （把 wasm-binaries.zip 直接解到 upstream\，绕开 emsdk 自带那个
#        17KB/s 的下载器；本脚本会自己写 .emscripten 配置）
#
#  用法：
#    powershell -ExecutionPolicy Bypass -File tools\build_web.ps1              # 全量
#    powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -ProbeOnly   # 只编探针
#    powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -Clean        # 先清 build\web
# ===========================================================================
param(
    [string]$EmsdkRoot = 'D:\emsdk',
    [string]$PythonExe = '',
    [string]$NodeExe   = '',
    [switch]$ProbeOnly,
    [switch]$Clean
)

# 注意：不能用 'Stop' —— 原生命令（emcc / cmake / ninja）往 stderr 写一行普通信息，
# PS 5.1 就会当成 NativeCommandError 直接中断脚本。改成 Continue + 显式查 $LASTEXITCODE。
$ErrorActionPreference = 'Continue'
$Root = Split-Path -Parent $PSScriptRoot

function Find-Exe([string]$Name, [string[]]$Candidates) {
    $c = Get-Command $Name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    foreach ($p in $Candidates) { if (Test-Path $p) { return $p } }
    return $null
}

# ---- 0. cmake / ninja ----
$cmake = Find-Exe 'cmake' @('E:\CLion\STM32CubeCLT_1.21.0\CMake\bin\cmake.exe')
$ninja = Find-Exe 'ninja' @('E:\CLion\STM32CubeCLT_1.21.0\Ninja\bin\ninja.exe')
if (-not $cmake) { throw '找不到 cmake' }
if (-not $ninja) { throw '找不到 ninja（-G Ninja 需要它）' }

# ---- 1. 准备 emcc 环境 ----
Write-Host '[1/3] 准备 emsdk / emcc ...' -ForegroundColor Cyan
$up = Join-Path $EmsdkRoot 'upstream'
$emccPy = Join-Path $up 'emscripten\emcc.py'
$emcc = $null

if (Test-Path $emccPy) {
    # ---- 布局 B：手动解包 ----
    if (-not $PythonExe) {
        $PythonExe = Find-Exe 'python' @('E:\Anaconda\python.exe', "$env:USERPROFILE\anaconda3\python.exe")
    }
    if (-not $PythonExe) { throw '手动布局需要 python 跑 emcc.py，用 -PythonExe 指定' }
    if (-not $NodeExe) {
        $NodeExe = Find-Exe 'node' @('E:\nodejs\node.exe', 'C:\Program Files\nodejs\node.exe')
    }
    if (-not $NodeExe) { throw '手动布局需要 node.exe，用 -NodeExe 指定' }

    $upFwd = $up.Replace('\', '/')
    $nodeFwd = $NodeExe.Replace('\', '/')
    $cfg = Join-Path $EmsdkRoot '.emscripten'
    $cfgText = @(
        '# 由 tools/build_web.ps1 生成（手动解包布局）',
        "LLVM_ROOT = '$upFwd/bin'",
        "BINARYEN_ROOT = '$upFwd'",
        "EMSCRIPTEN_ROOT = '$upFwd/emscripten'",
        "NODE_JS = '$nodeFwd'"
    ) -join "`r`n"
    Set-Content -LiteralPath $cfg -Value $cfgText -Encoding ASCII
    # emcc.exe 是个 24KB 的启动器，它自己按 PATH 找 python：得先把真 python 放到最前面，
    # 否则会撞上 Windows 那个"python 未安装"的应用商店占位符。
    $env:PATH = (Split-Path $PythonExe) + ';' + $env:PATH
    $env:EM_CONFIG = $cfg
    $env:PATH = (Join-Path $up 'emscripten') + ';' + (Join-Path $up 'bin') + ';' + $env:PATH
    # 6.0.12 的 releases 包里给的是原生 emcc.exe（老版本才是 emcc.bat）
    foreach ($cand in @('emcc.exe', 'emcc.bat')) {
        $p = Join-Path $up ("emscripten\" + $cand)
        if (Test-Path $p) { $emcc = $p; break }
    }
    if (-not $emcc) { throw "emcc.exe / emcc.bat 都没有 —— upstream 解包不完整？" }
    Write-Host "      布局 B（手动解包）：$emcc" -ForegroundColor DarkGray
}
else {
    # ---- 布局 A：emsdk 自己装的 ----
    $envScript = Join-Path $EmsdkRoot 'emsdk_env.ps1'
    if (-not (Test-Path $envScript)) {
        throw "既没有 $emccPy 也没有 $envScript —— 先装 emsdk（见 README 的 emsdk 一节）"
    }
    & $envScript | Out-Null
    $c = Get-Command emcc -ErrorAction SilentlyContinue
    if (-not $c) { throw 'emsdk activate 之后 emcc 还是不在 PATH 里' }
    $emcc = $c.Source
    Write-Host "      布局 A（emsdk）：$emcc" -ForegroundColor DarkGray
}

# 工具链文件就在 emcc 旁边，不依赖任何环境变量：
$emToolchain = Join-Path (Split-Path $emcc) 'cmake\Modules\Platform\Emscripten.cmake'
if (-not (Test-Path $emToolchain)) { throw "找不到 Emscripten.cmake：$emToolchain" }
Write-Host ('      ' + (& $emcc --version 2>&1 | Select-Object -First 1)) -ForegroundColor DarkGray

# ---- 2. CMake 配置 ----
$BuildDir = Join-Path $Root 'build\web'
if ($Clean -and (Test-Path $BuildDir)) {
    & $cmake -E rm -rf $BuildDir
}
Write-Host '[2/3] CMake 配置 ...' -ForegroundColor Cyan
& $cmake -S $Root -B $BuildDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    -DCMAKE_BUILD_TYPE=Release `
    "-DCMAKE_TOOLCHAIN_FILE=$emToolchain"
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }

# ---- 3. 编译 ----
Write-Host '[3/3] 编译 ...' -ForegroundColor Cyan
if ($ProbeOnly) {
    & $cmake --build $BuildDir --target probe
} else {
    & $cmake --build $BuildDir --target maze
}
if ($LASTEXITCODE -ne 0) { throw '编译失败' }

Write-Host ''
Write-Host '构建完成，产物：' -ForegroundColor Green
Get-ChildItem $BuildDir -Filter '*.html' | ForEach-Object {
    Write-Host ('  ' + $_.FullName) -ForegroundColor Green
}
Write-Host ''
Write-Host '本地预览：  python tools\serve.py --open' -ForegroundColor Yellow