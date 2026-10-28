#requires -Version 5.1
[CmdletBinding()]
param([string]$ToolchainRoot)
$ErrorActionPreference='Stop'
$repoRoot=Split-Path -Parent $PSScriptRoot
if(-not $ToolchainRoot){$ToolchainRoot=$repoRoot}
$vsPath=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vsPath) { throw 'Windows C++ build tools are required.' }
Import-Module (Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$clang=Join-Path $ToolchainRoot '.tools\llvm\bin\clang++.exe'
$out=Join-Path $repoRoot '.dev\rowell-checks'
[void][IO.Directory]::CreateDirectory($out)
$includes=@(('-I'+(Join-Path $repoRoot 'runtime\include')),('-I'+$repoRoot),
 ('-I'+(Join-Path $repoRoot 'runtime\src')),('-I'+(Join-Path $repoRoot 'android-source\runtime\include')),
 ('-I'+(Join-Path $repoRoot 'aurora-main\include')),('-I'+(Join-Path $repoRoot 'third_party\angle\include')),
 ('-I'+(Join-Path $ToolchainRoot 'build\windows\_deps\sdl3_prebuilt-src\include')))
$common=@('-std=c++20','-O1','-flto=thin','-fuse-ld=lld','-D_CRT_SECURE_NO_WARNINGS','-DNOMINMAX','-DWIN32_LEAN_AND_MEAN')
$logs=Join-Path $out 'bounded-log-check.exe'
$pacing=Join-Path $out 'frame-delta-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\frame_delta_check.cpp') '-o' $pacing
if($LASTEXITCODE -ne 0){throw 'Frame pacing checks failed to compile.'}
& $pacing
if($LASTEXITCODE -ne 0){throw 'Frame pacing checks failed.'}
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\bounded_log_check.cpp') '-o' $logs
if($LASTEXITCODE -ne 0){throw 'Bounded log checks failed to compile.'}
& $logs (Join-Path $out ('log-user-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Bounded log checks failed.'}
$sdl=Get-ChildItem -LiteralPath (Join-Path $ToolchainRoot 'build\windows\_deps\sdl3_prebuilt-src') -Filter SDL3.lib -Recurse -File | Select-Object -First 1 -ExpandProperty FullName
Copy-Item -LiteralPath (Join-Path $ToolchainRoot 'build\windows\SDL3.dll') -Destination $out -Force
$display=Join-Path $out 'pc-display-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\pc_display_check.cpp') (Join-Path $repoRoot 'runtime\src\rowell_touch.cpp') $sdl '-lshell32' '-o' $display
if($LASTEXITCODE -ne 0){throw 'PC display checks failed to compile.'}
& $display (Join-Path $out ('display-user-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'PC display checks failed.'}
$pad=Join-Path $out 'gamepad-bindings-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\gamepad_bindings_check.cpp') (Join-Path $repoRoot 'runtime\src\gamepad_bindings.cpp') $sdl '-o' $pad
if($LASTEXITCODE -ne 0){throw 'Controller binding checks failed to compile.'}
& $pad (Join-Path $out ('pad-user-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Controller binding checks failed.'}
$speed=Join-Path $out 'game-speed-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\game_speed_check.cpp') (Join-Path $repoRoot 'runtime\src\game_step.cpp') '-o' $speed
if($LASTEXITCODE -ne 0) { throw 'Game speed check failed to compile.' }
& $speed
if($LASTEXITCODE -ne 0) { throw 'Offline game speed checks failed.' }
$touch=Join-Path $out 'touch-check.exe'
& $clang @common @includes '-DMH3_PC_TOUCH_UI=1' '-DMH3_TOUCH_INPUT_TESTS=1' (Join-Path $repoRoot 'tests\rowell\touch_check.cpp') (Join-Path $repoRoot 'android-source\runtime\src\touch_controls.cpp') (Join-Path $repoRoot 'runtime\src\pc_keybindings.cpp') (Join-Path $repoRoot 'runtime\src\gamepad_bindings.cpp') (Join-Path $repoRoot 'runtime\native\mh3_classic_map.c') $sdl (Join-Path $repoRoot 'third_party\angle\lib\libGLESv2.lib') '-o' $touch
if($LASTEXITCODE -ne 0) { throw 'Touch input check failed to compile.' }
Copy-Item -LiteralPath (Join-Path $ToolchainRoot 'build\windows\SDL3.dll') -Destination $out -Force
Copy-Item -Path (Join-Path $repoRoot 'third_party\angle\bin\*.dll') -Destination $out -Force
& $touch (Join-Path $out ('input-user-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0) { throw 'Touch input checks failed.' }
$keys=Join-Path $out 'keybindings-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\keybindings_check.cpp') (Join-Path $repoRoot 'runtime\src\pc_keybindings.cpp') $sdl '-o' $keys
if($LASTEXITCODE -ne 0){throw 'Keyboard binding checks failed to compile.'}
& $keys (Join-Path $out ('key-user-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Keyboard binding checks failed.'}
$saves=Join-Path $out 'save-manager-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\save_manager_check.cpp') (Join-Path $repoRoot 'runtime\src\save_manager.cpp') '-o' $saves
if($LASTEXITCODE -ne 0){throw 'Save manager checks failed to compile.'}
& $saves (Join-Path $out ('save-user-'+[guid]::NewGuid().ToString('N')))
if($LASTEXITCODE -ne 0){throw 'Save manager checks failed.'}
$nand=Join-Path $out 'nand-check.exe'
& $clang @common @includes (Join-Path $repoRoot 'tests\rowell\nand_metadata_check.cpp') '-o' $nand
if($LASTEXITCODE -ne 0) { throw 'NAND metadata check failed to compile.' }
& $nand (Join-Path $out 'nand-fixtures')
if($LASTEXITCODE -ne 0) { throw 'NAND metadata checks failed.' }
