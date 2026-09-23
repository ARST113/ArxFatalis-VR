param(
    [string]$Adb = 'F:\CODEX\android-toolchain\sdk\platform-tools\adb.exe',
    [string]$Apk = 'F:\CODEX\ArxFatalis VR\ArxVR-Android\app\build\outputs\apk\debug\app-debug.apk',
    [string]$OutputDirectory = 'F:\CODEX\ArxFatalis VR\diagnostics\auto-human',
    [switch]$VideoOnly,
    [switch]$StopAfterHead6Dof
)

$ErrorActionPreference = 'Stop'
$package = 'com.arxvr.android'
$activity = 'com.picovr.openxr.MainActivity'
$remoteDirectory = '/sdcard/Pictures/ArxVR-Auto'
$remoteVideo = "$remoteDirectory/arxvr-human-test.mp4"
$recordProcess = $null

function Invoke-Adb {
    $adbArguments = @($args)
    $output = & $Adb @adbArguments 2>&1
    if ($LASTEXITCODE -ne 0) {
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
    Write-Host "ARXVR_TEST screenshot=$Name path=$local"
}

function Measure-StereoImage {
    param([string]$Path)
    Add-Type -AssemblyName System.Drawing
    $bitmap = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $results = @()
        $halfWidth = [int]($bitmap.Width / 2)
        foreach ($eye in 0..1) {
            [long]$samples = 0
            [long]$visible = 0
            [double]$luminanceSum = 0
            $xStart = $eye * $halfWidth
            $xEnd = $xStart + $halfWidth
            for ($y = 0; $y -lt $bitmap.Height; $y += 8) {
                for ($x = $xStart; $x -lt $xEnd; $x += 8) {
                    $pixel = $bitmap.GetPixel($x, $y)
                    $luminance = 0.2126 * $pixel.R + 0.7152 * $pixel.G + 0.0722 * $pixel.B
                    $luminanceSum += $luminance
                    if ($luminance -gt 8) { $visible++ }
                    $samples++
                }
            }
            $results += [pscustomobject]@{
                File = [IO.Path]::GetFileName($Path)
                Eye = if ($eye -eq 0) { 'left' } else { 'right' }
                Width = $halfWidth
                Height = $bitmap.Height
                MeanLuminance = [math]::Round($luminanceSum / [math]::Max(1, $samples), 3)
                VisibleRatio = [math]::Round($visible / [math]::Max(1, $samples), 4)
            }
        }
        return $results
    } finally {
        $bitmap.Dispose()
    }
}

$markers = [ordered]@{
    'checkpoint=yaw_090' = 'yaw-090'
    'checkpoint=yaw_180' = 'yaw-180'
    'checkpoint=yaw_270' = 'yaw-270'
    'phase=2 name=yaw_return_settle' = 'yaw-360'
    'phase=4 name=pitch_up_to_down' = 'pitch-up-55'
    'phase=5 name=pitch_return' = 'pitch-down-55'
    'pause=non_stereo' = 'scripted-cutscene'
    'phase=6 name=head_6dof' = 'head-6dof'
    'ArxVR physical crouch: active=1' = 'physical-crouch'
    'phase=7 name=hands_and_actions' = 'hands-moving'
    'action=right_lower_grip_interact' = 'right-lower-grip'
    'ArxVR physical grip:' = 'physical-grip'
    'action=right_trigger_rune' = 'right-trigger-rune'
    'action=left_squeeze_magic' = 'left-squeeze-magic'
    'phase=8 name=complete' = 'complete'
}
$captured = @{}
$eventLines = [System.Collections.Generic.List[string]]::new()

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Write-Host 'ARXVR_TEST installing without clearing game data'
Invoke-Adb install -r $Apk | Write-Host

try {
    Invoke-Adb shell input keyevent 224 | Out-Null
    Invoke-Adb shell svc power stayon true | Out-Null
    Invoke-Adb shell mkdir -p $remoteDirectory | Out-Null
    Invoke-Adb shell setprop debug.arxvr.skip_intro 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_play 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_human 1 | Out-Null
    Invoke-Adb shell am force-stop $package | Out-Null
    Invoke-Adb logcat -G 32M | Out-Null
    Invoke-Adb logcat -c | Out-Null

    Write-Host 'ARXVR_TEST launching real game and recording headset compositor'
    Invoke-Adb shell am start -n "$package/$activity" | Write-Host
    $recordProcess = Start-Process -FilePath $Adb -ArgumentList @(
        'shell', 'screenrecord', '--bit-rate', '12000000', '--time-limit', '120', $remoteVideo
    ) -WindowStyle Hidden -PassThru

    $deadline = [DateTime]::UtcNow.AddMinutes(3)
    $complete = $false
    $lastWake = [DateTime]::MinValue
    while ([DateTime]::UtcNow -lt $deadline -and -not $complete) {
        Start-Sleep -Seconds 1
        if (([DateTime]::UtcNow - $lastWake).TotalSeconds -ge 3) {
            & $Adb shell input keyevent 224 2>$null | Out-Null
            $lastWake = [DateTime]::UtcNow
        }
        $log = (& $Adb logcat -d 2>&1) -join "`n"
        foreach ($entry in $markers.GetEnumerator()) {
            if (-not $captured.ContainsKey($entry.Key) -and $log.Contains($entry.Key)) {
                if (-not $VideoOnly -or $entry.Key -eq 'phase=8 name=complete' -or $entry.Key -eq 'pause=non_stereo') {
                    Save-Screenshot $entry.Value
                }
                $captured[$entry.Key] = $true
                $matchingLine = @($log -split "`r?`n" | Where-Object { $_.Contains($entry.Key) })[-1]
                $eventLines.Add($matchingLine)
                if ($entry.Key -eq 'phase=8 name=complete' -or
                    ($StopAfterHead6Dof -and $entry.Key -eq 'phase=6 name=head_6dof')) {
                    $complete = $true
                }
            }
        }
        if ($log.Contains('FATAL EXCEPTION') -or $log.Contains('Fatal signal')) {
            throw 'Android crash marker found in logcat'
        }
    }
    if (-not $complete) {
        throw 'Timed out before ARXVR_HUMAN complete marker'
    }

    Start-Sleep -Seconds 3
    $screenRecordPid = ((& $Adb shell pidof screenrecord 2>$null) -join '').Trim()
    if ($screenRecordPid) {
        & $Adb shell kill -2 $screenRecordPid 2>$null | Out-Null
    }
    if ($recordProcess) {
        $recordProcess.WaitForExit(10000) | Out-Null
        if (-not $recordProcess.HasExited) { $recordProcess.Kill() }
    }
    Invoke-Adb pull $remoteVideo (Join-Path $OutputDirectory 'arxvr-human-test.mp4') | Out-Null

    $finalLog = (& $Adb logcat -d 2>&1) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'logcat.txt'), $finalLog,
                            [Text.UTF8Encoding]::new($false))
    $humanLines = @(($finalLog -split "`r?`n") | Where-Object {
        $_ -match 'ARXVR_HUMAN|ArxVR physical crouch|ArxVR physical grip|ArxVR body followed head|ArxVR auto-walk|vrRenderState|Fatal signal|FATAL EXCEPTION'
    })
    foreach ($line in $humanLines) { $eventLines.Add($line) }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'test-events.txt'),
                            ($eventLines -join "`r`n"), [Text.UTF8Encoding]::new($false))

    $measurements = Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png' |
        Sort-Object Name | ForEach-Object { Measure-StereoImage $_.FullName }
    $measurements | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'stereo-image-metrics.csv') -NoTypeInformation -Encoding UTF8
    $darkEyes = @($measurements | Where-Object { $_.VisibleRatio -lt 0.02 })
    $requiredMarkers = if ($StopAfterHead6Dof) {
        @('checkpoint=yaw_090', 'checkpoint=yaw_180', 'checkpoint=yaw_270',
          'phase=2 name=yaw_return_settle', 'phase=4 name=pitch_up_to_down',
          'phase=5 name=pitch_return', 'phase=6 name=head_6dof')
    } else {
        @($markers.Keys | Where-Object { $_ -ne 'pause=non_stereo' })
    }
    $missingMarkers = @($requiredMarkers | Where-Object { -not $captured.ContainsKey($_) })
    $summary = [pscustomobject]@{
        Completed = $complete
        Screenshots = @($captured.Keys).Count
        ExpectedScreenshots = $requiredMarkers.Count
        MissingMarkers = $missingMarkers.Count
        DarkEyes = $darkEyes.Count
        Video = (Join-Path $OutputDirectory 'arxvr-human-test.mp4')
    }
    $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding UTF8
    $summary | Format-List | Out-String | Write-Host
    if ($missingMarkers.Count -gt 0 -or $darkEyes.Count -gt 0) {
        throw "Visual test incomplete: captured=$($captured.Count)/$($requiredMarkers.Count), missing=$($missingMarkers.Count), darkEyes=$($darkEyes.Count)"
    }
} finally {
    Write-Host 'ARXVR_TEST disabling diagnostics and stopping application'
    & $Adb shell setprop debug.arxvr.auto_human 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.auto_play 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.skip_intro 0 2>$null | Out-Null
    & $Adb shell am force-stop $package 2>$null | Out-Null
    & $Adb shell svc power stayon false 2>$null | Out-Null
    if ($recordProcess -and -not $recordProcess.HasExited) {
        $screenRecordPid = ((& $Adb shell pidof screenrecord 2>$null) -join '').Trim()
        if ($screenRecordPid) {
            & $Adb shell kill -2 $screenRecordPid 2>$null | Out-Null
        }
        $recordProcess.WaitForExit(3000) | Out-Null
        if (-not $recordProcess.HasExited) { $recordProcess.Kill() }
    }
}
