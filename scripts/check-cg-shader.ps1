#requires -Version 5.1
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Install the Windows C++ build tools first.' }
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$clang = Join-Path $repoRoot '.tools\llvm\bin\clang++.exe'
if (-not (Test-Path -LiteralPath $clang)) {
    $clang = (Get-Command clang++.exe -ErrorAction Stop).Source
}
$out = Join-Path $repoRoot '.dev\shader-check'
[void][IO.Directory]::CreateDirectory($out)
$executable = Join-Path $out 'cg_shader_tests.exe'
$arguments = @('-std=c++20', '-O1', '-fuse-ld=lld', '-flto=thin',
    ('-I' + (Join-Path $repoRoot 'aurora-main\lib')),
    ('-I' + (Join-Path $repoRoot 'aurora-main\include')),
    ('-I' + (Join-Path $repoRoot 'build\windows\_deps\xxhash-src')),
    (Join-Path $repoRoot 'aurora-main\tests\cg_shader_check.cpp'),
    (Join-Path $repoRoot 'aurora-main\lib\gfx\cg_shader.cpp'),
    (Join-Path $repoRoot 'aurora-main\lib\gfx\port_capture.cpp'),
    (Join-Path $repoRoot 'build\windows\_deps\xxhash-build\xxhash.lib'),
    '-o', $executable)
& $clang @arguments
if ($LASTEXITCODE -ne 0) { throw 'Shader check compilation failed.' }
& $executable
if ($LASTEXITCODE -ne 0) { throw 'Shader conversion checks failed.' }
