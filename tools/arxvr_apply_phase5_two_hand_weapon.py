#!/usr/bin/env python3
"""Wire the Phase-5 VrWeaponSystem into live equipped-melee interaction.

This transform runs after the three Phase-4 generators. It feeds dominant and
off-hand tracking into the runtime-neutral two-hand constraint, uses the final
weapon pose for contact geometry, and prevents the off hand from simultaneously
acting as a fist while it is latched onto a two-handed weapon.
"""
from pathlib import Path

PATH = Path("ArxAndroid/src/core/ArxGame.cpp")
text = PATH.read_bytes().decode("utf-8")
original = text


def replace_exact(old: str, new: str, expected: int, label: str) -> None:
    global text
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} literal match(es), found {count}")
    text = text.replace(old, new)


replace_exact(
    '#include "vr/VrWeaponContact.h"\n',
    '#include "vr/VrWeaponContact.h"\n#include "vr/VrWeaponSystem.h"\n',
    1,
    "weapon system include",
)

replace_exact(
    "static arxvr::VrInteractionSystem g_vrInteractions;\n",
    "static arxvr::VrInteractionSystem g_vrInteractions;\n"
    "static arxvr::VrWeaponSystem g_vrWeaponSystem;\n",
    1,
    "weapon system state",
)

old_function = """static void updateVrEquippedWeaponCombat(bool rightHand, bool haveHand,
                                         const Vec3f & handPosition,
                                         const Vec3f & handDirection,
                                         Entity & weapon,
                                         const arxvr::VrWeaponProfile & profile,
                                         bool allowHit, std::uint64_t timestampUs) {
\tconst arxvr::VrHand hand = rightHand ? arxvr::VrHand::Right : arxvr::VrHand::Left;
\tconst arxvr::VrWeaponSegment segment = haveHand
\t    ? arxvr::vrBuildWeaponSegment(profile, vrImpactVector(handPosition),
\t                                  vrImpactVector(handDirection))
\t    : arxvr::VrWeaponSegment{};

\tarxvr::VrImpactSample sample;
\tsample.motion.timestampUs = timestampUs;
\tif(segment.valid) {
\t\tsample.motion.x = segment.end.x;
\t\tsample.motion.y = segment.end.y;
\t\tsample.motion.z = segment.end.z;
\t}
\tsample.source = arxvr::VrImpactSource::EquippedWeapon;
\tsample.sourceToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&weapon));
\tsample.profileOverride = profile.strike;
\tsample.effectiveMass = profile.effectiveMass;
\tsample.gestureActive = haveHand && segment.valid && allowHit;
\tsample.trackingValid = haveHand && segment.valid;
\tsample.useProfileOverride = true;

\tconst arxvr::VrImpactGateStatus status = g_vrInteractions.updateHand(hand, sample);
\tif(status != arxvr::VrImpactGateStatus::Qualified || !sample.gestureActive) {
\t\treturn;
\t}

\tVec3f hitPosition(0.f);
\tfloat contactT = 0.f;
\tEntity * target = findVrEquippedWeaponTarget(segment, hitPosition, contactT);
\tif(!target) {
\t\treturn;
\t}

\tarxvr::VrImpactEvent impact;
\tif(!g_vrInteractions.consumeImpact(hand, impact)) {
\t\treturn;
\t}
\timpact.attackerToken = static_cast<std::uint64_t>(
\t\treinterpret_cast<std::uintptr_t>(entities.player()));
\timpact.targetToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));

\tconst float impactSpeed = std::max(impact.metrics.terminalSpeed,
\t                                   impact.metrics.averageSpeed);
\tconst float speedScale = glm::clamp(
\t\timpactSpeed / std::max(profile.strike.minPeakSpeed * 2.f, 1.f), 0.25f, 1.f);
\tconst float massScale = glm::clamp(std::sqrt(impact.effectiveMass), 0.75f, 1.35f);
\tconst float tipBlend = glm::clamp((contactT - 0.45f) / 0.55f, 0.f, 1.f);
\tconst float tipScale = glm::mix(1.f, profile.tipDamageMultiplier, tipBlend);
\tconst float strength = glm::clamp(speedScale * massScale * tipScale, 0.25f, 1.f);

\tconst float damage = ARX_EQUIPMENT_ComputeDamages(
\t\tentities.player(), target, strength, &hitPosition);
\tARX_DAMAGES_DurabilityCheck(&weapon, g_framedelay * 0.006f);
\tarxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
\t                VrHapticEvent::ImpactHeavy,
\t                glm::clamp(strength, 0.45f, 1.f));
\tARX_PLAYER_Remove_Invisibility();
\tLogInfo << "ArxVR equipped-weapon hit: weapon=" << weapon.idString()
\t        << " target=" << target->idString() << " speed=" << impactSpeed
\t        << " mass=" << impact.effectiveMass << " contactT=" << contactT
\t        << " path=" << impact.metrics.pathLength
\t        << " consistency=" << impact.metrics.directionalConsistency
\t        << " strength=" << strength << " damage=" << damage
\t        << " life=" << target->_npcdata->lifePool.current;
}
"""

new_function = """static void updateVrEquippedWeaponCombat(bool rightHand, bool haveHand,
                                         const Vec3f & handPosition,
                                         const Vec3f & handDirection,
                                         const Vec3f & handUp,
                                         bool haveSecondaryHand,
                                         const Vec3f & secondaryHandPosition,
                                         bool secondaryGripPressed,
                                         Entity & weapon,
                                         const arxvr::VrWeaponProfile & profile,
                                         bool allowHit, std::uint64_t timestampUs) {
\tconst arxvr::VrHand hand = rightHand ? arxvr::VrHand::Right : arxvr::VrHand::Left;

\tarxvr::VrWeaponTrackingSample tracking;
\ttracking.weaponToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&weapon));
\ttracking.primaryPosition = vrImpactVector(handPosition);
\ttracking.primaryForward = vrImpactVector(handDirection);
\ttracking.primaryUp = vrImpactVector(handUp);
\ttracking.primaryValid = haveHand;
\ttracking.secondaryPosition = vrImpactVector(secondaryHandPosition);
\ttracking.secondaryValid = haveSecondaryHand;
\ttracking.secondaryGripPressed = secondaryGripPressed;

\tconst arxvr::VrWeaponPose weaponPose = g_vrWeaponSystem.update(profile, tracking);
\tconst arxvr::VrWeaponSegment segment =
\t\tg_vrWeaponSystem.buildContactSegment(profile, weaponPose);

\tarxvr::VrImpactSample sample;
\tsample.motion.timestampUs = timestampUs;
\tif(segment.valid) {
\t\tsample.motion.x = segment.end.x;
\t\tsample.motion.y = segment.end.y;
\t\tsample.motion.z = segment.end.z;
\t}
\tsample.source = arxvr::VrImpactSource::EquippedWeapon;
\tsample.sourceToken = tracking.weaponToken;
\tsample.profileOverride = profile.strike;
\tsample.effectiveMass = profile.effectiveMass;
\tsample.gestureActive = weaponPose.valid && segment.valid && allowHit;
\tsample.trackingValid = weaponPose.valid && segment.valid;
\tsample.useProfileOverride = true;

\tconst arxvr::VrImpactGateStatus status = g_vrInteractions.updateHand(hand, sample);
\tif(status != arxvr::VrImpactGateStatus::Qualified || !sample.gestureActive) {
\t\treturn;
\t}

\tVec3f hitPosition(0.f);
\tfloat contactT = 0.f;
\tEntity * target = findVrEquippedWeaponTarget(segment, hitPosition, contactT);
\tif(!target) {
\t\treturn;
\t}

\tarxvr::VrImpactEvent impact;
\tif(!g_vrInteractions.consumeImpact(hand, impact)) {
\t\treturn;
\t}
\timpact.attackerToken = static_cast<std::uint64_t>(
\t\treinterpret_cast<std::uintptr_t>(entities.player()));
\timpact.targetToken = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(target));

\tconst float impactSpeed = std::max(impact.metrics.terminalSpeed,
\t                                   impact.metrics.averageSpeed);
\tconst float speedScale = glm::clamp(
\t\timpactSpeed / std::max(profile.strike.minPeakSpeed * 2.f, 1.f), 0.25f, 1.f);
\tconst float massScale = glm::clamp(std::sqrt(impact.effectiveMass), 0.75f, 1.35f);
\tconst float tipBlend = glm::clamp((contactT - 0.45f) / 0.55f, 0.f, 1.f);
\tconst float tipScale = glm::mix(1.f, profile.tipDamageMultiplier, tipBlend);
\tconst float strength = glm::clamp(speedScale * massScale * tipScale, 0.25f, 1.f);

\tconst float damage = ARX_EQUIPMENT_ComputeDamages(
\t\tentities.player(), target, strength, &hitPosition);
\tARX_DAMAGES_DurabilityCheck(&weapon, g_framedelay * 0.006f);
\tarxvrEmitHaptic(rightHand ? VrHapticHand::Right : VrHapticHand::Left,
\t                VrHapticEvent::ImpactHeavy,
\t                glm::clamp(strength, 0.45f, 1.f));
\tARX_PLAYER_Remove_Invisibility();
\tLogInfo << "ArxVR equipped-weapon hit: weapon=" << weapon.idString()
\t        << " target=" << target->idString() << " speed=" << impactSpeed
\t        << " mass=" << impact.effectiveMass << " contactT=" << contactT
\t        << " twoHanded=" << weaponPose.twoHanded
\t        << " path=" << impact.metrics.pathLength
\t        << " consistency=" << impact.metrics.directionalConsistency
\t        << " strength=" << strength << " damage=" << damage
\t        << " life=" << target->_npcdata->lifePool.current;
}
"""
replace_exact(old_function, new_function, 1, "live equipped weapon system adapter")

replace_exact(
    """\tif(!g_haveVrCenterCamera || ARXmenu.mode() != Mode_InGame) {
\t\tg_vrInteractions.resetSession();
\t\tg_vrInteractionTarget = EntityHandle();
""",
    """\tif(!g_haveVrCenterCamera || ARXmenu.mode() != Mode_InGame) {
\t\tg_vrInteractions.resetSession();
\t\tg_vrWeaponSystem.reset();
\t\tg_vrInteractionTarget = EntityHandle();
""",
    1,
    "weapon system session reset",
)

old_routing = """\tconst bool equippedMeleeActive = equippedWeapon && equippedProfile.physicalMelee
\t                              && (player.Interface & INTER_COMBATMODE);
\tif(!rightDragging) {
\t\tif(equippedMeleeActive) {
\t\t\tupdateVrEquippedWeaponCombat(true, haveRightHand, rightHandPosition,
\t\t\t                             rightHandDirection, *equippedWeapon,
\t\t\t                             equippedProfile, !BLOCK_PLAYER_CONTROLS,
\t\t\t                             impactTimestampUs);
\t\t} else {
\t\t\tupdateVrFistCombat(true, haveRightHand, rightHandPosition,
\t\t\t                  arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE),
\t\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t\t}
\t}
\tif(!leftDragging) {
\t\tupdateVrFistCombat(false, haveLeftHand, leftHandPosition,
\t\t                  arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),
\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t}
"""

new_routing = """\tconst bool equippedMeleeActive = equippedWeapon && equippedProfile.physicalMelee
\t                              && (player.Interface & INTER_COMBATMODE);
\tif(!equippedMeleeActive || rightDragging) {
\t\tg_vrWeaponSystem.reset();
\t}
\tif(!rightDragging) {
\t\tif(equippedMeleeActive) {
\t\t\tconst Vec3f rightHandUp = haveRightHand
\t\t\t                        ? rightHandOrientation * Vec3f(0.f, 1.f, 0.f)
\t\t\t                        : Vec3f(0.f, 1.f, 0.f);
\t\t\tconst bool secondaryAvailable = haveLeftHand && !leftDragging;
\t\t\tupdateVrEquippedWeaponCombat(
\t\t\t\ttrue, haveRightHand, rightHandPosition, rightHandDirection, rightHandUp,
\t\t\t\tsecondaryAvailable, leftHandPosition,
\t\t\t\tsecondaryAvailable && arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),
\t\t\t\t*equippedWeapon, equippedProfile, !BLOCK_PLAYER_CONTROLS,
\t\t\t\timpactTimestampUs);
\t\t} else {
\t\t\tupdateVrFistCombat(true, haveRightHand, rightHandPosition,
\t\t\t                  arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE),
\t\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t\t}
\t}
\tconst bool secondaryWeaponGripActive = equippedMeleeActive
\t                                    && g_vrWeaponSystem.twoHanded();
\tif(!leftDragging && !secondaryWeaponGripActive) {
\t\tupdateVrFistCombat(false, haveLeftHand, leftHandPosition,
\t\t                  arxvrButtonPressed(ARXVR_BUTTON_LEFT_SQUEEZE),
\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t}
"""
replace_exact(old_routing, new_routing, 1, "two-hand live routing")

for required in (
    '#include "vr/VrWeaponSystem.h"',
    "static arxvr::VrWeaponSystem g_vrWeaponSystem;",
    "VrWeaponTrackingSample tracking;",
    "g_vrWeaponSystem.update(profile, tracking)",
    "g_vrWeaponSystem.buildContactSegment(profile, weaponPose)",
    "secondaryWeaponGripActive",
    "rightHandOrientation * Vec3f(0.f, 1.f, 0.f)",
    '<< " twoHanded=" << weaponPose.twoHanded',
):
    if required not in text:
        raise RuntimeError(f"required Phase-5 two-hand integration token missing: {required}")

if text == original:
    raise RuntimeError("Phase-5 two-hand transform produced no changes")

PATH.write_bytes(text.encode("utf-8"))
print("Integrated live two-hand equipped weapon constraints in", PATH)
