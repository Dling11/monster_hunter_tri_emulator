#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Doctor', 'Preinstall', 'PreinstallWindows', 'PreinstallEverything',
                 'Prepare', 'Translate', 'Configure', 'Build', 'BuildNative', 'All', 'Run')]
    [string]$Mode = 'Doctor',
    [string]$RomPath,
    [ValidateRange(1, 32)][int]$Jobs = 4,
    [ValidateRange(1, 2)][int]$OverlayJobs = 1,
    [switch]$ForceTranslation
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$RepoRoot = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
$Image = 'monster-hunter-tri-translation:local'
$LocalState = Join-Path $RepoRoot '.dev'
$WindowsBuild = Join-Path $RepoRoot 'build\windows'

function Invoke-Checked {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE." }
}

function Assert-Docker {
    if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
        throw 'Install Docker Desktop and enable its WSL2/Linux engine first.'
    }
    $engine = & docker info --format '{{.OSType}}' 2>$null
    if ($LASTEXITCODE -ne 0 -or $engine -ne 'linux') {
        throw 'Start Docker Desktop with its Linux engine. This script does not switch engines or restart Docker.'
    }
}

function Get-TranslationImageFingerprint {
    $watchedFiles = @((Join-Path $RepoRoot '.dockerignore'),
        (Join-Path $RepoRoot 'docker\translation.Dockerfile'),
        (Join-Path $RepoRoot 'scripts\dev-container.py'))
    $watchedFiles += @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'toolchain') -File -Recurse |
        Where-Object { $_.FullName -notmatch '[\\/](bin|obj|__pycache__)[\\/]' } |
        Select-Object -ExpandProperty FullName)
    $records = foreach ($path in ($watchedFiles | Sort-Object)) {
        $relative = $path.Substring($RepoRoot.Length + 1).Replace('\', '/')
        $relative + ':' + (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { $fingerprint = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes(($records -join "`n")))).Replace('-', '') }
    finally { $hasher.Dispose() }
    return $fingerprint
}

function Build-TranslationImage {
    Assert-Docker
    $fingerprint = Get-TranslationImageFingerprint
    $stampPath = Join-Path $LocalState 'translation-image.json'
    $imageId = & docker image inspect $Image --format '{{.Id}}' 2>$null
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $stampPath)) {
        $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
        if ($stamp.fingerprint -eq $fingerprint -and $stamp.imageId -eq $imageId) {
            Write-Host 'Using the cached Docker translation tools (source fingerprint matches).'
            return
        }
    }
    Invoke-Checked docker @('build', '--progress', 'plain', '--tag', $Image,
        '--file', (Join-Path $RepoRoot 'docker\translation.Dockerfile'), $RepoRoot)
    if ((Get-TranslationImageFingerprint) -ne $fingerprint) {
        throw 'Translator/image sources changed during the image build. Run Preinstall again.'
    }
    $imageId = & docker image inspect $Image --format '{{.Id}}'
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the built translation image.' }
    [void][IO.Directory]::CreateDirectory($LocalState)
    @{fingerprint = $fingerprint; imageId = $imageId} | ConvertTo-Json |
        Set-Content -LiteralPath $stampPath -Encoding Ascii
}

function Invoke-TranslationContainer {
    param([string]$Action)
    Assert-Docker
    $arguments = @('run', '--rm', '--cpus', [string]$Jobs, '--memory', '5g',
        '--env', "TRANSLATE_JOBS=$Jobs", '--env', "OVERLAY_JOBS=$OverlayJobs")
    if ($Action -ne 'doctor') {
        foreach ($relative in @('generated', 'build\container-generated', 'build\container-analysis', '.dev')) {
            [void][IO.Directory]::CreateDirectory((Join-Path $RepoRoot $relative))
        }
        $arguments += @('--mount', "type=bind,source=$RepoRoot,target=/workspace",
            '--mount', "type=bind,source=$RepoRoot\build\container-generated,target=/workspace/generated",
            '--mount', "type=bind,source=$RepoRoot\build\container-analysis,target=/workspace/build",
            '--mount', "type=bind,source=$RepoRoot\generated,target=/windows-generated")
    }
    $containerArgs = @($Action)
    if ($Action -eq 'prepare' -and $RomPath) {
        $discPath = (Resolve-Path -LiteralPath $RomPath).ProviderPath
        if (-not (Test-Path -LiteralPath $discPath -PathType Leaf)) { throw 'RomPath must be a file.' }
        $extension = [IO.Path]::GetExtension($discPath).ToLowerInvariant()
        if ($extension -notin @('.rvz', '.iso', '.wia')) { throw 'Use a PAL .rvz, .iso, or .wia image.' }
        $containerDisc = "/disc/input$extension"
        # The original PAL filename contains commas. --volume accepts them
        # without the CSV quoting needed by --mount; the input stays read-only.
        $arguments += @('--volume', ($discPath + ':' + $containerDisc + ':ro'))
        $containerArgs += @('--rom', $containerDisc)
    }
    if ($Action -eq 'translate') {
        $containerArgs += @('--windows-root', $RepoRoot.Replace('\', '/'))
        if ($ForceTranslation) { $containerArgs += '--force' }
    }
    $arguments += @($Image) + $containerArgs
    Invoke-Checked docker $arguments
}

function Find-VsTools {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $path = & $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
        if ($path) { return [string]($path | Select-Object -First 1) }
    }
    return $null
}

function Find-Clang {
    $candidates = @((Join-Path $RepoRoot '.tools\llvm\bin\clang.exe'),
        (Join-Path $env:ProgramFiles 'LLVM\bin\clang.exe'))
    $command = Get-Command clang.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates += $command.Source }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            $version = (& $candidate --version | Select-Object -First 1)
            if ($version -match 'clang version (\d+)' -and [int]$Matches[1] -ge 21) { return $candidate }
        }
    }
    return $null
}

function Install-WindowsTools {
    if (-not (Get-Command winget -ErrorAction SilentlyContinue)) { throw 'Windows App Installer / winget is required.' }
    $common = @('--exact', '--source', 'winget', '--silent', '--disable-interactivity',
        '--accept-source-agreements', '--accept-package-agreements')
    if (-not (Find-VsTools)) {
        $isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
        if (-not $isAdmin) { throw 'Run PreinstallEverything from an Administrator PowerShell to install the C++ build tools.' }
        $installPath = Join-Path $RepoRoot '.tools\VS2022'
        $options = '--quiet --wait --norestart --nocache --installPath "' + $installPath +
            '" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended' +
            ' --add Microsoft.VisualStudio.Component.VC.CMake.Project'
        Write-Host 'Installing Visual Studio 2022 C++ Build Tools, Windows SDK, CMake and Ninja.'
        & winget install --id Microsoft.VisualStudio.2022.BuildTools @common --override $options
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { throw "C++ Build Tools installation failed: $LASTEXITCODE" }
        if (-not (Find-VsTools)) { throw 'Required Visual Studio C++/CMake components were not detected. Check the installer log before building.' }
    }
    if (-not (Find-Clang)) {
        Write-Host 'Installing LLVM/Clang 21.1.8 for the Windows build.'
        & winget install --id LLVM.LLVM --version 21.1.8 @common --location (Join-Path $RepoRoot '.tools\llvm')
        if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 3010) { throw "LLVM installation failed: $LASTEXITCODE" }
        if (-not (Find-Clang)) { throw 'Clang 21+ was not detected after installation.' }
    }
    Write-Host 'Windows build tools are ready. No host Python or .NET SDK was installed.'
}

function Get-WindowsToolPaths {
    $visualStudio = Find-VsTools
    $clang = Find-Clang
    if (-not $visualStudio -or -not $clang) { throw 'Run .\dev.ps1 -Mode PreinstallEverything before the Windows build.' }
    $cmakeBase = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake'
    $cmake = Join-Path $cmakeBase 'CMake\bin\cmake.exe'
    $ninja = Join-Path $cmakeBase 'Ninja\ninja.exe'
    foreach ($tool in @($cmake, $ninja)) {
        if (-not (Test-Path -LiteralPath $tool)) { throw "Missing bundled build tool: $tool" }
    }
    $devShell = Join-Path $visualStudio 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    Import-Module $devShell
    Enter-VsDevShell -VsInstallPath $visualStudio -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
    $env:PATH = [IO.Path]::GetDirectoryName($clang) + ';' + [IO.Path]::GetDirectoryName($ninja) + ';' + $env:PATH
    return @{ CMake = $cmake; Ninja = $ninja; Clang = $clang }
}

function Configure-Windows {
    param([switch]$DependenciesOnly)
    $tools = Get-WindowsToolPaths
    $repoCmake = $RepoRoot.Replace('\', '/')
    $arguments = @('-S', (Join-Path $RepoRoot 'runtime'), '-B', $WindowsBuild, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', "-DCMAKE_C_COMPILER=$($tools.Clang)",
        "-DCMAKE_EXE_LINKER_FLAGS=-Wl,/opt:lldltojobs=$Jobs",
        "-DCMAKE_ASM_COMPILER=$($tools.Clang)",
        "-DCMAKE_CXX_COMPILER=$([IO.Path]::Combine([IO.Path]::GetDirectoryName($tools.Clang), 'clang++.exe'))",
        "-DCMAKE_MAKE_PROGRAM=$($tools.Ninja)", '-DMH3_ALLOW_UNSUPPORTED_HOST=ON',
        '-DMH3_USE_DOCKER_TRANSLATION=ON', "-DMH3_AURORA_ROOT=$repoCmake/aurora-main",
        '-DAURORA_ANGLE_VULKAN=ON', "-DAURORA_EGL_INCLUDE_DIR=$repoCmake/third_party/angle/include",
        "-DAURORA_GLES3_INCLUDE_DIR=$repoCmake/third_party/angle/include",
        "-DAURORA_GLESV2_LIBRARY=$repoCmake/third_party/angle/lib/libGLESv2.lib",
        '-DMH3_BUILD_TESTS=OFF', '-DMH3_BUILD_VITA_TEXTURE_PACKER=OFF')
    $preload = if ($DependenciesOnly) { 'ON' } else { 'OFF' }
    $arguments += "-DMH3_PRELOAD_DEPENDENCIES_ONLY=$preload"
    Invoke-Checked $tools.CMake $arguments
}

function Build-Windows {
    Configure-Windows
    $tools = Get-WindowsToolPaths
    Invoke-Checked $tools.CMake @('--build', $WindowsBuild, '--target', 'MH3', '--parallel', [string]$Jobs)
    $dllManifest = Join-Path $WindowsBuild 'mh3-runtime-dlls-Release.txt'
    if (-not (Test-Path -LiteralPath $dllManifest)) { throw 'CMake did not produce the runtime DLL manifest.' }
    foreach ($dll in (Get-Content -LiteralPath $dllManifest -Encoding UTF8 | Where-Object { $_.Trim() })) {
        if (-not (Test-Path -LiteralPath $dll)) { throw "Missing runtime DLL: $dll" }
        $destination = Join-Path $WindowsBuild ([IO.Path]::GetFileName($dll))
        if ([IO.Path]::GetFullPath($dll) -ine [IO.Path]::GetFullPath($destination)) {
            Copy-Item -LiteralPath $dll -Destination $destination -Force
        }
    }
    Copy-Item -Path (Join-Path $RepoRoot 'third_party\angle\bin\*.dll') -Destination $WindowsBuild -Force
    $languagePath = Join-Path $WindowsBuild 'MH3UI.uilanguage'
    if (-not (Test-Path -LiteralPath $languagePath)) { Set-Content -LiteralPath $languagePath -Value 'en' -Encoding Ascii }
    Write-Host "Built: $WindowsBuild\MH3.exe"
}

Push-Location -LiteralPath $RepoRoot
$operationLock = $null
try {
    if ($RepoRoot.Contains(',')) { throw 'The checkout path cannot contain a comma (Docker bind-mount syntax).' }
    if ($Mode -in @('Prepare', 'Translate', 'Configure', 'Build', 'BuildNative', 'All')) {
        [void][IO.Directory]::CreateDirectory($LocalState)
        $operationLock = [IO.File]::Open((Join-Path $LocalState 'operation.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    }
    switch ($Mode) {
        'Doctor' {
            Assert-Docker
            Write-Host "Checkout: $RepoRoot"
            Write-Host "Visual Studio C++/CMake: $(Find-VsTools)"
            Write-Host "Clang 21+: $(Find-Clang)"
            & docker image inspect $Image --format '{{.Id}}' 2>$null
            if ($LASTEXITCODE -eq 0) { Invoke-TranslationContainer doctor }
            else { Write-Host 'Docker translation image is missing. Run -Mode Preinstall.' }
            Write-Host "Windows export manifest present: $(Test-Path -LiteralPath (Join-Path $RepoRoot 'generated\docker-windows-ready.json'))"
        }
        'Preinstall' { Build-TranslationImage; Invoke-TranslationContainer doctor }
        'PreinstallWindows' { Install-WindowsTools }
        'PreinstallEverything' {
            Build-TranslationImage; Invoke-TranslationContainer doctor; Install-WindowsTools
            Configure-Windows -DependenciesOnly
            $androidPreinstall = Join-Path $RepoRoot 'android-source\rowell-android.ps1'
            if (Test-Path -LiteralPath $androidPreinstall) {
                Invoke-Checked powershell @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $androidPreinstall, '-Mode', 'Preinstall')
            } else {
                Write-Host 'Android preload requires the separate android-source checkout. See docs/BUILDING.md.'
            }
        }
        'Prepare' { Build-TranslationImage; Invoke-TranslationContainer prepare }
        'Translate' { Build-TranslationImage; Invoke-TranslationContainer translate }
        'Configure' { Configure-Windows }
        'Build' { Build-TranslationImage; Invoke-TranslationContainer translate; Build-Windows }
        'BuildNative' { Build-Windows }
        'All' {
            Build-TranslationImage; Invoke-TranslationContainer doctor; Install-WindowsTools
            Invoke-TranslationContainer prepare; Invoke-TranslationContainer translate; Build-Windows
        }
        'Run' {
            $executable = Join-Path $WindowsBuild 'MH3.exe'
            if (-not (Test-Path -LiteralPath $executable)) { throw 'Run -Mode Build first.' }
            if ($RomPath) { Invoke-Checked $executable @('--rom', (Resolve-Path -LiteralPath $RomPath).ProviderPath) }
            else { Invoke-Checked $executable @() }
        }
    }
}
finally {
    if ($operationLock) { $operationLock.Dispose() }
    Pop-Location
}
