#!/usr/bin/env python3
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
    """static arxvr::VrHandState g_vrRightHandImpact;\n"
    "static arxvr::VrHandState g_vrLeftHandImpact;\n\n"
    "static std::uint64_t vrImpactTimestampUs() {\n"
    "\treturn static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(\n"
    "\t\tstd::chrono::steady_clock::now().time_since_epoch()).count());\n"
    "}\n"
    """,
    "legacy combat state block",
)

sub_once(
    r"static void updateVrHeldObjectCombat\(bool gripHeld\) \{.*?\n\}\n\nstatic Entity \* findVrFistTarget",
    """static void updateVrHeldObjectCombat(bool rightHand, bool haveHand,\n"
    "                                     const Vec3f & handPosition, bool gripHeld,\n"
    "                                     bool allowHit, std::uint64_t timestampUs) {\n"
    "\tarxvr::VrHandState & state = rightHand ? g_vrRightHandImpact : g_vrLeftHandImpact;\n"
    "\tEntity * heldObject = getVrPhysicalDragEntity();\n\n"
    "\tarxvr::VrImpactSample sample;\n"
    "\tsample.motion.timestampUs = timestampUs;\n"
    "\tsample.source = heldObject ? arxvr::VrImpactSource::HeldObject\n"
    "\t                           : arxvr::VrImpactSource::None;\n"
    "\tsample.sourceToken = heldObject\n"
    "\t                   ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(heldObject))\n"
    "\t                   : 0;\n"
    "\tsample.gestureActive = heldObject && gripHeld && allowHit;\n"
    "\tsample.trackingValid = haveHand;\n"
    "\tif(haveHand) {\n"
    "\t\tsample.motion.x = handPosition.x;\n"
    "\t\tsample.motion.y = handPosition.y;\n"
    "\t\tsample.motion.z = handPosition.z;\n"
    "\t}\n"
    "\tconst arxvr::VrImpactGateStatus status = state.update(sample);\n"
    "\tif(status != arxvr::VrImpactGateStatus::Qualified || !heldObject\n"
    "\t   || !gripHeld || !allowHit || !haveHand) {\n"
    "\t\treturn;\n"
    "\t}\n\n"
    "\tconst Vec3f velocity = getVrPhysicalDragVelocity();\n"
    "\tconst float objectSpeed = glm::length(velocity);\n"
    "\tEERIE_3D_BBOX sweptBounds = heldObject->bbox3D;\n"
    "\tif(sweptBounds.valid()) {\n"
    "\t\tconst float frameSeconds = std::max(1.f, toMsf(g_platformTime.lastFrameDuration()))\n"
    "\t\t                         * 0.001f;\n"
    "\t\tconst Vec3f frameMovement = velocity * frameSeconds;\n"
    "\t\tsweptBounds.add(heldObject->bbox3D.min + frameMovement);\n"
    "\t\tsweptBounds.add(heldObject->bbox3D.max + frameMovement);\n"
    "\t}\n"
    "\tEntity * target = findVrHeldObjectTarget(*heldObject, sweptBounds);\n"
    "\tif(!target) {\n"
    "\t\treturn;\n"
    "\t}\n\n"
    "\tarxvr::VrQualifiedImpact impact;\n"
    "\tif(!state.consumeQualifiedImpact(impact)) {\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tconst float impactSpeed = std::max(objectSpeed, impact.metrics.terminalSpeed);\n"
    "\tconst Vec3f objectSize = heldObject->bbox3D.valid()\n"
    "\t                       ? heldObject->bbox3D.max - heldObject->bbox3D.min\n"
    "\t                       : Vec3f(30.f);\n"
    "\tconst float sizeBonus = glm::clamp(glm::length(objectSize) * 0.015f, 0.f, 5.f);\n"
    "\tconst float impactDamage = glm::clamp(2.f + impactSpeed * 0.03f + sizeBonus, 3.f, 18.f);\n"
    "\tconst Vec3f objectCenter = sweptBounds.valid()\n"
    "\t                         ? (sweptBounds.min + sweptBounds.max) * 0.5f\n"
    "\t                         : heldObject->pos;\n"
    "\tVec3f hitPosition = target->bbox3D.valid()\n"
    "\t                  ? glm::clamp(objectCenter, target->bbox3D.min, target->bbox3D.max)\n"
    "\t                  : target->pos;\n"
    "\tSendIOScriptEvent(entities.player(), target, SM_AGGRESSION);\n"
    "\tconst float damage = damageNpc(*target, impactDamage, entities.player(), nullptr,\n"
    "\t                               DAMAGE_TYPE_GENERIC, &hitPosition);\n"
    "\tconst std::string_view impactMaterial = !heldObject->weaponmaterial.empty()\n"
    "\t                                      ? std::string_view(heldObject->weaponmaterial)\n"
    "\t                                      : std::string_view(\"wood\");\n"
    "\tARX_SOUND_PlayCollision(\"flesh\", impactMaterial, 1.f, 1.f,\n"
    "\t                        hitPosition, entities.player());\n"
    "\tarxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,\n"
    "\t                VrHapticEvent::ImpactHeavy,\n"
    "\t                glm::clamp(impactSpeed / 300.f, 0.45f, 1.f));\n"
    "\t++g_vrHeldObjectHitCount;\n"
    "\tARX_PLAYER_Remove_Invisibility();\n"
    "\tLogInfo << \"ArxVR held-object hit: weapon=\" << heldObject->idString()\n"
    "\t        << \" target=\" << target->idString() << \" speed=\" << impactSpeed\n"
    "\t        << \" peak=\" << impact.metrics.peakSpeed\n"
    "\t        << \" path=\" << impact.metrics.pathLength\n"
    "\t        << \" consistency=\" << impact.metrics.directionalConsistency\n"
    "\t        << \" damage=\" << damage << \" life=\" << target->_npcdata->lifePool.current;\n"
    "}\n\n"
    "static Entity * findVrFistTarget""",
    "held-object combat function",
)

sub_once(
    r"static void updateVrFistCombat\(bool rightHand, bool haveHand,.*?\n\}\n\nstatic void updateVrPhysicalInteraction",
    """static void updateVrFistCombat(bool rightHand, bool haveHand,\n"
    "                            const Vec3f & handPosition, bool fistClosed,\n"
    "                            bool allowHit, std::uint64_t timestampUs) {\n"
    "\tarxvr::VrHandState & state = rightHand ? g_vrRightHandImpact : g_vrLeftHandImpact;\n"
    "\tarxvr::VrImpactSample sample;\n"
    "\tsample.motion.timestampUs = timestampUs;\n"
    "\tsample.source = arxvr::VrImpactSource::Fist;\n"
    "\tsample.sourceToken = 0;\n"
    "\tsample.gestureActive = haveHand && allowHit && fistClosed;\n"
    "\tsample.trackingValid = haveHand;\n"
    "\tif(haveHand) {\n"
    "\t\tsample.motion.x = handPosition.x;\n"
    "\t\tsample.motion.y = handPosition.y;\n"
    "\t\tsample.motion.z = handPosition.z;\n"
    "\t}\n"
    "\tconst arxvr::VrImpactGateStatus status = state.update(sample);\n"
    "\tif(status != arxvr::VrImpactGateStatus::Qualified\n"
    "\t   || !haveHand || !allowHit || !fistClosed) {\n"
    "\t\treturn;\n"
    "\t}\n\n"
    "\tEntity * target = findVrFistTarget(handPosition);\n"
    "\tif(!target) {\n"
    "\t\treturn;\n"
    "\t}\n\n"
    "\tarxvr::VrQualifiedImpact impact;\n"
    "\tif(!state.consumeQualifiedImpact(impact)) {\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tconst float speed = impact.metrics.terminalSpeed;\n"
    "\tconst float strength = glm::clamp((speed - 55.f) / 170.f, 0.35f, 1.f);\n"
    "\tVec3f hitPosition = handPosition;\n"
    "\tfloat damage = ARX_EQUIPMENT_ComputeDamages(entities.player(), target,\n"
    "\t                                           strength, &hitPosition);\n"
    "\t// A physical contact should not turn into a silent dice-roll miss. Keep the\n"
    "\t// normal armor/critical path above, but guarantee a small bare-hand impact.\n"
    "\tif(damage <= 0.f && target->_npcdata->lifePool.current > 0.f) {\n"
    "\t\tconst float minimumDamage = std::max(1.f, player.m_miscFull.damages * 0.25f);\n"
    "\t\tdamage = damageNpc(*target, minimumDamage, entities.player(), nullptr,\n"
    "\t\t                   DAMAGE_TYPE_GENERIC, &hitPosition);\n"
    "\t}\n"
    "\tarxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,\n"
    "\t                VrHapticEvent::ImpactLight,\n"
    "\t                glm::clamp(speed / 240.f, 0.35f, 1.f));\n"
    "\tARX_PLAYER_Remove_Invisibility();\n"
    "\tLogInfo << \"ArxVR fist hit: hand=\" << (rightHand ? \"right\" : \"left\")\n"
    "\t        << \" target=\" << target->idString() << \" speed=\" << speed\n"
    "\t        << \" peak=\" << impact.metrics.peakSpeed\n"
    "\t        << \" path=\" << impact.metrics.pathLength\n"
    "\t        << \" consistency=\" << impact.metrics.directionalConsistency\n"
    "\t        << \" strength=\" << strength << \" damage=\" << damage\n"
    "\t        << \" life=\" << target->_npcdata->lifePool.current;\n"
    "}\n\n"
    "static void updateVrPhysicalInteraction""",
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
    """\tVec3f rightHandPosition(0.f);\n"
    "\tVec3f rightHandDirection(0.f);\n"
    "\tVec3f leftHandPosition(0.f);\n"
    "\tVec3f leftHandDirection(0.f);\n"
    "\tglm::quat rightHandOrientation(1.f, 0.f, 0.f, 0.f);\n"
    "\tglm::quat leftHandOrientation(1.f, 0.f, 0.f, 0.f);\n"
    "\tconst bool haveRightHand = arxvrGetHandWorldPose(true, g_vrCenterCamera,\n"
    "\t                                                player.angle.getYaw(),\n"
    "\t                                                rightHandPosition,\n"
    "\t                                                rightHandDirection,\n"
    "\t                                                rightHandOrientation);\n"
    "\tconst bool haveLeftHand = arxvrGetHandWorldPose(false, g_vrCenterCamera,\n"
    "\t                                               player.angle.getYaw(),\n"
    "\t                                               leftHandPosition,\n"
    "\t                                               leftHandDirection,\n"
    "\t                                               leftHandOrientation);\n"
    "\tconst std::uint64_t impactTimestampUs = vrImpactTimestampUs();\n"
    "\tconst bool physicalDragActive = isVrPhysicalDragActive();\n"
    "\tconst bool rightDragging = physicalDragActive && g_vrPhysicalDragUsesRightHand;\n"
    "\tconst bool leftDragging = physicalDragActive && !g_vrPhysicalDragUsesRightHand;\n\n"
    "\t// Each physical hand owns exactly one impact state. The free hand keeps\n"
    "\t// collecting fist motion while the other hand holds an object; the drag\n"
    "\t// hand is updated only as HeldObject so source history is not reset twice\n"
    "\t// in the same frame.\n"
    "\tif(!rightDragging) {\n"
    "\t\tupdateVrFistCombat(true, haveRightHand, rightHandPosition,\n"
    "\t\t                  arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE),\n"
    "\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);\n"
    "\t}\n"
    "\tif(!leftDragging) {\n"
    "\t\tupdateVrFistCombat(false, haveLeftHand, leftHandPosition,\n"
    "\t\t                  arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),\n"
    "\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);\n"
    "\t}\n\n"
    "\tif(physicalDragActive) {\n"
    "\t\tconst bool haveDragHand = g_vrPhysicalDragUsesRightHand ? haveRightHand : haveLeftHand;\n"
    "\t\tconst Vec3f & dragPosition = g_vrPhysicalDragUsesRightHand\n"
    "\t\t                           ? rightHandPosition : leftHandPosition;\n"
    "\t\tconst Vec3f & dragDirection = g_vrPhysicalDragUsesRightHand\n"
    "\t\t                            ? rightHandDirection : leftHandDirection;\n"
    "\t\tconst glm::quat & dragOrientation = g_vrPhysicalDragUsesRightHand\n"
    "\t\t                                  ? rightHandOrientation : leftHandOrientation;\n"
    "\t\tconst std::uint32_t gripButton = g_vrPhysicalDragUsesRightHand\n"
    "\t\t                               ? ARXVR_BUTTON_RIGHT_SQUEEZE\n"
    "\t\t                               : ARXVR_BUTTON_LEFT_SQUEEZE;\n"
    "\t\tconst bool gripHeld = arxvrButtonPressed(gripButton);\n"
    "\t\t// A temporarily lost controller pose must not feed uninitialised\n"
    "\t\t// coordinates into the held object. Freeze it for that frame, while\n"
    "\t\t// explicitly failing the impact gate closed until tracking recovers.\n"
    "\t\tif(haveDragHand) {\n"
    "\t\t\tupdateVrPhysicalDragPose(dragPosition, dragDirection, dragOrientation,\n"
    "\t\t\t                         gripHeld);\n"
    "\t\t}\n"
    "\t\tupdateVrHeldObjectCombat(g_vrPhysicalDragUsesRightHand, haveDragHand,\n"
    "\t\t                         dragPosition, gripHeld, !BLOCK_PLAYER_CONTROLS,\n"
    "\t\t                         impactTimestampUs);\n"
    "\t\treturn;\n"
    "\t}\n\n"
    "\tif(!haveRightHand && !haveLeftHand) {\n"
    "\t\tg_vrInteractionTarget = EntityHandle();\n"
    "\t\treturn;\n"
    "\t}\n\n"
    "\tEntity * rightTarget""",
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

if text == original:
    raise RuntimeError("patch produced no changes")

PATH.write_bytes(text.encode("utf-8"))
print("Applied guarded Phase 4 live combat integration to", PATH)
