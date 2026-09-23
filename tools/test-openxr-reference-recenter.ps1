$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $PSScriptRoot
$programPath = Join-Path $projectRoot 'PICO-OpenXR-Demos\app\src\main\cpp\openxr_program.cpp'
$applicationHeaderPath = Join-Path $projectRoot 'PICO-OpenXR-Demos\app\src\main\cpp\demos\application.h'
$applicationPath = Join-Path $projectRoot 'PICO-OpenXR-Demos\app\src\main\cpp\demos\application.cpp'

$program = Get-Content -LiteralPath $programPath -Raw
$applicationHeader = Get-Content -LiteralPath $applicationHeaderPath -Raw
$application = Get-Content -LiteralPath $applicationPath -Raw

if($program -notmatch 'case\s+XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING\s*:\s*\{') {
    throw 'OpenXR reference-space change still falls through to the ignored/default event branch.'
}

if($program -notmatch 'referenceSpaceChangePending\.referenceSpaceType\s*==\s*m_appSpaceType') {
    throw 'Reference-space change is not restricted to the application tracking space.'
}

if($program -notmatch 'predictedDisplayTime\s*>=\s*m_referenceSpaceChangeTime') {
    throw 'Reference-space recenter is not deferred until the runtime change time.'
}

if($program -notmatch 'm_application->onReferenceSpaceChanged\(\)') {
    throw 'OpenXR program does not notify the application after the tracking origin changes.'
}

if($applicationHeader -notmatch 'virtual\s+void\s+onReferenceSpaceChanged\(\)\s*=\s*0\s*;') {
    throw 'IApplication has no reference-space change callback.'
}

if($application -notmatch '(?s)void\s+Application::onReferenceSpaceChanged\(\).*?mArxEngineRecenter2d\(\)') {
    throw 'Arx application does not rebase its head reference after an OpenXR origin change.'
}

Write-Output 'PASS: OpenXR application-space changes trigger a time-correct ArxVR head rebase.'
