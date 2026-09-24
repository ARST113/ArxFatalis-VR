#!/usr/bin/env python3
"""Route equipped melee through the Phase 4 semantic impact service.

This deterministic third-stage transform runs after the guarded fist/held-object
integration and VrInteractionSystem promotion. It keeps legacy Arx combat
resolution downstream while replacing legacy attack-button strike qualification
with tracked controller motion and controller-aligned weapon contact geometry.
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
    '#include "vr/VrInteractionSystem.h"\n',
    '#include "vr/VrInteractionSystem.h"\n#include "vr/VrWeaponContact.h"\n',
    1,
    "weapon contact include",
)

replace_exact(
    "static std::uint64_t vrImpactTimestampUs() {\n"
    "\treturn static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(\n"
    "\t\tstd::chrono::steady_clock::now().time_since_epoch()).count());\n"
    "}\n",
    """static std::uint64_t vrImpactTimestampUs() {
\treturn static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
\t\tstd::chrono::steady_clock::now().time_since_epoch()).count());
}

static arxvr::VrWeaponClass vrWeaponClassForArx(WeaponType type) {
\tswitch(type) {
\t\tcase WEAPON_DAGGER: return arxvr::VrWeaponClass::Dagger;
\t\tcase WEAPON_1H: return arxvr::VrWeaponClass::OneHanded;
\t\tcase WEAPON_2H: return arxvr::VrWeaponClass::TwoHanded;
\t\tcase WEAPON_BOW: return arxvr::VrWeaponClass::Bow;
\t\tcase WEAPON_BARE: return arxvr::VrWeaponClass::Unknown;
\t}
\treturn arxvr::VrWeaponClass::Unknown;
}

static arxvr::VrImpactVector3 vrImpactVector(const Vec3f & value) {
\treturn { value.x, value.y, value.z };
}

static Vec3f vrArxVector(const arxvr::VrImpactVector3 & value) {
\treturn Vec3f(value.x, value.y, value.z);
}
""",
    1,
    "weapon profile adapter helpers",
)

replace_exact(
    "static Entity * findVrFistTarget(const Vec3f & handPosition) {\n",
    """static Entity * findVrEquippedWeaponTarget(const arxvr::VrWeaponSegment & segment,
                                             Vec3f & hitPosition,
                                             float & contactT) {
\tEntity * nearest = nullptr;
\tfloat nearestT = 2.f;
\tfor(Entity & entity : entities.inScene()) {
\t\tif(!isVrInteractionCandidate(entity) || !(entity.ioflags & IO_NPC)
\t\t   || !entity._npcdata || entity._npcdata->lifePool.current <= 0.f
\t\t   || !entity.bbox3D.valid()) {
\t\t\tcontinue;
\t\t}

\t\tfloat entryT = 0.f;
\t\tif(!arxvr::vrWeaponSegmentIntersectsAabb(
\t\t       segment, vrImpactVector(entity.bbox3D.min),
\t\t       vrImpactVector(entity.bbox3D.max), entryT)
\t\t   || entryT >= nearestT) {
\t\t\tcontinue;
\t\t}

\t\tnearestT = entryT;
\t\tnearest = &entity;
\t\tconst Vec3f contact = vrArxVector(arxvr::vrWeaponSegmentPoint(segment, entryT));
\t\thitPosition = glm::clamp(contact, entity.bbox3D.min, entity.bbox3D.max);
\t}

\tcontactT = nearest ? nearestT : 0.f;
\treturn nearest;
}

static void updateVrEquippedWeaponCombat(bool rightHand, bool haveHand,
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

static Entity * findVrFistTarget(const Vec3f & handPosition) {
""",
    1,
    "equipped weapon combat path",
)

replace_exact(
    """\t// Each physical hand owns exactly one impact state. The free hand keeps
\t// collecting fist motion while the other hand holds an object; the drag
\t// hand is updated only as HeldObject so source history is not reset twice
\t// in the same frame.
\tif(!rightDragging) {
\t\tupdateVrFistCombat(true, haveRightHand, rightHandPosition,
\t\t                  arxvrButtonPressed(ARXVR_BUTTON_RIGHT_SQUEEZE),
\t\t                  !BLOCK_PLAYER_CONTROLS, impactTimestampUs);
\t}
\tif(!leftDragging) {
""",
    """\t// Each physical hand owns exactly one semantic impact source per frame.
\t// An equipped melee weapon takes ownership of the dominant (right) hand
\t// while it is readied in combat mode; physical drag still has priority.
\tEntity * equippedWeapon = entities.get(player.equiped[EQUIP_SLOT_WEAPON]);
\tconst arxvr::VrWeaponProfile equippedProfile = arxvr::vrDefaultWeaponProfile(
\t\tvrWeaponClassForArx(ARX_EQUIPMENT_GetPlayerWeaponType()));
\tconst bool equippedMeleeActive = equippedWeapon && equippedProfile.physicalMelee
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
""",
    1,
    "dominant-hand equipped weapon routing",
)

for required in (
    '#include "vr/VrWeaponContact.h"',
    "vrWeaponClassForArx",
    "updateVrEquippedWeaponCombat",
    "VrImpactSource::EquippedWeapon",
    "vrWeaponSegmentIntersectsAabb",
    "sample.profileOverride = profile.strike",
    "sample.effectiveMass = profile.effectiveMass",
    "equippedMeleeActive",
):
    if required not in text:
        raise RuntimeError(f"required equipped-weapon integration token missing: {required}")

if text == original:
    raise RuntimeError("equipped-weapon transform produced no changes")

PATH.write_bytes(text.encode("utf-8"))
print("Integrated tracked equipped-melee impacts in", PATH)
