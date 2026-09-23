param([string]$OutputDirectory = 'F:\CODEX\ArxFatalis VR\diagnostics\hand-space-v59')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $root 'PICO-OpenXR-Demos\app\src\main\cpp'
$hand = Get-Content -Raw -LiteralPath (Join-Path $cpp 'demos\hand.cpp')
$app = Get-Content -Raw -LiteralPath (Join-Path $cpp 'demos\application.cpp')
function Extract-BracedFunction([string]$Source, [string]$Signature) {
    $start = $Source.IndexOf($Signature)
    if($start -lt 0) { throw "Missing function: $Signature" }
    $brace = $Source.IndexOf('{', $start)
    $depth = 1
    $end = $brace + 1
    while($depth -gt 0 -and $end -lt $Source.Length) {
        if($Source[$end] -eq '{') { ++$depth }
        if($Source[$end] -eq '}') { --$depth }
        ++$end
    }
    if($depth) { throw 'Unbalanced production function' }
    $Source.Substring($start, $end-$start)
}
$setModel = Extract-BracedFunction $hand 'void Hand::setModel('
$gameStart = $app.IndexOf('const glm::vec3 position(poseState.position[0]')
$gameEnd = $app.IndexOf('mHands->setModel(hand, model);', $gameStart)
if($gameStart -lt 0 -or $gameEnd -lt 0) { throw 'Missing production game-hand transform' }
$game = $app.Substring($gameStart, $gameEnd-$gameStart+'mHands->setModel(hand, model);'.Length)
$template = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'test-arxvr-hand-space.cpp.in')
$generated = $template.Replace('@SET_MODEL@', $setModel).Replace('@GAME_MODEL@', $game)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$sourcePath = Join-Path $OutputDirectory 'hand-space-test.generated.cpp'
[IO.File]::WriteAllText($sourcePath, $generated)
$exe = Join-Path $OutputDirectory 'hand-space-test.exe'
$obj = Join-Path $OutputDirectory 'hand-space-test.obj'
$vcvars = Join-Path $root 'toolchain\VisualStudio2022\VC\Auxiliary\Build\vcvars64.bat'
$compile = '"{0}" >nul && cl /nologo /EHsc /std:c++17 /I"{1}" "{2}" /Fe:"{3}" /Fo:"{4}"' -f $vcvars, (Join-Path $cpp 'third'), $sourcePath, $exe, $obj
& $env:ComSpec /d /s /c $compile
if($LASTEXITCODE) { throw 'Test compilation failed' }
& $exe
if($LASTEXITCODE) { throw 'Hand coordinate-space regression test failed' }
