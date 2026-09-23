param(
    [string]$Adb = 'F:\CODEX\android-toolchain\sdk\platform-tools\adb.exe',
    [string]$Apk = 'F:\CODEX\ArxFatalis VR\ArxVR-Android\app\build\outputs\apk\debug\app-debug.apk',
    [string]$OutputDirectory = 'F:\CODEX\ArxFatalis VR\diagnostics\performance-passive',
    [int]$RunSeconds = 80
)

$ErrorActionPreference = 'Stop'
$package = 'com.arxvr.android'
$activity = 'com.picovr.openxr.MainActivity'

function Invoke-Adb {
    $adbArguments = @($args)
    $output = & $Adb @adbArguments 2>&1
    if($LASTEXITCODE -ne 0) {
        throw "adb $($adbArguments -join ' ') failed: $output"
    }
    return $output
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Write-Host 'ARXVR_PASSIVE_PERF installing without clearing game data'
Invoke-Adb install -r $Apk | Write-Host

try {
    Invoke-Adb shell input keyevent 224 | Out-Null
    Invoke-Adb shell svc power stayon true | Out-Null
    Invoke-Adb shell setprop debug.arxvr.skip_intro 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_play 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_human 1 | Out-Null
    Invoke-Adb shell am force-stop $package | Out-Null
    Invoke-Adb logcat -c | Out-Null
    Invoke-Adb shell am start -n "$package/$activity" | Write-Host

    # Do not poll logcat or record the screen during the measured interval. Both
    # operations perturb a standalone headset enough to distort frame timing.
    # A sparse wake key is still required when the headset is on a desk: PICO's
    # wear sensor otherwise suspends the XR session and skips automation phases.
    $elapsed = 0
    while($elapsed -lt $RunSeconds) {
        $slice = [Math]::Min(3, $RunSeconds - $elapsed)
        Start-Sleep -Seconds $slice
        $elapsed += $slice
        & $Adb shell input keyevent 224 2>$null | Out-Null
        Write-Host "ARXVR_PASSIVE_PERF elapsed=$elapsed/$RunSeconds"
    }

    $finalLog = (& $Adb logcat -d 2>&1) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'logcat.txt'), $finalLog,
                            [Text.UTF8Encoding]::new($false))
    if($finalLog.Contains('FATAL EXCEPTION') -or $finalLog.Contains('Fatal signal')) {
        throw 'Android crash marker found in logcat'
    }
    if(-not $finalLog.Contains('ARXVR_HUMAN phase=8 name=complete')) {
        throw 'ARXVR_HUMAN did not reach the complete marker during the passive interval'
    }

    Invoke-Adb shell screencap -p /sdcard/Pictures/arxvr-passive-performance-complete.png | Out-Null
    Invoke-Adb pull /sdcard/Pictures/arxvr-passive-performance-complete.png `
        (Join-Path $OutputDirectory 'complete.png') | Out-Null

    $metricLines = @(($finalLog -split "`r?`n") | Where-Object {
        $_ -match 'PxrMetric: FPS=.*Pkg=com\.arxvr\.android|ARXVR_(PERF|SCENE_PERF|UPDATE_PERF|RENDER_PERF)|ARXVR_HUMAN phase='
    })
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'performance.txt'),
                            ($metricLines -join "`r`n"), [Text.UTF8Encoding]::new($false))
    $metricLines | Select-Object -Last 40 | Write-Host
} finally {
    Write-Host 'ARXVR_PASSIVE_PERF stopping application and resetting diagnostics'
    & $Adb shell setprop debug.arxvr.auto_human 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.auto_play 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.skip_intro 0 2>$null | Out-Null
    & $Adb shell am force-stop $package 2>$null | Out-Null
    & $Adb shell svc power stayon false 2>$null | Out-Null
}
