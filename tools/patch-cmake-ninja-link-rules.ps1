param(
    [Parameter(Mandatory = $true)]
    [string] $RulesPath
)

$ErrorActionPreference = 'Stop'

if(-not (Test-Path -LiteralPath $RulesPath)) {
    exit 0
}

$rules = [System.IO.File]::ReadAllText($RulesPath)
$patched = [regex]::Replace(
    $rules,
    '(?m)^(\s*command = )cmd\.exe /C "\$PRE_LINK && (.*) && \$POST_BUILD"\r?$',
    '$1$2'
)

# CMake 3.22 emits SDL header copy commands with an explicit nested cmd.exe.
# The copies use absolute paths, so execute cmake -E directly.
$patched = [regex]::Replace(
    $patched,
    '(?m)^(\s*COMMAND = )cmd\.exe /C \\"cd /D [^"]+ && ([^"\r\n]*cmake\.exe -E copy_if_different [^"\r\n]+)\\"\r?$',
    '$1$2'
)

$copyPrefix = [regex]::Escape('cmd.exe /C "cd /D ')
$ninjaQuote = [regex]::Escape('"')
$copyPattern = '(?m)^(\s*COMMAND = )' + $copyPrefix
$copyPattern += '[^"]+ && ([^"\r\n]*cmake\.exe -E copy_if_different [^"\r\n]+)'
$copyPattern += $ninjaQuote + '\r?$'
$patched = [regex]::Replace($patched, $copyPattern, '$1$2')

if($patched -ne $rules) {
    [System.IO.File]::WriteAllText(
        $RulesPath,
        $patched,
        [System.Text.UTF8Encoding]::new($false)
    )
}
