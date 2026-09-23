param(
    [string]$Adb = 'F:\CODEX\android-toolchain\sdk\platform-tools\adb.exe',
    [string]$Apk = 'F:\CODEX\ArxFatalis VR\ArxVR-Android\app\build\outputs\apk\debug\app-debug.apk',
    [string]$OutputDirectory = 'F:\CODEX\ArxFatalis VR\diagnostics\auto-traversal-v1'
)

$ErrorActionPreference = 'Stop'
$package = 'com.arxvr.android'
$activity = 'com.picovr.openxr.MainActivity'
$remoteDirectory = '/sdcard/Pictures/ArxVR-Traversal'
$remoteVideo = "$remoteDirectory/arxvr-traversal.mp4"
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
    Write-Host "ARXVR_TRAVERSAL screenshot=$Name path=$local"
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
    'reached=jail_stone_0003' = '01-reached-stone'
    'interaction=physical_lower_grip target=jail_stone_0003' = '02-grip-stone'
    'released=jail_stone_0003' = '03-stone-removed'
    'ArxVR direct interaction: control=trigger' = '03b-direct-trigger'
    'story_event=STONE interaction=trigger target=untwisted_portcullis_2_0003' = '04-bars-opened'
    'crossed=untwisted_portcullis_2_0003' = '04b-crossed-bars'
    'interaction=action target=lever_0011' = '04c-jail-lever'
    'combat=guard_defeated target=goblin_base_0006' = '05-guard-defeated'
	'waypoint=marker_0258 crossed_porticullis_0013=1' = '05b-crossed-jail-gate'
	'waypoint=marker_0150 corridor_turn_complete=1' = '05c-corridor-turn'
    'interaction=action target=lever_0012' = '06-kultar-lever'
    'reached=jail_wood_grid_0001' = '07-trapdoor'
    'physical_crouch_confirmed=1' = '08-physical-crouch'
    'interaction=hit target=jail_wood_grid_0001' = '09-trapdoor-broken'
	'physical_stand_confirmed=1' = '09b-physical-stand'
    'ARXVR_TRAVERSE level_change' = '10-next-location'
    'ARXVR_TRAVERSE complete next_location_loaded=1' = '11-complete'
}
$captured = @{}
$eventLines = [System.Collections.Generic.List[string]]::new()

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
Write-Host 'ARXVR_TRAVERSAL installing APK with -r (game data and saves preserved)'
Invoke-Adb install -r $Apk | Write-Host

try {
    Invoke-Adb shell input keyevent 224 | Out-Null
    Invoke-Adb shell svc power stayon true | Out-Null
    Invoke-Adb shell mkdir -p $remoteDirectory | Out-Null
    Invoke-Adb shell setprop debug.arxvr.skip_intro 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_play 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_human 1 | Out-Null
    Invoke-Adb shell setprop debug.arxvr.auto_traverse 1 | Out-Null
    Invoke-Adb shell am force-stop $package | Out-Null
    Invoke-Adb logcat -G 32M | Out-Null
    Invoke-Adb logcat -c | Out-Null

    Write-Host 'ARXVR_TRAVERSAL launching new quest and recording both compositor eyes'
    Invoke-Adb shell am start -n "$package/$activity" | Write-Host
    $recordProcess = Start-Process -FilePath $Adb -ArgumentList @(
        'shell', 'screenrecord', '--bit-rate', '12000000', '--time-limit', '180', $remoteVideo
    ) -WindowStyle Hidden -PassThru

    $deadline = [DateTime]::UtcNow.AddMinutes(5)
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
                Save-Screenshot $entry.Value
                $captured[$entry.Key] = $true
                $matchingLine = @($log -split "`r?`n" | Where-Object { $_.Contains($entry.Key) })[-1]
                $eventLines.Add($matchingLine)
                if ($entry.Key -eq 'ARXVR_TRAVERSE complete next_location_loaded=1') {
                    $complete = $true
                }
            }
        }
        if ($log.Contains('ARXVR_TRAVERSE failed reason=')) {
            $failure = @($log -split "`r?`n" | Where-Object { $_.Contains('ARXVR_TRAVERSE failed reason=') })[-1]
            throw "Traversal failed: $failure"
        }
        if ($log.Contains('FATAL EXCEPTION') -or $log.Contains('Fatal signal')) {
            throw 'Android crash marker found in logcat'
        }
    }
    if (-not $complete) {
        throw 'Timed out before the next location was loaded'
    }

    Start-Sleep -Seconds 4
    $screenRecordPid = ((& $Adb shell pidof screenrecord 2>$null) -join '').Trim()
    if ($screenRecordPid) { & $Adb shell kill -2 $screenRecordPid 2>$null | Out-Null }
    if ($recordProcess) {
        $recordProcess.WaitForExit(10000) | Out-Null
        if (-not $recordProcess.HasExited) { $recordProcess.Kill() }
    }
    Invoke-Adb pull $remoteVideo (Join-Path $OutputDirectory 'arxvr-traversal.mp4') | Out-Null

    $finalLog = (& $Adb logcat -d 2>&1) -join "`r`n"
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'logcat.txt'), $finalLog,
                            [Text.UTF8Encoding]::new($false))
    $routeLines = @(($finalLog -split "`r?`n") | Where-Object {
        $_ -match 'ARXVR_TRAVERSE|ArxVR physical crouch|ArxVR physical grip|ArxVR locomotion|Loading level|teleport to|Fatal signal|FATAL EXCEPTION'
    })
    foreach ($line in $routeLines) { $eventLines.Add($line) }
    [IO.File]::WriteAllText((Join-Path $OutputDirectory 'test-events.txt'),
                            ($eventLines -join "`r`n"), [Text.UTF8Encoding]::new($false))

    $measurements = Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.png' |
        Sort-Object Name | ForEach-Object { Measure-StereoImage $_.FullName }
    $measurements | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'stereo-image-metrics.csv') -NoTypeInformation -Encoding UTF8
    $darkEyes = @($measurements | Where-Object { $_.VisibleRatio -lt 0.02 })
    $missingMarkers = @($markers.Keys | Where-Object { -not $captured.ContainsKey($_) })
    $summary = [pscustomobject]@{
        Completed = $complete
        StartToNextLocation = $captured.ContainsKey('ARXVR_TRAVERSE level_change')
        Screenshots = @($captured.Keys).Count
        ExpectedScreenshots = $markers.Count
        MissingMarkers = $missingMarkers.Count
        DarkEyes = $darkEyes.Count
        Video = (Join-Path $OutputDirectory 'arxvr-traversal.mp4')
    }
    $summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json') -Encoding UTF8
    $summary | Format-List | Out-String | Write-Host
    if ($missingMarkers.Count -gt 0 -or $darkEyes.Count -gt 0) {
        throw "Traversal visual evidence incomplete: missing=$($missingMarkers.Count), darkEyes=$($darkEyes.Count)"
    }
} finally {
    Write-Host 'ARXVR_TRAVERSAL disabling diagnostics and stopping application'
    & $Adb shell setprop debug.arxvr.auto_traverse 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.auto_human 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.auto_play 0 2>$null | Out-Null
    & $Adb shell setprop debug.arxvr.skip_intro 0 2>$null | Out-Null
    & $Adb shell am force-stop $package 2>$null | Out-Null
    & $Adb shell svc power stayon false 2>$null | Out-Null
    if ($recordProcess -and -not $recordProcess.HasExited) {
        $screenRecordPid = ((& $Adb shell pidof screenrecord 2>$null) -join '').Trim()
        if ($screenRecordPid) { & $Adb shell kill -2 $screenRecordPid 2>$null | Out-Null }
        $recordProcess.WaitForExit(3000) | Out-Null
        if (-not $recordProcess.HasExited) { $recordProcess.Kill() }
    }
}
