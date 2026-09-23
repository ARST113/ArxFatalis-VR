param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string] $Target,

    [int] $Jobs = 8
)

$ErrorActionPreference = 'Stop'

$ninja = 'F:\CODEX\android-toolchain\sdk\cmake\3.22.1\bin\ninja.exe'
$commands = @(& $ninja -C $BuildDirectory -t commands $Target)
if($LASTEXITCODE -ne 0 -or $commands.Count -eq 0) {
    throw "Could not obtain Ninja commands for $Target"
}

function Normalize-Command([string] $Command) {
    $prefix = 'cmd.exe /C "'
    if($Command.StartsWith($prefix) -and $Command.EndsWith('"')) {
        return $Command.Substring($prefix.Length, $Command.Length - $prefix.Length - 1)
    }
    return $Command
}

function Start-BuildProcess([string] $Command) {
    $normalized = Normalize-Command $Command
    $info = [System.Diagnostics.ProcessStartInfo]::new()
    $info.WorkingDirectory = $BuildDirectory
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    if($normalized -match '^(?<Program>\S*clang(?:\+\+)?\.exe)\s+(?<Arguments>.*)$') {
        # Preserve Clang's command line exactly. Passing Ninja's escaped macro
        # definitions through an extra cmd.exe ArgumentList changes their
        # quoting on Windows.
        $info.FileName = $Matches.Program
        $info.Arguments = $Matches.Arguments
    } else {
        $info.FileName = $env:ComSpec
        $info.ArgumentList.Add('/D')
        $info.ArgumentList.Add('/S')
        $info.ArgumentList.Add('/C')
        $info.ArgumentList.Add($normalized)
    }
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $info
    if(-not $process.Start()) {
        throw 'Failed to start build process'
    }
    return $process
}

function Invoke-BuildCommand([string] $Command) {
    $process = Start-BuildProcess $Command
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $process.Dispose()
    if($exitCode -ne 0) {
        throw "Build command failed with exit code ${exitCode}: $Command"
    }
}

$compileCommands = [System.Collections.Generic.List[string]]::new()
$prerequisiteCommands = [System.Collections.Generic.List[string]]::new()
$linkCommands = [System.Collections.Generic.List[string]]::new()

foreach($command in $commands) {
    # Generated OpenAL/Arx sources are prepared explicitly before this script;
    # their nested cmd.exe quoting is the same construct that stalls Ninja.
    if($command.StartsWith('cmd.exe /C "')) {
        continue
    } elseif($command -match 'clang(?:\+\+)?\.exe .* -c ') {
		if($command -match ' -o (?<Output>\S+) -c (?<Source>\S+)') {
			$objectPath = Join-Path $BuildDirectory $Matches.Output
			$sourcePath = $Matches.Source
			if((Test-Path -LiteralPath $objectPath) -and
			   (Test-Path -LiteralPath $sourcePath) -and
			   (Get-Item -LiteralPath $objectPath).LastWriteTimeUtc -ge
			       (Get-Item -LiteralPath $sourcePath).LastWriteTimeUtc) {
				continue
			}
		}
        $compileCommands.Add($command)
    } elseif($command -match 'cmake\.exe -E rm -f ' -or
             ($command -match 'clang(?:\+\+)?\.exe ' -and $command -notmatch ' -c ')) {
        $linkCommands.Add($command)
    } else {
        $prerequisiteCommands.Add($command)
    }
}

Write-Output "Prerequisites: $($prerequisiteCommands.Count)"
foreach($command in $prerequisiteCommands) {
    Invoke-BuildCommand $command
}

Write-Output "Compiling: $($compileCommands.Count) commands with $Jobs workers"
$running = [System.Collections.Generic.List[object]]::new()
$completed = 0

foreach($command in $compileCommands) {
    while($running.Count -ge $Jobs) {
        $finished = $null
        foreach($entry in $running) {
            if($entry.Process.HasExited) {
                $finished = $entry
                break
            }
        }
        if(-not $finished) {
            Start-Sleep -Milliseconds 40
            continue
        }
        $finished.Process.WaitForExit()
        $exitCode = $finished.Process.ExitCode
        $finished.Process.Dispose()
        [void] $running.Remove($finished)
        $completed++
        if(($completed % 25) -eq 0 -or $completed -eq $compileCommands.Count) {
            Write-Output "Compiled $completed / $($compileCommands.Count)"
        }
        if($exitCode -ne 0) {
            throw "Compile command failed with exit code ${exitCode}: $($finished.Command)"
        }
    }

    $running.Add([pscustomobject]@{
        Process = Start-BuildProcess $command
        Command = $command
    })
}

while($running.Count -gt 0) {
    $finished = $null
    foreach($entry in $running) {
        if($entry.Process.HasExited) {
            $finished = $entry
            break
        }
    }
    if(-not $finished) {
        Start-Sleep -Milliseconds 40
        continue
    }
    $finished.Process.WaitForExit()
    $exitCode = $finished.Process.ExitCode
    $finished.Process.Dispose()
    [void] $running.Remove($finished)
    $completed++
    if(($completed % 25) -eq 0 -or $completed -eq $compileCommands.Count) {
        Write-Output "Compiled $completed / $($compileCommands.Count)"
    }
    if($exitCode -ne 0) {
        throw "Compile command failed with exit code ${exitCode}: $($finished.Command)"
    }
}

Write-Output "Linking: $($linkCommands.Count) commands"
foreach($command in $linkCommands) {
    Invoke-BuildCommand $command
}

Write-Output "Built $Target successfully"
