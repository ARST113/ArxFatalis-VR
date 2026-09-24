#!/usr/bin/env python3
"""Guarded Phase 4 live-combat integration for ArxGame.cpp.

This script is intentionally deterministic: it is applied to the known
pre-integration ArxGame.cpp and refuses to continue if any anchor is ambiguous.
It preserves the existing Arx target/damage/script/sound/haptic pipeline and
only replaces physical strike qualification/debounce with VrHandState.
"""
from pathlib import Path
import re

PATH = Path("ArxAndroid/src/core/ArxGame.cpp")
text = PATH.read_bytes().decode("utf-8")
original = text


def replace_once(old: str, new: str, label: str) -> None:
    global text
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one literal match, found {count}")
    text = text.replace(old, new, 1)


def sub_once(pattern: str, replacement: str, label: str) -> None:
    global text
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected one regex match, found {count}")


replace_once(
    "#include <chrono>\n",
    "#include <chrono>\n#include <cstdint>\n",
    "cstdint include",
)
replace_once(
    '#include "vr/VrHaptics.h"\n',
    '#include "vr/VrHaptics.h"\n#include "vr/VrHandState.h"\n',
    "VrHandState include",
)
replace_once(
    "constexpr float kVrFistMinimumSpeed = 85.f;\n",
    "",
    "legacy fist speed constant",
)

sub_once(
    r"struct VrFistState \{.*?static VrHeldObjectCombatState g_vrHeldObjectCombat;\n",
    """static arxvr::VrHandState g_vrRightHandImpact;
static arxvr::VrHandState g_vrLeftHandImpact;

static std::uint64_t vrImpactTimestampUs() {
\treturn static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
\t\tstd::chrono::steady_clock::now().time_since_epoch()).count());
}
""",
    "legacy combat state block",
)

sub_once(
    r"static void updateVrHeldObjectCombat\(bool gripHeld\) \{.*?\n\}\n\nstatic Entity \* findVrFistTarget",
    """static void updateVrHeldObjectCombat(bool rightHand, bool haveHand,
                                     const Vec3f & handPosition, bool gripHeld,
                                     bool allowHit, std::uint64_t timestampUs) {
\tarxvr::VrHandState & state = rightHand ? g_vrRightHandImpact : g_vrLeftHandImpact;
\tEntity * heldObject = getVrPhysicalDragEntity();

\tarxvr::VrImpactSample sample;
\tsample.motion.timestampUs = timestampUs;
\tsample.source = heldObject ? arxvr::VrImpactSource::HeldObject
\t                           : arxvr::VrImpactSource::None;
\tsample.sourceToken = heldObject
\t                   ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heldObject))
\t                   : 0;
\tsample.gestureActive = heldObject && gripHeld && allowHit;
\tsample.trackingValid = haveHand;
\tif(haveHand) {
\t\tsample.motion.x = handPosition.x;
\t\tsample.motion.y = handPosition.y;
\t\tsample.motion.z = handPosition.z;
\t}
\tconst arxvr::VrImpactGateStatus status = state.update(sample);
\tif(status != arxvr::VrImpactGateStatus::Qualified || !heldObject
\t   || !gripHeld || !allowHit || !haveHand) {
\t\treturn;
\t}

\tconst Vec3f velocity = getVrPhysicalDragVelocity();
\tconst float objectSpeed = glm::length(velocity);
\tEERIE_3D_BBOX sweptBounds = heldObject->bbox3D;
\tif(sweptBounds.valid()) {
\t\tconst float frameSeconds = std::max(1.f, toMsf(g_platformTime.lastFrameDuration()))
\t\t                         * 0.001f;
\t\tconst Vec3f frameMovement = velocity * frameSeconds;
\t\tsweptBounds.add(heldObject->bbox3D.min + frameMovement);
\t\tsweptBounds.add(heldObject->bbox3D.max + frameMovement);
\t}
\tEntity * target = findVrHeldObjectTarget(*heldObject, sweptBounds);
\tif(!target) {
\t\treturn;
\t}

\tarxvr::VrQualifiedImpact impact;
\tif(!state.consumeQualifiedImpact(impact)) {
\t\treturn;
\t}
\tconst float impactSpeed = std::max(objectSpeed, impact.metrics.terminalSpeed);
\tconst Vec3f objectSize = heldObject->bbox3D.valid()
\t                       ? heldObject->bbox3D.max - heldObject->bbox3D.min
\t                       : Vec3f(30.f);
\tconst float sizeBonus = glm::clamp(glm::length(objectSize) * 0.015f, 0.f, 5.f);
\tconst float impactDamage = glm::clamp(2.f + impactSpeed * 0.03f + sizeBonus, 3.f, 18.f);
\tconst Vec3f objectCenter = sweptBounds.valid()
\t                         ? (sweptBounds.min + sweptBounds.max) * 0.5f
\t                         : heldObject->pos;
\tVec3f hitPosition = target->bbox3D.valid()
\t                  ? glm::clamp(objectCenter, target->bbox3D.min, target->bbox3D.max)
\t                  : target->pos;
\tSendIOScriptEvent(entities.player(), target, SM_AGGRESSION);
\tconst float damage = damageNpc(*target, impactDamage, entities.player(), nullptr,
\t                               DAMAGE_TYPE_GENERIC, &hitPosition);
\tconst std::string_view impactMaterial = !heldObject->weaponmaterial.empty()
\t                                      ? std::string_view(heldObject->weaponmaterial)
\t                                      : std::string_view("wood");
\tARX_SOUND_PlayCollision("flesh", impactMaterial, 1.f, 1.f,
\t                        hitPosition, entities.player());
\tarxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
\t                VrHapticEvent::ImpactHeavy,
\t                glm::clamp(impactSpeed / 300.f, 0.45f, 1.f));
\t++g_vrHeldObjectHitCount;
\tARX_PLAYER_Remove_Invisibility();
\tLogInfo << "ArxVR held-object hit: weapon=" << heldObject->idString()
\t        << " target=" << target->idString() << " speed=" << impactSpeed
\t        << " peak=" << impact.metrics.peakSpeed
\t        << " path=" << impact.metrics.pathLength
\t        << " consistency=" << impact.metrics.directionalConsistency
\t        << " damage=" << damage << " life=" << target->_npcdata->lifePool.current;
}

static Entity * findVrFistTarget""",
    "held-object combat function",
)

sub_once(
    r"static void updateVrFistCombat\(bool rightHand, bool haveHand,.*?\n\}\n\nstatic void updateVrPhysicalInteraction",
    """static void updateVrFistCombat(bool rightHand, bool haveHand,
                            const Vec3f & handPosition, bool fistClosed,
                            bool allowHit, std::uint64_t timestampUs) {
\tarxvr::VrHandState & state = rightHand ? g_vrRightHandImpact : g_vrLeftHandImpact;
\tarxvr::VrImpactSample sample;
\tsample.motion.timestampUs = timestampUs;
\tsample.source = arxvr::VrImpactSource::Fist;
\tsample.sourceToken = 0;
\tsample.gestureActive = haveHand && allowHit && fistClosed;
\tsample.trackingValid = haveHand;
\tif(haveHand) {
\t\tsample.motion.x = handPosition.x;
\t\tsample.motion.y = handPosition.y;
\t\tsample.motion.z = handPosition.z;
\t}
\tconst arxvr::VrImpactGateStatus status = state.update(sample);
\tif(status != arxvr::VrImpactGateStatus::Qualified
\t   || !haveHand || !allowHit || !fistClosed) {
\t\treturn;
\t}

\tEntity * target = findVrFistTarget(handPosition);
\tif(!target) {
\t\treturn;
\t}

\tarxvr::VrQualifiedImpact impact;
\tif(!state.consumeQualifiedImpact(impact)) {
\t\treturn;
\t}
\tconst float speed = impact.metrics.terminalSpeed;
\tconst float strength = glm::clamp((speed - 55.f) / 170.f, 0.35f, 1.f);
\tVec3f hitPosition = handPosition;
\tfloat damage = ARX_EQUIPMENT_ComputeDamages(entities.player(), target,
\t                                           strength, &hitPosition);
\t// A physical contact should not turn into a silent dice-roll miss. Keep the
\t// normal armor/critical path above, but guarantee a small bare-hand impact.
\tif(damage <= 0.f && target->_npcdata->lifePool.current > 0.f) {
\t\tconst float minimumDamage = std::max(1.f, player.m_miscFull.damages * 0.25f);
\t\tdamage = damageNpc(*target, minimumDamage, entities.player(), nullptr,
\t\t                   DAMAGE_TYPE_GENERIC, &hitPosition);
\t}
\tarxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
\t                VrHapticEvent::ImpactLight,
\t                glm::clamp(speed / 240.f, 0.35f, 1.f));
\tARX_PLAYER_Remove_Invisibility();
\tLogInfo << "ArxVR fist hit: hand=" << (rightHand ? "right" : "left")
\t        << " target=" << target->idString() << " speed=" << speed
\t        << " peak=" << impact.metrics.peakSpeed
\t        << " path=" << impact.metrics.pathLength
\t        << " consistency=" << impact.metrics.directionalConsistency
\t        << " strength=" << strength << " damage=" << damage
\t        << " life=" << target->_npcdata->lifePool.current;
}

static void updateVrPhysicalInteraction""",
    "fist combat function",
)

replace_once(
    "\tif(!g_haveVrCenterCamera || ARXmenu.mode() != Mode_InGame) {\n"
    "\t\tg_vrInteractionTarget = EntityHandle();\n"
    "\t\tarxvrSetDirectInteractionTriggerCaptured(false);\n"
    "\t\treturn;\n"
    "\t}\n",
    "\tif(!g_haveVrCenterCamera || ARXmenu.mode() != Mode_InGame) {\n"
    "\t\tg_vrRightHandImpact.resetSession();\n"
    "\t\tg_vrLeftHandImpact.resetSession();\n"
    "\t\tg_vrInteractionTarget = EntityHandle();\n"
    "\t\tarxvrSetDirectInteractionTriggerCaptured(false);\n"
    "\t\treturn;\n"
    "\t}\n",
    "session reset outside gameplay",
)

sub_once(
    r"\tVec3f rightHandPosition;.*?\n\tEntity \* rightTarget",
    """\tVec3f rightHandPosition(0.f);
\tVec3f rightHandDirection(0.f);
\tVec3f leftHandPosition(0.f);
\tVec3f leftHandDirection(0.f);
\tglm::quat rightHandOrientation(1.f, 0.f, 0.f, 0.f);
\tglm::quat leftHandOrientation(1.f, 0.f, 0.f, 0.f);
\tconst bool haveRightHand = arxvrGetHandWorldPose(true, g_vrCenterCamera,
\t                                                player.angle.getYaw(),
\t                                                rightHandPosition,
\t                                                rightHandDirection,
\t                                                rightHandOrientation);
\tconst bool haveLeftHand = arxvrGetHandWorldPose(false, g_vrCenterCamera,
\t                                               player.angle.getYaw(),
\t                                               leftHandPosition,
\t                                               leftHandDirection,
\t                                               leftHandOrientation);
\tconst std::uint64_t impactTimestampUs = vrImpactTimestampUs();
\tconst bool physicalDragActive = isVrPhysicalDragActive();
\tconst bool rightDragging = physicalDragActive && g_vrPhysicalDragUsesRightHand;
\tconst bool leftDragging = physicalDragActive && !g_vrPhysicalDragUsesRightHand;

\t// Each physical hand owns exactly one impact state. The free hand keeps
\t// collecting fist motion while the other hand holds an object; the drag
\t// hand is updated only as HeldObject so source history is not reset twice
\t// in the same frame.
\tif(!rightDragging) {
\t\tupdateVrFistCombat(true, haveRightHand, rightHandPosition,
\t\t                  arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE),
\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t}
\tif(!leftDragging) {
\t\tupdateVrFistCombat(false, haveLeftHand, leftHandPosition,
\t\t                  arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),
\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t}

\tif(physicalDragActive) {
\t\tconst bool haveDragHand = g_vrPhysicalDragUsesRightHand ? haveRightHand : haveLeftHand;
\t\tconst Vec3f & dragPosition = g_vrPhysicalDragUsesRightHand
\t\t                           ? rightHandPosition : leftHandPosition;
\t\tconst Vec3f & dragDirection = g_vrPhysicalDragUsesRightHand
\t\t                            ? rightHandDirection : leftHandDirection;
\t\tconst glm::quat & dragOrientation = g_vrPhysicalDragUsesRightHand
\t\t                                  ? rightHandOrientation : leftHandOrientation;
\t\tconst std::uint32_t gripButton = g_vrPhysicalDragUsesRightHand
\t\t                               ? ARXVR_BUTTON_RIGHT_SQUEEZE
\t\t                               : ARXVR_BUTTON_LEFT_SQUEEZE;
\t\tconst bool gripHeld = arxvrButtonPressed(gripButton);
\t\t// A temporarily lost controller pose must not feed uninitialised
\t\t// coordinates into the held object. Freeze it for that frame, while
\t\t// explicitly failing the impact gate closed until tracking recovers.
\t\tif(haveDragHand) {
\t\t\tupdateVrPhysicalDragPose(dragPosition, dragDirection, dragOrientation,
\t\t\t                         gripHeld);
\t\t}
\t\tupdateVrHeldObjectCombat(g_vrPhysicalDragUsesRightHand, haveDragHand,
\t\t                         dragPosition, gripHeld, !BLOCK_PLAYER_CONTROLS,
\t\t                         impactTimestampUs);
\t\treturn;
\t}

\tif(!haveRightHand && !haveLeftHand) {
\t\tg_vrInteractionTarget = EntityHandle();
\t\treturn;
\t}

\tEntity * rightTarget""",
    "physical interaction hand routing",
)

for legacy in (
    "VrFistState",
    "VrHeldObjectCombatState",
    "g_vrRightFist",
    "g_vrLeftFist",
    "g_vrHeldObjectCombat",
    "kVrFistMinimumSpeed",
):
    if legacy in text:
        raise RuntimeError(f"legacy combat token still present after integration: {legacy}")

for required in (
    '#include "vr/VrHandState.h"',
    "g_vrRightHandImpact",
    "consumeQualifiedImpact",
    "VrImpactGateStatus::Qualified",
    "updateVrHeldObjectCombat(g_vrPhysicalDragUsesRightHand",
):
    if required not in text:
        raise RuntimeError(f"required integration token missing: {required}")

for forbidden in (
    'static arxvr::VrHandState g_vrRightHandImpact;\\n"',
    '"\\n    "\\t',
    'static void updateVrFistCombat(bool rightHand, bool haveHand,\\n"',
):
    if forbidden in text:
        raise RuntimeError(f"serialized Python string fragment detected: {forbidden!r}")

if text == original:
    raise RuntimeError("patch produced no changes")

PATH.write_bytes(text.encode("utf-8"))
print("Applied guarded Phase 4 live combat integration to", PATH)
