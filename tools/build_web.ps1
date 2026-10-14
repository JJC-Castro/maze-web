# ===========================================================================
#  build_web.ps1 —— 构建 Web 版（Emscripten）
#
#  前置：
#    1. emsdk 装好并 activate 过（默认找 D:\emsdk，可用 -EmsdkRoot 指定）
#    2. cmake / ninja 在 PATH 里
#
#  用法：
#    powershell -ExecutionPolicy Bypass -File tools\build_web.ps1            # 完整构建
#    powershell -ExecutionPolicy Bypass -File tools\build_web.ps1 -ProbeOnly # 只构建探针
# ===========================================================================
param(
    [string]$EmsdkRoot = 'D:\emsdk',
    [switch]$ProbeOnly,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

# ---- 1. 激活 emsdk ----
$emsdkEnv = Join-Path $EmsdkRoot 'emsdk_env.ps1'
if (-not (Test-Path $emsdkEnv)) {
    throw "找不到 $emsdkEnv —— 先装 emsdk：git clone https://github.com/emscripten-core/emsdk.git $EmsdkRoot ; cd $EmsdkRoot ; .\emsdk install latest ; .\emsdk activate latest"
}
Write-Host '[1/3] 激活 emsdk ...' -ForegroundColor Cyan
& $emsdkEnv | Out-Null

if (-not (Get-Command emcc -ErrorAction SilentlyContinue)) {
    throw 'emcc 不在 PATH 里，emsdk activate 可能失败了'
}
Write-Host ("      emcc -> " + (Get-Command emcc).Source) -ForegroundColor DarkGray

# ---- 2. 配置 ----
$BuildDir = Join-Path $Root 'build\web'
if ($Clean -and (Test-Path $BuildDir)) {
    Remove-Item -LiteralPath $BuildDir -Recurse -Force
}
Write-Host '[2/3] CMake 配置 ...' -ForegroundColor Cyan
& cmake -S $Root -B $BuildDir -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="$EmsdkRoot/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
if ($LASTEXITCODE -ne 0) { throw 'CMake 配置失败' }

# ---- 3. 编译 ----
Write-Host '[3/3] 编译 ...' -ForegroundColor Cyan
if ($ProbeOnly) {
    & cmake --build $BuildDir --target probe
} else {
    & cmake --build $BuildDir --target maze
}
if ($LASTEXITCODE -ne 0) { throw '编译失败' }

Write-Host ''
Write-Host '构建完成，产物在:' -ForegroundColor Green
Get-ChildItem $BuildDir -Filter '*.html' | ForEach-Object {
    Write-Host ("  " + $_.FullName) -ForegroundColor Green
}
Write-Host ''
Write-Host '本地预览：  python tools\serve.py --open' -ForegroundColor Yellow