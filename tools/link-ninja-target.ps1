param(
    [Parameter(Mandatory = $true)]
    [string] $BuildDirectory,

    [Parameter(Mandatory = $true)]
    [string] $TargetFileName,

    [Parameter(Mandatory = $true)]
    [string] $NinjaPath
)

$ErrorActionPreference = 'Stop'

$manifestPath = Join-Path $BuildDirectory 'build.ninja'
$manifestLines = [System.IO.File]::ReadAllLines($manifestPath)
$escapedTarget = [regex]::Escape($TargetFileName)
$edgeIndex = -1

for($i = 0; $i -lt $manifestLines.Length; $i++) {
    if($manifestLines[$i] -match "^build .*${escapedTarget}: ") {
        $edgeIndex = $i
        break
    }
}

if($edgeIndex -lt 0) {
    throw "Build edge for $TargetFileName was not found in $manifestPath"
}

$edge = $manifestLines[$edgeIndex]
$separator = $edge.IndexOf(': ')
$tokens = $edge.Substring($separator + 2) -split '\s+'
$implicitSeparator = [Array]::IndexOf($tokens, '|')
$orderOnlySeparator = [Array]::IndexOf($tokens, '||')
if($implicitSeparator -lt 0 -or
   ($orderOnlySeparator -ge 0 -and $orderOnlySeparator -lt $implicitSeparator)) {
    $implicitSeparator = $orderOnlySeparator
}
if($implicitSeparator -lt 0) {
    $implicitSeparator = $tokens.Length
}

# The first token is the Ninja rule. CMake's response content is the explicit
# input list followed by LINK_PATH and LINK_LIBRARIES.
$responseTokens = [System.Collections.Generic.List[string]]::new()
for($i = 1; $i -lt $implicitSeparator; $i++) {
    $responseTokens.Add($tokens[$i])
}

$responseFileRelative = $null
for($i = $edgeIndex + 1; $i -lt $manifestLines.Length; $i++) {
    $line = $manifestLines[$i]
    if([string]::IsNullOrWhiteSpace($line)) {
        break
    }
    if($line -match '^  (LINK_PATH|LINK_LIBRARIES) = (.*)$') {
        foreach($token in ($Matches[2] -split '\s+')) {
            if(-not [string]::IsNullOrWhiteSpace($token)) {
                $responseTokens.Add($token)
            }
        }
    }
    if($line -match '^  RSP_FILE = (.*)$') {
        $responseFileRelative = $Matches[1]
    }
}

for($i = 0; $i -lt $responseTokens.Count; $i++) {
    $responseTokens[$i] = $responseTokens[$i].Replace('$:', ':').Replace('$ ', ' ').Replace('$$', '$')
}

if(-not $responseFileRelative) {
    throw "RSP_FILE for $TargetFileName was not found"
}

$responsePath = Join-Path $BuildDirectory $responseFileRelative
[System.IO.File]::WriteAllText(
    $responsePath,
    ($responseTokens -join [Environment]::NewLine),
    [System.Text.UTF8Encoding]::new($false)
)

$ninjaTargetFileName = $TargetFileName.Replace('$:', ':').Replace('$ ', ' ').Replace('$$', '$')
$commands = @(& $NinjaPath -C $BuildDirectory -t commands $ninjaTargetFileName)
$responseReference = '@' + $responseFileRelative
$linkCommand = $commands | Where-Object { $_.Contains($responseReference) } | Select-Object -Last 1
if(-not $linkCommand) {
    throw "Link command using $responseReference was not found"
}

Write-Output "Linking $TargetFileName with $($responseTokens.Count) response-file entries"
Push-Location $BuildDirectory
try {
    & $env:ComSpec /D /S /C $linkCommand
    if($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
} finally {
    Pop-Location
}
