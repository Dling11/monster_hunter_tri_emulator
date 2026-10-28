#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Restore','Export','Verify')][string]$Mode='Restore',
    [string]$Destination
)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$bundle=Join-Path $projectRoot 'android'
if(-not $Destination) { $Destination=Join-Path $projectRoot 'android-source' }
$Destination=[IO.Path]::GetFullPath($Destination)
$baseCommit='f7b47b7208bfe1febe8d50716359dc682331c9d9'
$upstream='https://github.com/Kiridor123/OceanTriRecomp-Android.git'
$utf8=[Text.UTF8Encoding]::new($false)

function Invoke-Git {
    param([string[]]$GitArguments)
    & git -c core.safecrlf=false @GitArguments
    if($LASTEXITCODE -ne 0) { throw "Git failed: $($GitArguments -join ' ')" }
}
function Resolve-Child {
    param([string]$Root,[string]$Relative)
    if([IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.([\\/]|$)' -or $Relative -match '[:\r\n]') {
        throw "Invalid bundle path: $Relative"
    }
    $prefix=[IO.Path]::GetFullPath($Root).TrimEnd([IO.Path]::DirectorySeparatorChar,[IO.Path]::AltDirectorySeparatorChar)+[IO.Path]::DirectorySeparatorChar
    $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes its folder: $Relative" }
    return $path
}
function Get-LfHash {
    param([string]$Path)
    $bytes=$utf8.GetBytes([IO.File]::ReadAllText($Path).Replace("`r`n","`n"))
    $hasher=[Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($bytes)).Replace('-','').ToLowerInvariant() }
    finally { $hasher.Dispose() }
}
function Assert-Baseline {
    $head=(& git -C $Destination rev-parse HEAD).Trim()
    if($LASTEXITCODE -ne 0 -or $head -ne $baseCommit) {
        throw "Expected Android baseline $baseCommit in $Destination. The existing checkout was not changed."
    }
}
function Assert-Content {
    param($Manifest)
    foreach($entry in $Manifest.trackedFiles) {
        $path=Resolve-Child $Destination $entry.path
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-LfHash $path) -ne $entry.sha256Lf) {
            throw "Android source differs from the saved bundle: $($entry.path). Export intentional edits with -Mode Export."
        }
    }
    foreach($entry in $Manifest.extraFiles) {
        $path=Resolve-Child $Destination $entry.path
        if(-not(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
            throw "Android additional file differs from the saved bundle: $($entry.path)."
        }
    }
}
function Test-Patch {
    param([switch]$Reverse)
    $previousPreference=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    try {
        $arguments=@('-c','core.autocrlf=true','-C',$Destination,'apply','--check')
        if($Reverse) { $arguments+='--reverse' }
        & git @arguments $patch 2>$null
        return $LASTEXITCODE -eq 0
    } finally { $ErrorActionPreference=$previousPreference }
}

if($Mode -eq 'Export') {
    Assert-Baseline
    $additional=@('docker/.dockerignore','docker/AndroidTools.Dockerfile','rowell-android.ps1',
        'runtime/include/touch_contacts.h','runtime/include/gamepad_bindings.h','runtime/src/gamepad_bindings.cpp','runtime/include/game_speed.h','scripts/rowell_android_build.py','scripts/rowell_android_package.py',
        'packaging/android/src/com/oceantri/mh3/RowellRestartActivity.java',
        'runtime/include/rowell_ui_atlas.h','runtime/include/bounded_log.h','runtime/include/resource_guard.h','runtime/include/rowell_frame_delta.h','runtime/src/equipment_tests.cpp',
        'packaging/android/src/com/oceantri/mh3/RowellLogStore.java',
        'packaging/android/assets/licenses/Inter-OFL.txt')
    $untracked=@(& git -C $Destination ls-files --others --exclude-standard)
    if($LASTEXITCODE -ne 0) { throw 'Could not inspect additional Android source files.' }
    $unsupported=@($untracked | Where-Object { -not $_.StartsWith('generated/') -and $_ -notin $additional })
    if($unsupported.Count) { throw "Additional Android files need to be added to the export list before proceeding: $($unsupported -join ', ')" }
    $gameEdits=@(& git -c core.safecrlf=false -c core.autocrlf=true -c core.filemode=false -C $Destination diff --name-only $baseCommit -- generated/functions)
    if($LASTEXITCODE -ne 0 -or $gameEdits.Count) { throw 'The pinned generated game-function sources must remain unchanged. Record intentional game-code patches explicitly before exporting.' }
    [void][IO.Directory]::CreateDirectory($bundle)
    $patch=Join-Path $bundle 'rowell.patch'
    Invoke-Git @('-c','core.autocrlf=true','-c','core.filemode=false','-C',$Destination,
        'diff','--binary','--full-index','--no-ext-diff',"--output=$patch",$baseCommit,'--','.',':!generated')
    $changed=@(& git -c core.safecrlf=false -c core.autocrlf=true -c core.filemode=false -C $Destination diff --name-only $baseCommit -- . ':!generated')
    if($LASTEXITCODE -ne 0 -or -not $changed.Count) { throw 'No Android source changes were exported.' }
    $tracked=@(foreach($relative in ($changed | Sort-Object)) {
        $path=Resolve-Child $Destination $relative
        [ordered]@{path=$relative;sha256Lf=(Get-LfHash $path)}
    })
    $extras=@(foreach($relative in ($additional | Sort-Object)) {
        $source=Resolve-Child $Destination $relative
        $target=Resolve-Child (Join-Path $bundle 'extra') $relative
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
        Copy-Item -LiteralPath $source -Destination $target -Force
        [ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $manifest=[ordered]@{
        format=1;upstream=$upstream;baseCommit=$baseCommit
        sparseDirectories=@('aurora-main','cmake','docs','generated','packaging','pgo','runtime','scripts','toolchain')
        patchSha256=(Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant()
        trackedFiles=$tracked;extraFiles=$extras
    }
    [IO.File]::WriteAllText((Join-Path $bundle 'source.json'),(($manifest | ConvertTo-Json -Depth 8).Replace("`r`n","`n")+"`n"),$utf8)
    Write-Host "Exported $($tracked.Count) changed files and $($extras.Count) additional files into android/."
    exit 0
}

$manifest=Get-Content -LiteralPath (Join-Path $bundle 'source.json') -Raw | ConvertFrom-Json
if($manifest.format -ne 1 -or $manifest.baseCommit -ne $baseCommit -or $manifest.upstream -ne $upstream) {
    throw 'The Android bundle has an unexpected baseline or format.'
}
$patch=Join-Path $bundle 'rowell.patch'
if((Get-FileHash -LiteralPath $patch -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.patchSha256) {
    throw 'The Android patch checksum does not match source.json.'
}
foreach($entry in $manifest.extraFiles) {
    $payload=Resolve-Child (Join-Path $bundle 'extra') $entry.path
    if((Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "The Android payload checksum does not match: $($entry.path)"
    }
}
if($Mode -eq 'Verify') {
    Assert-Baseline
    Assert-Content $manifest
    Write-Host 'Android baseline, patch, payload and restored source checks passed.'
    exit 0
}

if(-not(Test-Path -LiteralPath $Destination)) {
    Invoke-Git @('clone','--filter=blob:none','--no-checkout',$upstream,$Destination)
    Invoke-Git @('-C',$Destination,'sparse-checkout','init','--cone')
    Invoke-Git @('-C',$Destination,'checkout','--detach',$baseCommit)
    Invoke-Git (@('-C',$Destination,'sparse-checkout','set')+@($manifest.sparseDirectories))
} elseif(-not(Test-Path -LiteralPath (Join-Path $Destination '.git'))) {
    throw "Destination already exists and is not a Git checkout: $Destination. No files were replaced."
}
Assert-Baseline
# Check every additional file before applying anything to an existing checkout.
foreach($entry in $manifest.extraFiles) {
    $target=Resolve-Child $Destination $entry.path
    if((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
        throw "Local Android file has different content: $($entry.path). No files were replaced."
    }
}
if(Test-Patch -Reverse) {
    Write-Host 'The saved Android patch is already applied.'
} else {
    if(-not(Test-Patch)) { throw 'The Android patch cannot be applied cleanly. Existing local changes were preserved.' }
    Invoke-Git @('-c','core.autocrlf=true','-C',$Destination,'apply','--whitespace=nowarn',$patch)
}
foreach($entry in $manifest.extraFiles) {
    $target=Resolve-Child $Destination $entry.path
    if(-not(Test-Path -LiteralPath $target)) {
        [void][IO.Directory]::CreateDirectory((Split-Path -Parent $target))
        Copy-Item -LiteralPath (Resolve-Child (Join-Path $bundle 'extra') $entry.path) -Destination $target
    }
}
Assert-Content $manifest
Write-Host "Android Rowell source is ready: $Destination"
