<#
    NATextGenerator 构建脚本(Windows / MinGW-w64)

    用法:
        powershell -ExecutionPolicy Bypass -File build.ps1
        powershell -ExecutionPolicy Bypass -File build.ps1 -Clean

    步骤:
        1. 把 web/ 下的前端资源内嵌成 src/web_assets.cpp
        2. 用 windres 编译图标资源
        3. 用 g++ 编译并静态链接 FreeType
#>
param(
    [switch]$Clean,
    [switch]$NoWeb,
    [string]$Out = 'NATextGenerator.exe'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $root

$objDir = Join-Path $root 'build'
if ($Clean -and (Test-Path $objDir)) {
    Remove-Item -Recurse -Force $objDir
}
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

# 1) 内嵌前端资源
if (-not $NoWeb) {
    $py = Get-Command python -ErrorAction SilentlyContinue
    if ($py) {
        & python (Join-Path $root 'tools\embed_web.py')
        if ($LASTEXITCODE -ne 0) { throw 'embed_web.py 失败' }
    }
    else {
        Write-Warning '未找到 python，跳过前端资源内嵌(将使用已有的 src/web_assets.cpp)'
    }
}

# 2) 图标资源
$res = Join-Path $objDir 'app.res'
$windres = Get-Command windres -ErrorAction SilentlyContinue
if ($windres) {
    # 注意: --preprocessor= 必须用等号形式，否则包含空格的路径会被拆开
    & windres "--preprocessor=gcc.exe -E -xc-header -DRC_INVOKED" `
        -i 'NATextGenerator_private.rc' -o $res -O coff
    if ($LASTEXITCODE -ne 0) {
        Write-Warning 'windres 失败，生成的可执行文件将没有图标'
        $res = $null
    }
}
else {
    Write-Warning '未找到 windres，生成的可执行文件将没有图标'
    $res = $null
}

# 3) 编译链接
$srcs = Get-ChildItem -Path (Join-Path $root 'src') -Filter *.cpp | ForEach-Object { $_.FullName }
$includes = @(
    '-I', (Join-Path $root 'libs\include\freetype2')
)
$libs = @(
    (Join-Path $root 'libs\lib\libfreetype.a'),
    '-static-libstdc++', '-static-libgcc',
    '-lshell32', '-lws2_32'
)

$gppArgs = @('-std=c++17', '-O2', '-Wall', '-Wextra') + $includes + $srcs
if ($res) { $gppArgs += $res }
$gppArgs += @('-o', $Out) + $libs

Write-Host ("g++ " + ($srcs -join ' ')) -ForegroundColor DarkGray
& g++ @gppArgs
if ($LASTEXITCODE -ne 0) {
    Write-Host ''
    Write-Host '编译失败。常见原因:' -ForegroundColor Yellow
    Write-Host '  * NATextGenerator.exe 正在运行(双击过)，链接器无法覆盖——请先关闭程序再重试'
    Write-Host '  * web/ 里的前端文件有问题(可先跑 node --check web\app.js)'
    throw '编译失败'
}

$exe = Join-Path $root $Out
$size = [math]::Round((Get-Item $exe).Length / 1MB, 2)
Write-Host "构建成功: $Out ($size MB)" -ForegroundColor Green
