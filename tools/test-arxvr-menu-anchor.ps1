param([string]$OutputDirectory = 'F:\CODEX\ArxFatalis VR\diagnostics\menu-anchor-v59')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cpp = Join-Path $root 'PICO-OpenXR-Demos\app\src\main\cpp'
$app = Get-Content -Raw -LiteralPath (Join-Path $cpp 'demos\application.cpp')
$start = $app.IndexOf('const glm::quat headOrientation =', $app.IndexOf('void Application::renderArxTexture('))
$end = $app.IndexOf('textureProjection = project;', $start)
if($start -lt 0 -or $end -lt 0) { throw 'Production menu anchor block not found' }
$anchor = $app.Substring($start, $end-$start)
$signature = 'void Application::setTrackingFrameHead('
$callbackStart = $app.IndexOf($signature)
if($callbackStart -lt 0) {
    # Baseline has no focus/centre-head notification. Execute its no-op behavior
    # so a missing handler fails an assertion rather than a compiler error.
    $callback = 'void Application::setTrackingFrameHead(const XrPosef&, bool) {}'
} else {
    $brace = $app.IndexOf('{', $callbackStart)
    $depth = 1
    $callbackEnd = $brace+1
    while($depth -gt 0 -and $callbackEnd -lt $app.Length) {
        if($app[$callbackEnd] -eq '{') { ++$depth }
        if($app[$callbackEnd] -eq '}') { --$depth }
        ++$callbackEnd
    }
    if($depth) { throw 'Unbalanced production head callback' }
    $callback = $app.Substring($callbackStart, $callbackEnd-$callbackStart)
}
$focus = [regex]::Match($app, '(?s)void Application::onSessionFocusLost\(\)\s*\{[^}]*\}')
$focusCallback = if($focus.Success) { $focus.Value } else { 'void Application::onSessionFocusLost() {}' }
$template = Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'test-arxvr-menu-anchor.cpp.in')
$generated = $template.Replace('@ANCHOR@', $anchor).Replace('@HEAD_CALLBACK@', $callback).Replace('@FOCUS_CALLBACK@', $focusCallback)
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$sourcePath = Join-Path $OutputDirectory 'menu-anchor-test.generated.cpp'
[IO.File]::WriteAllText($sourcePath, $generated)
$exe = Join-Path $OutputDirectory 'menu-anchor-test.exe'
$obj = Join-Path $OutputDirectory 'menu-anchor-test.obj'
$vcvars = Join-Path $root 'toolchain\VisualStudio2022\VC\Auxiliary\Build\vcvars64.bat'
$compile = '"{0}" >nul && cl /nologo /EHsc /std:c++17 /I"{1}" "{2}" /Fe:"{3}" /Fo:"{4}"' -f $vcvars, (Join-Path $cpp 'third'), $sourcePath, $exe, $obj
& $env:ComSpec /d /s /c $compile
if($LASTEXITCODE) { throw 'Test compilation failed' }
& $exe
if($LASTEXITCODE) { throw 'Menu anchor regression test failed' }
