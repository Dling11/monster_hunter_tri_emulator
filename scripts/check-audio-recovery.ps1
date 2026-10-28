$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$out=Join-Path $root '.dev/audio-recovery-checks'
[void][IO.Directory]::CreateDirectory($out)
$user=Join-Path $out ('audio-user-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($user)
$start=[Diagnostics.ProcessStartInfo]::new()
$start.FileName=Join-Path $root 'build/windows/MH3.exe'
$start.Arguments='--audio-self-test'
$start.WorkingDirectory=Join-Path $root 'build/windows'
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
$start.EnvironmentVariables['MH3_USER_DIR']=$user
$start.EnvironmentVariables['MH3_DISC_DIR']=Join-Path $root 'Assets/DATA'
$start.EnvironmentVariables['SDL_AUDIO_DRIVER']='dummy'
$start.EnvironmentVariables['SDL_AUDIODRIVER']='dummy'
$start.EnvironmentVariables['MH3_AUDIO_RECOVERY_SELF_TEST']='1'
$process=[Diagnostics.Process]::new()
$process.StartInfo=$start
$startedUtc=[DateTime]::UtcNow
[void]$process.Start()
$stdout=$process.StandardOutput.ReadToEndAsync()
$stderr=$process.StandardError.ReadToEndAsync()
if(-not$process.WaitForExit(60000)){$process.Kill();throw 'Isolated audio recovery check timed out'}
$process.WaitForExit()
[IO.File]::WriteAllText((Join-Path $out 'audio.stdout.log'),$stdout.Result)
[IO.File]::WriteAllText((Join-Path $out 'audio.stderr.log'),$stderr.Result)
if($process.ExitCode-ne0){throw ('Audio recovery failed: '+$stderr.Result)}
$runtimeLog=Join-Path $start.WorkingDirectory 'MH3.log'
if(-not(Test-Path -LiteralPath $runtimeLog) -or (Get-Item -LiteralPath $runtimeLog).LastWriteTimeUtc -lt $startedUtc){throw 'The executable did not produce a fresh runtime log'}
$details=[IO.File]::ReadAllText($runtimeLog)
[IO.File]::WriteAllText((Join-Path $out 'audio.runtime.log'),$details)
if(-not$details.Contains('Audio lifecycle: stalled stream recovery and joined shutdown passed.')){throw 'Audio recovery check did not execute'}
Write-Output 'Actual SDL dummy stream: forced stall, reopen, resumed mixing, rapid resume and joined shutdown passed.'
exit 0
