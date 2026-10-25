#requires -Version 5.1
# Disposable integration fixtures for CMake's real Docker freshness gate.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath
if (-not $vsPath) { throw 'Install the Windows build tools first.' }
$cmake = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$testRoot = Join-Path $repoRoot ('.dev\gate-tests\' + [guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding($false)

function Write-TestFile([string]$Path, [string]$Text) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Path))
    [IO.File]::WriteAllText($Path, $Text, $utf8)
}

function New-Fixture([string]$Name) {
    $root = Join-Path $testRoot $Name
    $inputs = @('recomp.yml', 'symbols.txt', 'runtime/CMakeLists.txt',
        'cmake/DockerTranslation.cmake', 'docker/translation.Dockerfile',
        'runtime/src/input.cpp', 'toolchain/example.cs', 'scripts/example.py')
    foreach ($relative in $inputs) { Write-TestFile (Join-Path $root $relative) "fixture`n" }
    Copy-Item -LiteralPath (Join-Path $repoRoot 'cmake\DockerTranslation.cmake') `
        -Destination (Join-Path $root 'cmake\DockerTranslation.cmake') -Force
    Write-TestFile (Join-Path $root 'runtime\CMakeLists.txt') @'
cmake_minimum_required(VERSION 3.25)
project(DockerGateFixture LANGUAGES NONE)
include(../cmake/DockerTranslation.cmake)
mh3_validate_docker_translation()
'@
    Write-TestFile (Join-Path $root 'generated\blob.S') '.section .rdata,"dr"'
    $inputRecords = @($inputs | Sort-Object | ForEach-Object {
        @{path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $outputRecords = @(@{path = 'blob.S'; sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'generated\blob.S') -Algorithm SHA256).Hash.ToLowerInvariant()})
    $hashLists = @{}
    foreach ($group in @('inputs', 'outputs')) {
        $records = if ($group -eq 'inputs') { $inputRecords } else { $outputRecords }
        $text = ($records | ForEach-Object { $_.sha256 + '  ' + $_.path }) -join "`n"
        $path = Join-Path $root "generated\docker-$group.sha256"
        Write-TestFile $path ($text + "`n")
        $hashLists[$group] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $manifest = @{format = 1; windowsRoot = $root.Replace('\', '/'); inputs = $inputRecords;
        outputs = $outputRecords; hashLists = $hashLists}
    Write-TestFile (Join-Path $root 'generated\docker-windows-ready.json') ($manifest | ConvertTo-Json -Depth 5)
    return $root
}

$cases = @(
    @{Name = 'valid'; Expected = 0; Message = 'Validated Docker translation'},
    @{Name = 'changed-source'; Expected = 1; Message = 'Changed translation input/output'},
    @{Name = 'changed-output'; Expected = 1; Message = 'Changed translation input/output'},
    @{Name = 'added-source'; Expected = 1; Message = 'Translation source list changed'},
    @{Name = 'changed-hash-list'; Expected = 1; Message = 'Docker hash list changed'},
    @{Name = 'missing-output'; Expected = 1; Message = 'Missing translation input/output'}
)
foreach ($case in $cases) {
    $root = New-Fixture $case.Name
    switch ($case.Name) {
        'changed-source' { Add-Content -LiteralPath (Join-Path $root 'runtime\src\input.cpp') -Value '// edited' }
        'changed-output' { Add-Content -LiteralPath (Join-Path $root 'generated\blob.S') -Value '// edited' }
        'added-source' { Write-TestFile (Join-Path $root 'runtime\src\added.cpp') '// new' }
        'changed-hash-list' { Add-Content -LiteralPath (Join-Path $root 'generated\docker-inputs.sha256') -Value 'invalid' }
        'missing-output' { Move-Item -LiteralPath (Join-Path $root 'generated\blob.S') -Destination (Join-Path $root 'generated\blob.moved') }
    }
    $log = Join-Path $root 'configure.log'
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    & $cmake -S (Join-Path $root 'runtime') -B (Join-Path $root 'out') *> $log
    $code = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    $output = Get-Content -LiteralPath $log -Raw
    if (($case.Expected -eq 0 -and $code -ne 0) -or
        ($case.Expected -ne 0 -and $code -eq 0) -or -not $output.Contains($case.Message)) {
        throw "Gate test $($case.Name) failed. See $log"
    }
    Write-Output "PASS: $($case.Name)"
}
Write-Output "All six freshness-gate integration checks passed. Fixtures: $testRoot"
