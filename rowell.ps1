#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Build','Package','Test','Run')][string]$Mode='Run',
    [string]$RomPath
)
$ErrorActionPreference='Stop'
$projectRoot=$PSScriptRoot
$pcFolder=Join-Path $projectRoot 'playtest\PC'
$exe=Join-Path $pcFolder 'MH3_Rowell_Dev.exe'
$userFolder=Join-Path $pcFolder 'MH3_Rowell_Dev'

function Prepare-RowellDisc {
    if(-not $RomPath) { throw 'Supply your own PAL image with -RomPath. See docs/BUILDING.md.' }
    $disc=(Resolve-Path -LiteralPath $RomPath -ErrorAction Stop).ProviderPath
    $source=Join-Path $projectRoot 'Assets\DATA'
    $prepared=Join-Path $pcFolder 'disc'
    [void][IO.Directory]::CreateDirectory((Join-Path $prepared 'sys'))
    foreach($name in @('boot.bin','bi2.bin','apploader.img','main.dol','fst.bin')) {
        Copy-Item -LiteralPath (Join-Path $source "sys\$name") -Destination (Join-Path $prepared "sys\$name") -Force
    }
    foreach($name in @('ticket.bin','tmd.bin','cert.bin','h3.bin')) {
        Copy-Item -LiteralPath (Join-Path $source $name) -Destination (Join-Path $prepared $name) -Force
    }
    $utf8=[Text.UTF8Encoding]::new($false)
    [IO.File]::WriteAllText((Join-Path $prepared 'rom.txt'),($disc+"`n"+(Get-Item -LiteralPath $disc).Length+"`n"),$utf8)
    [IO.File]::WriteAllText((Join-Path $prepared 'disc-layout.txt'),"mh3-partition-v1`n",$utf8)
    return $prepared
}

function Package-Rowell {
    $built=Join-Path $projectRoot 'build\windows\MH3.exe'
    if(-not(Test-Path -LiteralPath $built)) { throw 'Build the development game first with -Mode Build.' }
    [void][IO.Directory]::CreateDirectory($pcFolder)
    [void][IO.Directory]::CreateDirectory($userFolder)
    $portable=Join-Path $pcFolder 'portable.txt'
    if(-not(Test-Path -LiteralPath $portable)) { Set-Content -LiteralPath $portable -Value 'Rowell development portable installation' -Encoding Ascii }
    $nativeSettings=Join-Path $pcFolder 'UserData'
    [void][IO.Directory]::CreateDirectory($nativeSettings)
    $nativeConfig=Join-Path $nativeSettings 'Config.toml'
    # The shared touch UI already provides an FPS counter. Keep the original
    # PC counter available in F1 without covering the touch shoulder buttons.
    if(-not(Test-Path -LiteralPath $nativeConfig)) { [IO.File]::WriteAllText($nativeConfig,"[video]`nshow_fps = false`n",[Text.UTF8Encoding]::new($false)) }
    Copy-Item -LiteralPath $built -Destination $exe -Force
    Copy-Item -Path (Join-Path $projectRoot 'build\windows\*.dll') -Destination $pcFolder -Force
    $english=Join-Path $pcFolder 'MH3UI.uilanguage'
    if(-not(Test-Path -LiteralPath $english)) { Set-Content -LiteralPath $english -Value 'en' -Encoding Ascii }
    Write-Host "PC development build: $exe"
    Write-Host "Separate data folder: $userFolder"
}

function Test-Rowell {
    if(-not(Test-Path -LiteralPath $exe)) { throw 'Package the built development game first with -Mode Package.' }
    $checkFolder=Join-Path $projectRoot ('.dev\pc-runtime-check-'+[guid]::NewGuid().ToString('N'))
    [void][IO.Directory]::CreateDirectory($checkFolder)
    $previousUser=$env:MH3_USER_DIR
    $previousDisc=$env:MH3_DISC_DIR
    try {
        $env:MH3_USER_DIR=$checkFolder
        $env:MH3_DISC_DIR=Join-Path $projectRoot 'Assets\DATA'
        foreach($check in @('controls-self-test','self-test')) {
            $start=[Diagnostics.ProcessStartInfo]::new()
            $start.FileName=$exe
            $start.Arguments="--$check"
            $start.WorkingDirectory=$pcFolder
            $start.UseShellExecute=$false
            $start.CreateNoWindow=$true
            $start.RedirectStandardOutput=$true
            $start.RedirectStandardError=$true
            $process=[Diagnostics.Process]::new()
            $process.StartInfo=$start
            [void]$process.Start()
            $stdout=$process.StandardOutput.ReadToEndAsync()
            $stderr=$process.StandardError.ReadToEndAsync()
            if(-not $process.WaitForExit(120000)) {
                Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                throw "$check timed out. Its isolated test process was stopped. See $checkFolder."
            }
            $process.WaitForExit()
            [IO.File]::WriteAllText((Join-Path $checkFolder "$check.stdout.log"),$stdout.Result)
            [IO.File]::WriteAllText((Join-Path $checkFolder "$check.stderr.log"),$stderr.Result)
            if($process.ExitCode -ne 0) { throw "$check failed with exit code $($process.ExitCode). See $checkFolder and $pcFolder\MH3.log." }
            Write-Host "$check passed."
        }
    } finally {
        $env:MH3_USER_DIR=$previousUser
        $env:MH3_DISC_DIR=$previousDisc
    }
}

if($Mode -eq 'Build') {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $projectRoot 'dev.ps1') -Mode Build -Jobs 3 -OverlayJobs 2
    if($LASTEXITCODE -ne 0) { throw 'Development build failed.' }
    Package-Rowell
} elseif($Mode -eq 'Package') { Package-Rowell }
elseif($Mode -eq 'Test') { Test-Rowell }
else {
    if(-not(Test-Path -LiteralPath $exe)) { throw 'Build and package the development game first with -Mode Build.' }
    $preparedDisc=Prepare-RowellDisc
    $previousUser=$env:MH3_USER_DIR
    $previousDisc=$env:MH3_DISC_DIR
    $previousShow=$env:MH3_SHOW_TOUCH_SETTINGS
    try {
        $env:MH3_USER_DIR=$userFolder
        $env:MH3_DISC_DIR=$preparedDisc
        $env:MH3_SHOW_TOUCH_SETTINGS='1'
        $launch=@{FilePath=$exe;WorkingDirectory=$pcFolder;PassThru=$true}
        $process=Start-Process @launch
        Write-Host "Started Rowell Dev (PID $($process.Id)). F2: touch settings. Escape: return to the game."
    } finally {
        $env:MH3_USER_DIR=$previousUser
        $env:MH3_DISC_DIR=$previousDisc
        $env:MH3_SHOW_TOUCH_SETTINGS=$previousShow
    }
}
