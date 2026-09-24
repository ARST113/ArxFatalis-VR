#pragma once

#include <cstdint>

#include "vr/VrImpact.h"

namespace arxvr {

// Engine-neutral weapon categories used by the physical VR layer. The Arx
// equipment adapter maps its native WeaponType to one of these categories;
// bows intentionally remain outside physical melee and are handled by Phase 6.
enum class VrWeaponClass : std::uint8_t {
	Unknown = 0,
	Dagger,
	OneHanded,
	TwoHanded,
	Bow
};

struct VrWeaponProfile {
	VrWeaponClass weaponClass = VrWeaponClass::Unknown;
	bool physicalMelee = false;
	bool supportsTwoHand = false;

	// Controller-local alignment fields are intentionally data, not OpenXR
	// transforms. They can later be tuned per weapon/controller without coupling
	// gameplay code to a runtime API.
	VrImpactVector3 gripOffset{};
	VrImpactVector3 gripAnglesDegrees{};
	VrImpactVector3 secondaryGripLocal{};

	// Arx world units. These are conservative engineering defaults used to build
	// and test the collision/qualification path before headset calibration.
	float reach = 0.f;
	float contactRadius = 0.f;
	float effectiveMass = 1.f;
	float tipDamageMultiplier = 1.f;
	VrStrikeProfile strike = vrHeldObjectStrikeProfile();
};

inline constexpr VrWeaponProfile vrDefaultWeaponProfile(VrWeaponClass weaponClass) {
	VrWeaponProfile profile;
	profile.weaponClass = weaponClass;
	profile.strike = vrHeldObjectStrikeProfile();

	switch(weaponClass) {
		case VrWeaponClass::Dagger:
			profile.physicalMelee = true;
			profile.reach = 48.f;
			profile.contactRadius = 11.f;
			profile.effectiveMass = 0.45f;
			profile.tipDamageMultiplier = 1.15f;
			profile.strike.historyWindowUs = 150000;
			profile.strike.cooldownUs = 240000;
			profile.strike.minPeakSpeed = 105.f;
			profile.strike.minAverageSpeed = 65.f;
			profile.strike.minTerminalSpeed = 60.f;
			profile.strike.minFollowThroughSpeed = 30.f;
			profile.strike.maxFollowThroughToPeakRatio = 0.80f;
			profile.strike.minPathLength = 7.f;
			profile.strike.minDirectionalConsistency = 0.50f;
			profile.strike.minEnergy = 550.f;
			profile.strike.rearmSpeed = 35.f;
			profile.strike.minRearmDistance = 6.f;
			profile.strike.maxInstantSpeed = 1400.f;
			break;

		case VrWeaponClass::OneHanded:
			profile.physicalMelee = true;
			profile.reach = 85.f;
			profile.contactRadius = 14.f;
			profile.effectiveMass = 1.f;
			profile.tipDamageMultiplier = 1.20f;
			profile.strike.minPathLength = 9.f;
			profile.strike.minRearmDistance = 8.f;
			break;

		case VrWeaponClass::TwoHanded:
			profile.physicalMelee = true;
			profile.supportsTwoHand = true;
			profile.reach = 110.f;
			profile.contactRadius = 16.f;
			profile.effectiveMass = 1.8f;
			profile.tipDamageMultiplier = 1.25f;
			profile.secondaryGripLocal = { 0.f, 0.f, -28.f };
			profile.strike.historyWindowUs = 220000;
			profile.strike.cooldownUs = 350000;
			profile.strike.minPeakSpeed = 85.f;
			profile.strike.minAverageSpeed = 52.f;
			profile.strike.minTerminalSpeed = 52.f;
			profile.strike.minFollowThroughSpeed = 28.f;
			profile.strike.maxFollowThroughToPeakRatio = 0.80f;
			profile.strike.minPathLength = 12.f;
			profile.strike.minDirectionalConsistency = 0.45f;
			profile.strike.minEnergy = 650.f;
			profile.strike.rearmSpeed = 32.f;
			profile.strike.minRearmDistance = 10.f;
			profile.strike.maxInstantSpeed = 1400.f;
			break;

		case VrWeaponClass::Bow:
			// A bow uses a two-hand physical interaction, but its attack is produced
			// by string draw/release rather than melee impact qualification.
			profile.supportsTwoHand = true;
			break;

		case VrWeaponClass::Unknown:
			break;
	}

	return profile;
}

} // namespace arxvr
