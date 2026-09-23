param(
    [string]$Adb = 'F:\CODEX\android-toolchain\sdk\platform-tools\adb.exe',
    [string]$Apk = 'F:\CODEX\ArxFatalis VR\ArxVR-Android\app\build\outputs\apk\debug\app-debug.apk',
    [string]$OutputDirectory = 'F:\CODEX\ArxFatalis VR\diagnostics\menu-transition'
)

$ErrorActionPreference = 'Stop'
$package = 'com.arxvr.android'
$activity = 'com.picovr.openxr.MainActivity'
$remoteDirectory = '/sdcard/Pictures/ArxVR-Menu-Test'
$remoteVideo = "$remoteDirectory/menu-transition.mp4"
$recordProcess = $null

function Invoke-Adb {
    $adbArguments = @($args)
    $output = & $Adb @adbArguments 2>&1
    if($LASTEXITCODE -ne 0) {
        throw "adb $($adbArguments -join ' ') failed: $output"
    }
    return $output
}

function Save-Screenshot {
    param([string]$Name)
    $remote = "$remoteDirectory/$Name.png"
    $local = Join-Path $OutputDirectory "$Name.png"
    Invoke-Adb shell screencap -p $remote | Out-Null
    Invoke-Adb pull $remote $local | Out-Null
    Write-Host "ARXVR_MENU_TEST screenshot=$Name path=$local"
}

function Wait-ForLogMarker {
    param([string]$Marker, [int]$TimeoutSeconds)
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    $lastWake = [DateTime]::MinValue
    while([DateTime]::UtcNow -lt $deadline) {
        $log = (& $Adb logcat -d 2>&1) -join "`n"
        if($log.Contains($Marker)) {
            return $log
        }
        if(([DateTime]::UtcNow - $lastWake).TotalSeconds -ge 3) {
            & $Adb shell input keyevent 224 2>$null | Out-Null
            $lastWake = [DateTime]::UtcNow
        }
        Start-Sleep -Seconds 1
    }
    throw "Timed out waiting for log marker: $Marker"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Write-Host 'ARXVR_MENU_TEST installing without clearing game data'
Invoke-Adb install -r $Apk | Write-Host

try {
    Invoke-Adb shell input keyevent 224 | Out-Null
    Invoke-Adb shell svc power stayon true | Out-Null
    Invoke-Adb shell mkdir -p $remoteDirectory | Out-Null
    Invoke-Adb shell setprop debug.arxvr.skip_intro 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_play 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_human 1 | Out-Null
    Invoke-Adb shell am force-stop $package | Out-Null
    Invoke-Adb logcat -c | Out-Null
    Invoke-Adb shell am start -n "$package/$activity" | Write-Host

    Write-Host 'ARXVR_MENU_TEST waiting for the lower-grip world interaction'
    Wait-ForLogMarker 'ARXVR_HUMAN action=right_lower_grip_interact' 150 | Out-Null
    Save-Screenshot 'hands-and-lower-grip'

    Write-Host 'ARXVR_MENU_TEST waiting for completed real-level movement run'
    $gameplayLog = Wait-ForLogMarker 'ARXVR_HUMAN phase=8 name=complete' 30
    $legacyGripReached = $gameplayLog -match 'ArxVR interaction: lowerGrip=1.*target=(?!none)'
    $directGripReached = $gameplayLog -match 'ArxVR direct interaction: control=grip.*target=(?!none)'
    if(-not ($legacyGripReached -or $directGripReached)) {
        throw 'Lower-grip click did not reach a real interactive world entity'
    }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'gameplay-logcat.txt'),
                            $gameplayLog, [Text.UTF8Encoding]::new($false))
    Save-Screenshot 'gameplay-before-menu'

    Invoke-Adb logcat -c | Out-Null
    $recordProcess = Start-Process -FilePath $Adb -ArgumentList @(
        'shell', 'screenrecord', '--bit-rate', '12000000', '--time-limit', '25', $remoteVideo
    ) -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 1

    Write-Host 'ARXVR_MENU_TEST opening pause menu with the engine B/Menu input'
    Invoke-Adb shell setprop debug.arxvr.force_menu 1 | Out-Null
    Start-Sleep -Milliseconds 350
    Invoke-Adb shell setprop debug.arxvr.force_menu 0 | Out-Null
    $menuLog = Wait-ForLogMarker 'stereo to world-fixed 2D transition complete' 12
    Start-Sleep -Seconds 2
    Save-Screenshot 'menu-open'

    Invoke-Adb logcat -c | Out-Null
    Write-Host 'ARXVR_MENU_TEST closing pause menu with a second B/Menu click'
    Invoke-Adb shell setprop debug.arxvr.force_menu 1 | Out-Null
    Start-Sleep -Milliseconds 350
    Invoke-Adb shell setprop debug.arxvr.force_menu 0 | Out-Null
    $resumeLog = Wait-ForLogMarker 'head pose recentered on stereo gameplay entry' 12
    Start-Sleep -Seconds 2
    Save-Screenshot 'gameplay-resumed'

    $screenRecordPid = ((& $Adb shell pidof screenrecord 2>$null) -join '').Trim()
    if($screenRecordPid) {
        & $Adb shell kill -2 $screenRecordPid 2>$null | Out-Null
    }
    if($recordProcess) {
        $recordProcess.WaitForExit(10000) | Out-Null
        if(-not $recordProcess.HasExited) { $recordProcess.Kill() }
    }
    Invoke-Adb pull $remoteVideo (Join-Path $OutputDirectory 'menu-transition.mp4') | Out-Null

    $combinedLog = $menuLog + "`r`n" + $resumeLog
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'transition-logcat.txt'),
                            $combinedLog, [Text.UTF8Encoding]::new($false))
    if($combinedLog.Contains('FATAL EXCEPTION') -or $combinedLog.Contains('Fatal signal')) {
        throw 'Android crash marker found in menu transition log'
    }
    Write-Host 'ARXVR_MENU_TEST complete: stereo -> world-fixed menu -> stereo'
} finally {
    & $Adb shell setprop debug.arxvr.auto_human 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.auto_play 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.skip_intro 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.force_menu 0 2>$null | Out-Null
    & $Adb shell am force-stop $package 2>$null | Out-Null
    & $Adb shell svc power stayon false 2>$null | Out-Null
    if($recordProcess -and -not $recordProcess.HasExited) {
        $screenRecordPid = ((& $Adb shell pidof screenrecord 2>$null) -join '').Trim()
        if($screenRecordPid) {
            & $Adb shell kill -2 $screenRecordPid 2>$null | Out-Null
        }
        $recordProcess.WaitForExit(3000) | Out-Null
        if(-not $recordProcess.HasExited) { $recordProcess.Kill() }
    }
}
