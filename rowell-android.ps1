#requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Preinstall','Build')][string]$Mode='Build',[ValidateRange(1,3)][int]$Jobs=2,
    [ValidateSet('armeabi-v7a','arm64-v8a')][string]$Abi='armeabi-v7a',
    [string]$GameAssets,[string]$GameMap)
$ErrorActionPreference='Stop'
$androidRoot=$PSScriptRoot
$image="mh-tri-low-spec-android:$Abi"
if($Mode -eq 'Preinstall') {
    & docker build --progress plain --build-arg "MH3_ANDROID_ABI=$Abi" -t $image -f (Join-Path $androidRoot 'docker\AndroidTools.Dockerfile') (Join-Path $androidRoot 'docker')
} else {
    if (-not $GameAssets -or -not $GameMap) { throw 'Provide explicit -GameAssets and -GameMap paths for your own extracted game. The main app is never used as an implicit build input.' }
    $GameAssets=(Resolve-Path -LiteralPath $GameAssets -ErrorAction Stop).Path
    $GameMap=(Resolve-Path -LiteralPath $GameMap -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath (Join-Path $GameAssets 'DATA\sys\main.dol')) -or -not (Test-Path -LiteralPath (Join-Path $GameMap 'MAP.txt'))) { throw 'Required extracted game or map file is missing.' }
    & docker image inspect $image *> $null
    if($LASTEXITCODE -ne 0) { throw 'Run -Mode Preinstall first.' }
    & docker run --rm --cpus $Jobs --memory 4g --env "MH3_BUILD_JOBS=$Jobs" --env "MH3_ANDROID_ABI=$Abi" `
        --mount "type=bind,source=$androidRoot,target=/workspace" `
        --mount "type=bind,source=$GameAssets,target=/workspace/Assets,readonly" `
        --mount "type=bind,source=$GameMap,target=/game-map,readonly" `
        --mount "type=volume,source=mh-tri-low-spec-$Abi-cache,target=/cache" `
        $image python3 /workspace/scripts/rowell_android_build.py
}
if($LASTEXITCODE -ne 0) { throw "Android $Mode failed with exit code $LASTEXITCODE." }
