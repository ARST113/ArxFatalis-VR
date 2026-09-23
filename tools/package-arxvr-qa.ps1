param(
    [string]$BaseApk = 'F:\CODEX\ArxFatalis VR\ArxVR-Android\app\build\outputs\apk\debug\app-debug-manualfix-v57.apk',
    [string]$OutputApk = 'F:\CODEX\ArxFatalis VR\ArxVR-Android\app\build\outputs\apk\debug\app-debug-v58-qa.apk'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$hostLibrary = Join-Path $root 'PICO-OpenXR-Demos\app\build-codex\intermediates\cxx\Debug\1q6z8e58\lib\arm64-v8a\libopenxr_demos.so'
$buildTools = 'F:\CODEX\android-toolchain\sdk\build-tools\35.0.0'
$env:JAVA_HOME = 'F:\CODEX\android-toolchain\jdk\jdk-17.0.20+8'
$staging = "$OutputApk.unsigned"
if((Test-Path -LiteralPath $OutputApk) -or (Test-Path -LiteralPath $staging)) {
    throw 'Use a new output name; existing test builds are preserved.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
Copy-Item -LiteralPath $BaseApk -Destination $staging
$zip = [IO.Compression.ZipFile]::Open($staging, [IO.Compression.ZipArchiveMode]::Update)
try {
    $zip.GetEntry('lib/arm64-v8a/libopenxr_demos.so').Delete()
    foreach($entry in @($zip.Entries | Where-Object { $_.FullName -match '^META-INF/.*\.(SF|RSA|DSA|EC)$' -or $_.FullName -eq 'META-INF/MANIFEST.MF' })) {
        $entry.Delete()
    }
    [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $hostLibrary,
        'lib/arm64-v8a/libopenxr_demos.so', [IO.Compression.CompressionLevel]::NoCompression) | Out-Null
} finally { $zip.Dispose() }
& "$buildTools\zipalign.exe" -f -P 16 4 $staging $OutputApk
if($LASTEXITCODE) { throw 'zipalign failed' }
& "$buildTools\apksigner.bat" sign --ks (Join-Path $root '.android-home\debug.keystore') --ks-key-alias androiddebugkey --ks-pass pass:android --key-pass pass:android $OutputApk
if($LASTEXITCODE) { throw 'apksigner failed' }
& "$buildTools\zipalign.exe" -c -P 16 4 $OutputApk
if($LASTEXITCODE) { throw 'APK alignment verification failed' }
& "$buildTools\apksigner.bat" verify --verbose --print-certs $OutputApk
if($LASTEXITCODE) { throw 'APK signature verification failed' }

function EntryHash($Entry) {
    $stream = $Entry.Open()
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToHexString($sha.ComputeHash($stream)) }
    finally { $sha.Dispose(); $stream.Dispose() }
}
$baseZip = [IO.Compression.ZipFile]::OpenRead($BaseApk)
$qaZip = [IO.Compression.ZipFile]::OpenRead($OutputApk)
try {
    foreach($entry in $baseZip.Entries) {
        if($entry.FullName -match '^META-INF/' -or $entry.FullName -eq 'lib/arm64-v8a/libopenxr_demos.so') { continue }
        $newEntry = $qaZip.GetEntry($entry.FullName)
        if(!$newEntry -or (EntryHash $entry) -ne (EntryHash $newEntry)) {
            throw "Unexpected APK content change: $($entry.FullName)"
        }
    }
    $actual = EntryHash ($qaZip.GetEntry('lib/arm64-v8a/libopenxr_demos.so'))
    if($actual -ne (Get-FileHash $hostLibrary -Algorithm SHA256).Hash) { throw 'Wrong host library packaged' }
} finally { $baseZip.Dispose(); $qaZip.Dispose() }
Get-FileHash -LiteralPath $OutputApk -Algorithm SHA256 | Format-List
"PASS: only the OpenXR host library and APK signing metadata changed from $BaseApk."
