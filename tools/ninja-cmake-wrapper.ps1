$ErrorActionPreference = 'Stop'

$ninjaArguments = [System.Collections.Generic.List[string]]::new()
foreach($argument in $args) {
    $ninjaArguments.Add([string]$argument)
}

$buildDirectory = (Get-Location).Path
for($index = 0; $index -lt $ninjaArguments.Count - 1; $index++) {
    if($ninjaArguments[$index] -eq '-C') {
        $buildDirectory = [System.IO.Path]::GetFullPath($ninjaArguments[$index + 1])
        break
    }
}

$rulesPath = Join-Path $buildDirectory 'CMakeFiles\rules.ninja'
if(Test-Path -LiteralPath $rulesPath) {
    & 'F:\CODEX\ArxFatalis VR\tools\patch-cmake-ninja-link-rules.ps1' -RulesPath $rulesPath
}

$realNinja = 'F:\CODEX\android-toolchain\sdk\cmake\3.22.1\bin\ninja.exe'
$isToolQuery = $ninjaArguments.Contains('--version') -or $ninjaArguments.Contains('-t')
$isTryCompile = $buildDirectory -match '[\\/]CMakeFiles[\\/]CMakeTmp$'

if($isTryCompile -and -not $isToolQuery) {
    $targets = [System.Collections.Generic.List[string]]::new()
    for($index = 0; $index -lt $ninjaArguments.Count; $index++) {
        if($ninjaArguments[$index] -eq '-C') {
            $index++
            continue
        }
        if($ninjaArguments[$index] -in @('-v', '-d', 'explain')) {
            continue
        }
        if(-not $ninjaArguments[$index].StartsWith('-')) {
            $targets.Add($ninjaArguments[$index])
        }
    }

    Push-Location $buildDirectory
    try {
        $commands = if($targets.Count -gt 0) {
            @(& $realNinja -t commands @targets)
        } else {
            @(& $realNinja -t commands)
        }
        foreach($command in $commands) {
            if([string]::IsNullOrWhiteSpace($command)) {
                continue
            }
            & $env:ComSpec /D /S /C $command
            if($LASTEXITCODE -ne 0) {
                exit $LASTEXITCODE
            }
        }
    } finally {
        Pop-Location
    }
    exit 0
}

& $realNinja @ninjaArguments
exit $LASTEXITCODE
