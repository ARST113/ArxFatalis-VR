#!/usr/bin/env python3
"""Apply the guarded Phase 7 physical-rune runtime adapter to ArxGame.cpp.

The integration helper reconstructs ArxGame.cpp from the known pre-VR base and
runs this generator after the Phase 4/5 generators. Keep this patch narrowly
anchored so failures are explicit when upstream/live integration layout changes.
"""

from pathlib import Path

GAME = Path("ArxAndroid/src/core/ArxGame.cpp")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    text = GAME.read_text(encoding="utf-8")

    if 'g_vrRuneRuntime.update(' in text:
        print("Phase 7 physical rune runtime is already integrated")
        return

    text = replace_once(
        text,
        '#include "vr/VrInteractionSystem.h"\n#include "vr/VrWeaponContact.h"',
        '#include "vr/VrInteractionSystem.h"\n'
        '#include "vr/VrRuneRuntime.h"\n'
        '#include "game/magic/SpellRecognition.h"\n'
        '#include "vr/VrWeaponContact.h"',
        "VR rune runtime includes",
    )

    text = replace_once(
        text,
        'static Camera g_vrCenterCamera;\nstatic bool g_haveVrCenterCamera = false;\n',
        '''static Camera g_vrCenterCamera;
static bool g_haveVrCenterCamera = false;
static arxvr::VrRuneRuntime g_vrRuneRuntime;
static bool g_vrRuneFeedbackPending = false;
static std::array<Rune, MAX_SPELL_SYMBOLS> g_vrRuneSymbolsBefore{};
static size_t g_vrRuneFeedbackPointCount = 0;
static float g_vrRuneFeedbackPathLength = 0.f;
static std::uint64_t g_vrRuneFeedbackDurationUs = 0;

static arxvr::VrRuneVector3 vrRuneVector(const Vec3f & value) {
\treturn { value.x, value.y, value.z };
}

static arxvr::VrRunePlane vrRuneDrawingPlane() {
\tarxvr::VrRunePlane plane;
\tif(!g_haveVrCenterCamera) {
\t\treturn plane;
\t}

\tVec3f forward = angleToVector(g_vrCenterCamera.angle);
\tif(glm::length(forward) <= 0.001f) {
\t\treturn plane;
\t}
\tforward = glm::normalize(forward);
\t// Arx world +Y points down. Using world-up and a normal facing the player
\t// makes cross(up, normal) point toward physical controller-right at the
\t// neutral camera pose. VrRuneSystem locks this basis on paint-down.
\tconst Vec3f worldUp(0.f, -1.f, 0.f);
\tplane.origin = vrRuneVector(g_vrCenterCamera.m_pos + forward * 70.f);
\tplane.normal = vrRuneVector(-forward);
\tplane.up = vrRuneVector(worldUp);
\tplane.valid = true;
\treturn plane;
}

static std::uint64_t vrRuneTimestampUs() {
\tconst auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
\tconst auto micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
\treturn micros > 0 ? static_cast<std::uint64_t>(micros) : 1u;
}

static bool vrRuneScreenPoint(const arxvr::VrRunePoint2 & point, Vec2s & screenPoint) {
\tconst arxvr::VrRuneViewportPoint mapped = g_vrRuneRuntime.mapToViewport(
\t\tpoint, g_size.width(), g_size.height());
\tif(!mapped.valid) {
\t\treturn false;
\t}
\tscreenPoint = Vec2s(Vec2f(mapped.x, mapped.y));
\treturn true;
}
''',
        "VR rune runtime globals",
    )

    text = replace_once(
        text,
        '\tARX_MAGICAL_FLARES_FirstInit();\n',
        '''\tARX_MAGICAL_FLARES_FirstInit();
#if defined(ARXVR_ANDROID_BUILD)
\tg_vrRuneRuntime.reset();
\tg_vrRuneFeedbackPending = false;
\tg_vrRuneFeedbackPointCount = 0;
\tg_vrRuneFeedbackPathLength = 0.f;
\tg_vrRuneFeedbackDurationUs = 0;
#endif
''',
        "VR rune runtime session reset",
    )

    old_rune_block = '''\t// Checks Magic Flares Drawing
\tif(!player.m_paralysed) {
\t\tbool runeDrawPressed = eeMousePressed1();
#if defined(ARXVR_ANDROID_BUILD)
\t\tVec2s vrRunePoint;
\t\tif(arxvrGetRuneScreenPoint(g_size, vrRunePoint)) {
\t\t\tDANAEMouse = vrRunePoint;
\t\t\truneDrawPressed = true;
\t\t}
#endif
\t\tif(runeDrawPressed) {
\t\t\tif(!ARX_FLARES_Block) {
\t\t\t\tstatic PlatformDuration runeDrawPointElapsed = 0;
\t\t\t\tif(!config.input.useAltRuneRecognition) {
\t\t\t\t\truneDrawPointElapsed += g_platformTime.lastFrameDuration();
\t\t\t\t\t
\t\t\t\t\tif(runeDrawPointElapsed >= runeDrawPointInterval) {
\t\t\t\t\t\tARX_SPELLS_AddPoint(DANAEMouse);
\t\t\t\t\t\twhile(runeDrawPointElapsed >= runeDrawPointInterval) {
\t\t\t\t\t\t\truneDrawPointElapsed -= runeDrawPointInterval;
\t\t\t\t\t\t}
\t\t\t\t\t}
\t\t\t\t} else {
\t\t\t\t\tARX_SPELLS_AddPoint(DANAEMouse);
\t\t\t\t}
\t\t\t} else {
\t\t\t\tspellRecognitionPointsReset();
\t\t\t\tARX_FLARES_Block = false;
\t\t\t}
\t\t} else if(!ARX_FLARES_Block) {
\t\t\tARX_FLARES_Block = true;
\t\t}
\t}

\tARX_SPELLS_Precast_Check();
\t
\tif(ARXmenu.mode() == Mode_InGame) {
\t\tARX_SPELLS_ManageMagic();
\t}
'''

    new_rune_block = '''\t// Checks Magic Flares Drawing
\tif(!player.m_paralysed) {
\t\tbool runeDrawPressed = eeMousePressed1();
#if defined(ARXVR_ANDROID_BUILD)
\t\tVec3f vrRuneHandPosition(0.f);
\t\tVec3f vrRuneHandDirection(0.f);
\t\tglm::quat vrRuneHandOrientation(1.f, 0.f, 0.f, 0.f);
\t\tconst bool vrRuneTracking = g_haveVrCenterCamera
\t\t                         && arxvrGetRightHandWorldPose(
\t\t\t                         g_vrCenterCamera, player.angle.getYaw(),
\t\t\t                         vrRuneHandPosition, vrRuneHandDirection,
\t\t\t                         vrRuneHandOrientation);
\t\t(void)vrRuneHandDirection;
\t\t(void)vrRuneHandOrientation;

\t\tarxvr::VrRuneSample vrRuneSample;
\t\tvrRuneSample.timestampUs = vrRuneTimestampUs();
\t\tvrRuneSample.handPosition = vrRuneVector(vrRuneHandPosition);
\t\tvrRuneSample.trackingValid = vrRuneTracking;
\t\tvrRuneSample.paintPressed = arxvrIsRuneDrawing();
\t\tconst arxvr::VrRuneRuntimeResult vrRune = g_vrRuneRuntime.update(
\t\t\tvrRuneSample, vrRuneDrawingPlane());

\t\t// The legacy flare renderer still consumes DANAEMouse, but the point now
\t\t// comes from the paint-down-locked physical plane rather than an HMD-
\t\t// relative projection. Repeated filtered points simply leave it stable.
\t\tif(vrRune.livePointValid) {
\t\t\tVec2s vrRunePoint;
\t\t\tif(vrRuneScreenPoint(vrRune.livePoint, vrRunePoint)) {
\t\t\t\tDANAEMouse = vrRunePoint;
\t\t\t}
\t\t}
\t\truneDrawPressed = runeDrawPressed
\t\t               || vrRune.status == arxvr::VrRuneStatus::Capturing;

\t\tif(vrRune.strokeEnded) {
\t\t\t// Discard all frame-rate-dependent intermediate points. A qualified
\t\t\t// physical gesture is replayed from VrRuneSystem's filtered path so the
\t\t\t// existing Arx recognizer, spell prerequisites and scripts stay intact.
\t\t\tspellRecognitionPointsReset();
\t\t\tif(vrRune.gestureReady) {
\t\t\t\tfor(const arxvr::VrRunePoint2 & point : vrRune.gesture.points) {
\t\t\t\t\tVec2s mapped;
\t\t\t\t\tif(vrRuneScreenPoint(point, mapped)) {
\t\t\t\t\t\tARX_SPELLS_AddPoint(mapped);
\t\t\t\t\t}
\t\t\t\t}
\t\t\t}

\t\t\tif(!vrRune.cancelled) {
\t\t\t\tg_vrRuneSymbolsBefore = SpellSymbol;
\t\t\t\tg_vrRuneFeedbackPending = true;
\t\t\t\tg_vrRuneFeedbackPointCount = vrRune.gestureReady
\t\t\t\t                               ? vrRune.gesture.points.size() : 0;
\t\t\t\tg_vrRuneFeedbackPathLength = vrRune.gestureReady
\t\t\t\t                               ? vrRune.gesture.pathLength : 0.f;
\t\t\t\tg_vrRuneFeedbackDurationUs = vrRune.gestureReady
\t\t\t\t                              ? vrRune.gesture.durationUs : 0;
\t\t\t}
\t\t}
#endif
\t\tif(runeDrawPressed) {
\t\t\tif(!ARX_FLARES_Block) {
\t\t\t\tstatic PlatformDuration runeDrawPointElapsed = 0;
\t\t\t\tif(!config.input.useAltRuneRecognition) {
\t\t\t\t\truneDrawPointElapsed += g_platformTime.lastFrameDuration();
\t\t\t\t\t
\t\t\t\t\tif(runeDrawPointElapsed >= runeDrawPointInterval) {
\t\t\t\t\t\tARX_SPELLS_AddPoint(DANAEMouse);
\t\t\t\t\t\twhile(runeDrawPointElapsed >= runeDrawPointInterval) {
\t\t\t\t\t\t\truneDrawPointElapsed -= runeDrawPointInterval;
\t\t\t\t\t\t}
\t\t\t\t\t}
\t\t\t\t} else {
\t\t\t\t\tARX_SPELLS_AddPoint(DANAEMouse);
\t\t\t\t}
\t\t\t} else {
\t\t\t\tspellRecognitionPointsReset();
\t\t\t\tARX_FLARES_Block = false;
\t\t\t}
\t\t} else if(!ARX_FLARES_Block) {
\t\t\tARX_FLARES_Block = true;
\t\t}
\t}

\tARX_SPELLS_Precast_Check();
\t
\tif(ARXmenu.mode() == Mode_InGame) {
\t\tARX_SPELLS_ManageMagic();
#if defined(ARXVR_ANDROID_BUILD)
\t\tif(g_vrRuneFeedbackPending) {
\t\t\tconst bool accepted = SpellSymbol != g_vrRuneSymbolsBefore;
\t\t\tarxvrEmitHaptic(VrHapticHand::Right,
\t\t\t                 accepted ? VrHapticEvent::RuneAccepted
\t\t\t                          : VrHapticEvent::RuneRejected,
\t\t\t                 accepted ? 0.75f : 0.45f);
\t\t\tLogInfo << "ArxVR rune: accepted=" << accepted
\t\t\t        << " points=" << g_vrRuneFeedbackPointCount
\t\t\t        << " path=" << g_vrRuneFeedbackPathLength
\t\t\t        << " durationUs=" << g_vrRuneFeedbackDurationUs;
\t\t\tg_vrRuneFeedbackPending = false;
\t\t}
#endif
\t}
'''

    text = replace_once(text, old_rune_block, new_rune_block, "physical rune live adapter")

    required = [
        '#include "vr/VrRuneRuntime.h"',
        'static arxvr::VrRuneRuntime g_vrRuneRuntime;',
        'g_vrRuneRuntime.update(',
        'vrRuneDrawingPlane()',
        'vrRune.gesture.points',
        'SpellSymbol != g_vrRuneSymbolsBefore',
        'VrHapticEvent::RuneAccepted',
        'VrHapticEvent::RuneRejected',
    ]
    for marker in required:
        if marker not in text:
            raise SystemExit(f"generated ArxGame.cpp missing marker: {marker}")
    if 'arxvrGetRuneScreenPoint(g_size, vrRunePoint)' in text:
        raise SystemExit("legacy HMD-relative rune projection remains in live ArxGame path")

    GAME.write_text(text, encoding="utf-8", newline="\n")
    print("Applied guarded Phase 7 physical rune runtime integration")


if __name__ == "__main__":
    main()
