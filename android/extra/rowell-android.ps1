#requires -Version 5.1
[CmdletBinding()]
param([ValidateSet('Preinstall','Build')][string]$Mode='Build',[ValidateRange(1,3)][int]$Jobs=2)
$ErrorActionPreference='Stop'
$androidRoot=$PSScriptRoot
$parentRoot=Split-Path -Parent $androidRoot
$image='monster-hunter-tri-android:rowell'
if($Mode -eq 'Preinstall') {
    & docker build --progress plain -t $image -f (Join-Path $androidRoot 'docker\AndroidTools.Dockerfile') (Join-Path $androidRoot 'docker')
} else {
    & docker image inspect $image *> $null
    if($LASTEXITCODE -ne 0) { throw 'Run -Mode Preinstall first.' }
    & docker run --rm --cpus $Jobs --memory 4g --env "MH3_BUILD_JOBS=$Jobs" `
        --mount "type=bind,source=$androidRoot,target=/workspace" `
        --mount "type=bind,source=$parentRoot\Assets,target=/workspace/Assets,readonly" `
        --mount "type=bind,source=$parentRoot\build\container-analysis,target=/game-map,readonly" `
        --mount 'type=volume,source=mh3-rowell-android-cache,target=/cache' `
        $image python3 /workspace/scripts/rowell_android_build.py
}
if($LASTEXITCODE -ne 0) { throw "Android $Mode failed with exit code $LASTEXITCODE." }
