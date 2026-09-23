param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string] $TargetFileName,

    [Parameter(Mandatory = $true)]
    [string] $NinjaPath
)

$ErrorActionPreference = 'Stop'
$commands = @(& $NinjaPath -C $BuildDirectory -t commands $TargetFileName)
$missing = [System.Collections.Generic.List[object]]::new()

foreach($command in $commands) {
    if($command -match '(?:^|\s)-o\s+([^\s]+\.o)\s+-c\s+') {
        $relativeOutput = $Matches[1].Trim('"')
        $outputPath = if([System.IO.Path]::IsPathRooted($relativeOutput)) {
            $relativeOutput
        } else {
            Join-Path $BuildDirectory $relativeOutput
        }
        if(-not (Test-Path -LiteralPath $outputPath)) {
            $missing.Add([pscustomobject]@{
                Command = $command
                Output = $outputPath
            })
        }
    }
}

Write-Output "Missing object files: $($missing.Count)"
Push-Location $BuildDirectory
try {
    $index = 0
    foreach($item in $missing) {
        $index++
        Write-Output "Compiling $index/$($missing.Count): $([System.IO.Path]::GetFileName($item.Output))"
        & $env:ComSpec /D /S /C $item.Command
        if($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }
} finally {
    Pop-Location
}

