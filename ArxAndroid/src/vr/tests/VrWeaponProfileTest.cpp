#include "vr/VrInteractionSystem.h"
#include "vr/VrWeaponProfile.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

int g_failures = 0;

void expect(bool condition, const char * message) {
	if(!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

arxvr::VrImpactSample weaponSample(float x, float y, std::uint64_t timestampUs,
                                   std::uint64_t token,
                                   const arxvr::VrWeaponProfile & profile,
                                   float effectiveMass) {
	arxvr::VrImpactSample sample;
	sample.motion = { x, y, 0.f, timestampUs };
	sample.source = arxvr::VrImpactSource::EquippedWeapon;
	sample.sourceToken = token;
	sample.profileOverride = profile.strike;
	sample.effectiveMass = effectiveMass;
	sample.gestureActive = true;
	sample.trackingValid = true;
	sample.useProfileOverride = true;
	return sample;
}

void feedWeaponSwing(arxvr::VrInteractionSystem & system,
                     const arxvr::VrWeaponProfile & profile,
                     std::uint64_t token, float effectiveMass,
                     std::uint64_t startUs = 0, float offsetX = 0.f) {
	const arxvr::VrHand hand = arxvr::VrHand::Right;
	system.updateHand(hand, weaponSample(offsetX + 0.f, 0.f, startUs + 0,
	                                    token, profile, effectiveMass));
	system.updateHand(hand, weaponSample(offsetX + 2.f, 0.2f, startUs + 20000,
	                                    token, profile, effectiveMass));
	system.updateHand(hand, weaponSample(offsetX + 5.f, 0.6f, startUs + 40000,
	                                    token, profile, effectiveMass));
	system.updateHand(hand, weaponSample(offsetX + 9.f, 1.2f, startUs + 60000,
	                                    token, profile, effectiveMass));
	system.updateHand(hand, weaponSample(offsetX + 14.f, 2.f, startUs + 80000,
	                                    token, profile, effectiveMass));
}

void defaultProfilesHaveSafeOrdering() {
	const auto unknown = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Unknown);
	const auto dagger = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Dagger);
	const auto oneHanded = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	const auto twoHanded = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	const auto bow = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Bow);

	expect(!unknown.physicalMelee,
	       "unknown equipment must fail closed instead of becoming a generic melee weapon");
	expect(dagger.physicalMelee && oneHanded.physicalMelee && twoHanded.physicalMelee,
	       "supported melee classes should explicitly enable physical impacts");
	expect(!bow.physicalMelee,
	       "bow must stay out of the melee impact pipeline");
	expect(!dagger.supportsTwoHand && !oneHanded.supportsTwoHand
	       && twoHanded.supportsTwoHand && bow.supportsTwoHand,
	       "two-hand capability should follow the weapon interaction model");
	expect(dagger.reach < oneHanded.reach && oneHanded.reach < twoHanded.reach,
	       "weapon reach should increase from dagger to one-handed to two-handed");
	expect(dagger.effectiveMass < oneHanded.effectiveMass
	       && oneHanded.effectiveMass < twoHanded.effectiveMass,
	       "effective mass should retain broad weapon-class ordering");
	expect(dagger.strike.minPathLength < twoHanded.strike.minPathLength,
	       "heavy two-handed swings should require a longer deliberate path");
}

void profileOverrideActuallyControlsQualification() {
	auto strict = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	strict.strike.minPathLength = 30.f;
	arxvr::VrInteractionSystem strictSystem;
	feedWeaponSwing(strictSystem, strict, 0x101u, strict.effectiveMass);
	expect(!strictSystem.canImpact(arxvr::VrHand::Right),
	       "equipped-weapon profile override must be applied by the private hand state");

	const auto normal = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	arxvr::VrInteractionSystem normalSystem;
	feedWeaponSwing(normalSystem, normal, 0x102u, normal.effectiveMass);
	expect(normalSystem.canImpact(arxvr::VrHand::Right),
	       "normal one-handed profile should qualify the deterministic deliberate swing");
}

void weaponMassReachesSemanticImpact() {
	const auto twoHanded = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrInteractionSystem system;
	feedWeaponSwing(system, twoHanded, 0x201u, twoHanded.effectiveMass);

	arxvr::VrImpactEvent impact;
	expect(system.consumeImpact(arxvr::VrHand::Right, impact),
	       "two-handed swing should emit a semantic impact");
	expect(std::abs(impact.effectiveMass - twoHanded.effectiveMass) < 0.0001f,
	       "weapon effective mass should survive qualification into VrImpactEvent");
	expect(impact.weaponToken == 0x201u,
	       "equipped weapon identity should remain attached to the semantic impact");
}

void malformedMassFailsToNeutralMass() {
	const auto dagger = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Dagger);
	arxvr::VrInteractionSystem system;
	feedWeaponSwing(system, dagger, 0x301u,
	                std::numeric_limits<float>::quiet_NaN());

	arxvr::VrImpactEvent impact;
	expect(system.consumeImpact(arxvr::VrHand::Right, impact),
	       "malformed mass must not invalidate otherwise valid tracked kinematics");
	expect(impact.effectiveMass == 1.f,
	       "non-finite effective mass should be neutralized at the hand-state boundary");
}

void switchingWeaponProfileCannotBypassPreviousCooldown() {
	const auto oneHanded = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	const auto dagger = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Dagger);
	arxvr::VrInteractionSystem system;
	feedWeaponSwing(system, oneHanded, 0x401u, oneHanded.effectiveMass);
	arxvr::VrImpactEvent firstImpact;
	expect(system.consumeImpact(arxvr::VrHand::Right, firstImpact),
	       "first weapon should establish a real cooldown/retraction boundary");

	// Start the replacement weapon at the previous contact point and move far
	// enough to satisfy retraction, but remain well inside the original 300 ms
	// cooldown. A profile/token transition may clear path history only.
	feedWeaponSwing(system, dagger, 0x402u, dagger.effectiveMass, 100000, 14.f);
	expect(!system.canImpact(arxvr::VrHand::Right),
	       "swapping to a faster weapon profile must not clear the previous cooldown");
	arxvr::VrImpactEvent secondImpact;
	expect(!system.consumeImpact(arxvr::VrHand::Right, secondImpact),
	       "weapon swapping must not emit a second impact during the inherited cooldown");
}

} // namespace

int main() {
	defaultProfilesHaveSafeOrdering();
	profileOverrideActuallyControlsQualification();
	weaponMassReachesSemanticImpact();
	malformedMassFailsToNeutralMass();
	switchingWeaponProfileCannotBypassPreviousCooldown();

	if(g_failures != 0) {
		std::cerr << g_failures << " weapon-profile test(s) failed\n";
		return 1;
	}
	std::cout << "VrWeaponProfile: class profiles and semantic mass tests passed\n";
	return 0;
}
