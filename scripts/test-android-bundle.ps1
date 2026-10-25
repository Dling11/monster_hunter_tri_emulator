#requires -Version 5.1
[CmdletBinding()]
param([switch]$UseRemote)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
$manifest=Get-Content -LiteralPath (Join-Path $projectRoot 'android\source.json') -Raw | ConvertFrom-Json
$fixture=Join-Path $projectRoot ('.dev\android-restore-check-'+[guid]::NewGuid().ToString('N'))
$workingAndroid=Join-Path $projectRoot 'android-source'
$restorer=Join-Path $PSScriptRoot 'android-source.ps1'

function Run-Git {
    param([string[]]$Arguments)
    & git -c core.safecrlf=false @Arguments
    if($LASTEXITCODE -ne 0) { throw "Git fixture setup failed: $($Arguments -join ' ')" }
}
if(-not $UseRemote -and (Test-Path -LiteralPath (Join-Path $workingAndroid '.git'))) {
    # Read existing objects without changing the working Android checkout.
    Run-Git @('clone','--shared','--no-checkout',$workingAndroid,$fixture)
} else {
    [void][IO.Directory]::CreateDirectory($fixture)
    Run-Git @('-C',$fixture,'init')
    Run-Git @('-C',$fixture,'remote','add','origin',$manifest.upstream)
    Run-Git @('-C',$fixture,'config','remote.origin.promisor','true')
    Run-Git @('-C',$fixture,'config','remote.origin.partialclonefilter','blob:none')
    Run-Git @('-C',$fixture,'fetch','--depth=1','--filter=blob:none','origin',$manifest.baseCommit)
    Run-Git @('-C',$fixture,'update-ref','HEAD',$manifest.baseCommit)
}
Run-Git @('-C',$fixture,'sparse-checkout','init','--no-cone')
$patterns=@($manifest.trackedFiles | ForEach-Object { '/'+$_.path })
Run-Git (@('-C',$fixture,'sparse-checkout','set','--no-cone','--')+$patterns)
Run-Git @('-C',$fixture,'checkout','--detach',$manifest.baseCommit)
foreach($pass in 1,2) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $restorer -Mode Restore -Destination $fixture
    if($LASTEXITCODE -ne 0) { throw "Restore check $pass failed." }
}
& powershell -NoProfile -ExecutionPolicy Bypass -File $restorer -Mode Verify -Destination $fixture
if($LASTEXITCODE -ne 0) { throw 'Restored source checksum check failed.' }

$conflict=Join-Path $fixture 'runtime\include\touch_contacts.h'
$original=[IO.File]::ReadAllText($conflict)
$localChange=$original+"`n// fixture: preserve an intentional local change`n"
[IO.File]::WriteAllText($conflict,$localChange,[Text.UTF8Encoding]::new($false))
$previousPreference=$ErrorActionPreference
$ErrorActionPreference='Continue'
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $restorer -Mode Restore -Destination $fixture *> (Join-Path $fixture 'expected-conflict.log')
    $conflictCode=$LASTEXITCODE
} finally { $ErrorActionPreference=$previousPreference }
if($conflictCode -eq 0 -or [IO.File]::ReadAllText($conflict) -ne $localChange) {
    throw 'The restore command did not preserve a conflicting additional file.'
}
[IO.File]::WriteAllText($conflict,$original,[Text.UTF8Encoding]::new($false))
Write-Host 'Android bundle: clean restore, repeat restore, content checks and local-edit protection passed.'
Write-Host "Private fixture retained at $fixture"
# The expected rejection above leaves a nonzero native exit code. GitHub's
# PowerShell wrapper checks that value even when every assertion has passed.
exit 0
