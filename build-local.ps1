param([ValidateSet('armeabi-v7a','arm64-v8a')][string]$Abi='armeabi-v7a',[ValidateRange(1,4)][int]$Jobs=3)
$ErrorActionPreference='Stop'
$liteRoot=$PSScriptRoot
$configPath=Join-Path $liteRoot '.local\build-tools.json'
$localTools=if(Test-Path -LiteralPath $configPath){Get-Content -LiteralPath $configPath -Raw|ConvertFrom-Json}else{$null}
if($localTools.sdk){$env:ANDROID_HOME=$localTools.sdk}
if($localTools.java){$env:JAVA_HOME=$localTools.java}
if(!$env:ANDROID_HOME -or !$env:JAVA_HOME){throw 'Set ANDROID_HOME and JAVA_HOME, or configure .local/build-tools.json as described in docs/BUILDING.md.'}
$env:ANDROID_NDK_HOME=Join-Path $liteRoot '.tools\ndk29\android-ndk-r29'
if($localTools.ndk){$env:ANDROID_NDK_HOME=$localTools.ndk}
$env:MH3_OPENSSL_ROOT=Join-Path $liteRoot '.tools\openssl-arm32'
if($localTools.openssl){$env:MH3_OPENSSL_ROOT=$localTools.openssl}
if($Abi -ne 'armeabi-v7a'){throw 'Build an ABI-matched OpenSSL before selecting another ABI.'}
$env:MH3_ANDROID_ABI=$Abi
$env:MH3_BUILD_JOBS="$Jobs"
$env:DOTNET_CLI_HOME=Join-Path $liteRoot '.tools\dotnet-home'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:PATH=(Join-Path $liteRoot '.tools\dotnet')+';'+$env:JAVA_HOME+'\bin;'+$env:PATH
if($localTools.cmake){$env:PATH=$localTools.cmake+';'+$env:PATH}
if($localTools.ninja){$env:PATH=$localTools.ninja+';'+$env:PATH}
$pythonExe=if($localTools.python){$localTools.python}else{'python'}
Push-Location $liteRoot
try { & $pythonExe scripts\rowell_android_build.py; if($LASTEXITCODE -ne 0){throw 'Lite Android build failed. See build output.'} }
finally { Pop-Location }
