#requires -Version 5.1
[CmdletBinding()]
param([string]$BuildDirectory,[string]$OutputDirectory,[string]$VersionName='1.4-rowell.7')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(-not $BuildDirectory){$BuildDirectory=Join-Path $projectRoot 'build/windows'}
$BuildDirectory=(Resolve-Path -LiteralPath $BuildDirectory).ProviderPath
$executable=Join-Path $BuildDirectory 'MH3_Rowell_Dev.exe'
if(-not(Test-Path -LiteralPath $executable)){$executable=Join-Path $BuildDirectory 'MH3.exe'}
if(-not(Test-Path -LiteralPath $executable)){throw 'Build the Windows app before packaging.'}
if(-not $OutputDirectory){$OutputDirectory=Join-Path $projectRoot 'dist/Windows'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$staging=Join-Path $projectRoot ('.dev/windows-package-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($staging)
$utf8=[Text.UTF8Encoding]::new($false)
Copy-Item -LiteralPath $executable -Destination (Join-Path $staging 'MH3_Rowell_Dev.exe')
foreach($name in @('SDL3.dll','libEGL.dll','libGLESv2.dll','libGLESv1_CM.dll','d3dcompiler_47.dll','libpng16.dll','z.dll')){
    $library=Join-Path $BuildDirectory $name
    if(-not(Test-Path -LiteralPath $library)){throw "Required runtime library is missing: $name"}
    Copy-Item -LiteralPath $library -Destination (Join-Path $staging $name)
}
foreach($entry in @(@('docs/WINDOWS_INSTALL.md','INSTALL.md'),@('docs/CONTROLS.md','CONTROLS.md'),
    @('docs/CHANGES.md','CHANGES.md'),@('LICENSE','LICENSE.txt'))){
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry[0]) -Destination (Join-Path $staging $entry[1])
}
$sourceRepository='https://github.com/Dling11/monster_hunter_tri_emulator'
$creditText=[IO.File]::ReadAllText((Join-Path $projectRoot 'CREDITS.md'))
$creditText=[regex]::Replace($creditText,'\]\(([^)]+)\)',{
    param($match)
    $target=$match.Groups[1].Value
    if($target -eq 'LICENSE'){return '](LICENSE.txt)'}
    if($target.StartsWith('LICENSES/') -or $target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('#')){return $match.Value}
    return ']('+$sourceRepository+'/blob/main/'+$target+')'
})
[IO.File]::WriteAllText((Join-Path $staging 'CREDITS.md'),$creditText,$utf8)
Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSES') -Destination (Join-Path $staging 'LICENSES') -Recurse
$launcher=@'
@echo off
setlocal
cd /d "%~dp0"
set "MH3_USER_DIR=%~dp0MH3_Rowell_Dev"
set "MH3_DISC_DIR="
set "MH3_SHOW_TOUCH_SETTINGS=1"
start "" "%~dp0MH3_Rowell_Dev.exe"
'@
[IO.File]::WriteAllText((Join-Path $staging 'START_ROWELL.cmd'),$launcher.Replace("`r`n","`n").Replace("`n","`r`n")+"`r`n",[Text.Encoding]::ASCII)
[IO.File]::WriteAllText((Join-Path $staging 'portable.txt'),'MH Tri - Rowell portable configuration',$utf8)
[IO.File]::WriteAllText((Join-Path $staging 'MH3UI.uilanguage'),"en`n",$utf8)
$readme=@'
MH Tri - Rowell / Windows x64

Extract the entire ZIP into a writable folder and run START_ROWELL.cmd.
Keep MH3_Rowell_Dev.exe and all DLLs together.
Provide your own European/PAL Monster Hunter Tri RVZ/ISO/WIA (RMHP08).
Put one image beside the EXE or select it at first launch. Allow preparation time.

WASD: move. IJKL: camera. F2: settings. F1: original PC settings.
Controls > Keyboard bindings: remap inputs, save automatically, reset defaults.
Read INSTALL.md for requirements, updates and save locations.

Character saves use MH3_Rowell_Dev beside the EXE. Keep it when updating.
No game image, personal saves, settings, logs or developer tools are included.
Source: https://github.com/Dling11/monster_hunter_tri_emulator
'@
[IO.File]::WriteAllText((Join-Path $staging 'README.txt'),$readme,$utf8)
$exeHash=(Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
$sourceCommit=(& git -C $projectRoot rev-parse HEAD 2>$null)
if($LASTEXITCODE -ne 0){$sourceCommit=$null}
$metadata=[ordered]@{appName='MH Tri - Rowell';platform='Windows';architecture='x64';versionName=$VersionName;
    exeFile='MH3_Rowell_Dev.exe';exeSha256=$exeHash;dataFolder='MH3_Rowell_Dev';sourceRepository=$sourceRepository;sourceCommit=$sourceCommit}
[IO.File]::WriteAllText((Join-Path $staging 'VERSION-Windows.json'),($metadata|ConvertTo-Json)+"`n",$utf8)
$sums=foreach($file in (Get-ChildItem -LiteralPath $staging -File -Recurse | Sort-Object FullName)){
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$file.FullName.Substring($staging.Length+1).Replace('\','/')
}
[IO.File]::WriteAllText((Join-Path $staging 'SHA256SUMS.txt'),($sums -join "`n")+"`n",$utf8)
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $OutputDirectory 'MH3-Rowell-Windows-x64.zip'
$temporary=Join-Path $OutputDirectory ([guid]::NewGuid().ToString('N')+'.zip.partial')
$archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
try{
    foreach($file in (Get-ChildItem -LiteralPath $staging -File -Recurse | Sort-Object FullName)){
        $relative=$file.FullName.Substring($staging.Length+1).Replace('\','/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative,[IO.Compression.CompressionLevel]::Optimal)
    }
}finally{$archive.Dispose()}
Move-Item -LiteralPath $temporary -Destination $zip -Force
$zipHash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zip+'.sha256'),$zipHash+'  '+[IO.Path]::GetFileName($zip)+"`n",$utf8)
Copy-Item -LiteralPath (Join-Path $staging 'VERSION-Windows.json') -Destination $OutputDirectory -Force
Write-Host "Windows ZIP: $zip"
Write-Host "ZIP SHA256: $zipHash"
Write-Host "EXE SHA256: $exeHash"
