#requires -Version 5.1
[CmdletBinding()]
param([string]$ApkPath,[string]$MetadataPath,[string]$OutputDirectory,[switch]$PhoneGameplayVerified)
$ErrorActionPreference='Stop'
$projectRoot=Split-Path -Parent $PSScriptRoot
if(-not $ApkPath) {
    $ApkPath=Join-Path $projectRoot 'android-source\playtest\MH3-Rowell-Dev-1.4.1-arm64.apk'
}
$ApkPath=(Resolve-Path -LiteralPath $ApkPath -ErrorAction Stop).ProviderPath
if(-not $MetadataPath) { $MetadataPath=[IO.Path]::ChangeExtension($ApkPath,'.json') }
$metadata=Get-Content -LiteralPath $MetadataPath -Raw | ConvertFrom-Json
$apkHash=(Get-FileHash -LiteralPath $ApkPath -Algorithm SHA256).Hash.ToLowerInvariant()
if($metadata.sha256 -ne $apkHash -or $metadata.package -ne 'com.rowell.mh3dev') {
    throw 'APK checksum or package metadata does not match the Rowell build.'
}
if(-not $metadata.versionName -or -not $metadata.versionCode -or $metadata.abi -ne 'arm64-v8a') {
    throw 'Build metadata is missing version/ABI details. Use the current Android APK packager.'
}
if(-not $metadata.sdlVersion -or $metadata.verifiedJniMethods -lt 1) {
    throw 'The APK metadata lacks the SDL Java/native compatibility check. Repackage it with the current Android packager.'
}
if(-not $OutputDirectory) { $OutputDirectory=Join-Path $projectRoot 'dist\Android' }
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
$staging=Join-Path $projectRoot ('.dev\phone-package-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($staging)
$utf8=[Text.UTF8Encoding]::new($false)
$apkName='MH3_Rowell_Dev-arm64.apk'
Copy-Item -LiteralPath $ApkPath -Destination (Join-Path $staging $apkName)
foreach($entry in @(
    @('docs\ANDROID_INSTALL.md','INSTALL.md'),@('docs\CONTROLS.md','CONTROLS.md'),
    @('docs\CHANGES.md','CHANGES.md'),@('CREDITS.md','CREDITS.md'),@('LICENSE','LICENSE.txt')
)) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $entry[0]) -Destination (Join-Path $staging $entry[1])
}
$credits=Join-Path $staging 'CREDITS.md'
$sourceRepository='https://github.com/Dling11/monster_hunter_tri_emulator'
$creditText=[IO.File]::ReadAllText($credits)
$creditText=[regex]::Replace($creditText,'\]\(([^)]+)\)',{
    param($match)
    $target=$match.Groups[1].Value
    if($target -eq 'LICENSE') { return '](LICENSE.txt)' }
    if($target.StartsWith('LICENSES/')) { return ']('+ $target +')' }
    if($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('#')) { return $match.Value }
    return ']('+$sourceRepository+'/blob/main/'+$target+')'
})
[IO.File]::WriteAllText($credits,$creditText,$utf8)
$licenseRoot=Join-Path $projectRoot 'LICENSES'
Copy-Item -LiteralPath $licenseRoot -Destination (Join-Path $staging 'LICENSES') -Recurse
$readme=@'
@APP_NAME@ / Android package

1. Extract this ZIP in your phone's Files app.
2. Tap MH3_Rowell_Dev-arm64.apk and choose Install.
3. Open @APP_NAME@ and follow the storage-access prompt.
4. Supply your own European/PAL Monster Hunter Tri game image (RMHP08).

Read INSTALL.md for the full steps and CONTROLS.md for controls.
App settings: English. Combo 5: R + X + A. Existing controls are retained.
Separate app package: com.rowell.mh3dev
Separate storage folder: Internal storage/MH3_Rowell_Dev

The original app and MH3 save folder stay separate. No saves are auto-imported.
This archive contains the Android app, not the PC executable or game image.
The ZIP is not installed directly: extract it and install the APK inside.
No BlueStacks, Docker or Android Studio is required on the phone.

Version and APK SHA256: VERSION.json
Checksums for archive contents: SHA256SUMS.txt
Source: https://github.com/Dling11/monster_hunter_tri_emulator

Phone gameplay and performance still need testing on real devices.
'@
[IO.File]::WriteAllText((Join-Path $staging 'README.txt'),$readme.Replace('@APP_NAME@',$metadata.appName),$utf8)
$release=[ordered]@{
    appName=$metadata.appName;package=$metadata.package;versionName=$metadata.versionName
    versionCode=$metadata.versionCode;abi=$metadata.abi;minSdk=$metadata.minSdk
    dataFolder=$metadata.dataFolder;apkFile=$apkName;apkSha256=$apkHash
    touchSourceSHA256=$metadata.touchSourceSHA256
    sdlVersion=$metadata.sdlVersion;verifiedJniMethods=$metadata.verifiedJniMethods
    sourceRepository=$sourceRepository
    phoneGameplayVerified=[bool]$PhoneGameplayVerified
}
[IO.File]::WriteAllText((Join-Path $staging 'VERSION.json'),($release | ConvertTo-Json -Depth 5)+"`n",$utf8)
$files=@(Get-ChildItem -LiteralPath $staging -File -Recurse | Sort-Object FullName)
$sums=foreach($file in $files) {
    $relative=$file.FullName.Substring($staging.Length+1).Replace('\','/')
    (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$relative
}
[IO.File]::WriteAllText((Join-Path $staging 'SHA256SUMS.txt'),($sums -join "`n")+"`n",$utf8)

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $OutputDirectory 'MH3-Rowell-Dev-Android.zip'
$temporary=Join-Path $OutputDirectory ([guid]::NewGuid().ToString('N')+'.zip.partial')
$archive=[IO.Compression.ZipFile]::Open($temporary,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($file in (Get-ChildItem -LiteralPath $staging -File -Recurse | Sort-Object FullName)) {
        $compression=if($file.Extension -eq '.apk') { [IO.Compression.CompressionLevel]::NoCompression } else { [IO.Compression.CompressionLevel]::Optimal }
        $relative=$file.FullName.Substring($staging.Length+1).Replace('\','/')
        [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative,$compression)
    }
} finally { $archive.Dispose() }
Move-Item -LiteralPath $temporary -Destination $zip -Force
Copy-Item -LiteralPath $ApkPath -Destination (Join-Path $OutputDirectory $apkName) -Force
Copy-Item -LiteralPath (Join-Path $staging 'VERSION.json') -Destination (Join-Path $OutputDirectory 'VERSION.json') -Force
$zipHash=(Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText(($zip+'.sha256'),$zipHash+'  '+[IO.Path]::GetFileName($zip)+"`n",$utf8)
Write-Host "Phone ZIP: $zip"
Write-Host "ZIP SHA256: $zipHash"
Write-Host "APK SHA256: $apkHash"
