#pragma once

#include <cstdint>

#include "vr/VrImpactGate.h"

namespace arxvr {

enum class VrHand : std::uint8_t {
	Left = 0,
	Right = 1,
	Unknown = 255
};

enum class VrImpactType : std::uint8_t {
	None = 0,
	Fist,
	HeldObject,
	EquippedWeapon,
	Shield,
	Projectile
};

struct VrImpactVector3 {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;
};

// Transport-neutral representation of a qualified physical impact. The
// qualification layer fills tracked kinematics here; Arx gameplay code can
// subsequently resolve attacker/target/weapon handles and apply the existing
// damage, scripts, sounds and haptics without depending on OpenXR types.
struct VrImpactEvent {
	VrHand hand = VrHand::Unknown;
	VrImpactType type = VrImpactType::None;
	VrImpactSource source = VrImpactSource::None;
	std::uint64_t sourceToken = 0;

	// Engine-facing identity slots. They deliberately use opaque integer tokens
	// at this boundary so the VR subsystem remains testable without EntityHandle.
	// Live Arx routing may populate these after target selection.
	std::uint64_t attackerToken = 0;
	std::uint64_t targetToken = 0;
	std::uint64_t weaponToken = 0;

	VrMotionSample motion{};
	VrStrikeMetrics metrics{};
	VrImpactVector3 position{};
	VrImpactVector3 direction{};
	VrImpactVector3 linearVelocity{};
	VrImpactVector3 angularVelocity{};
	float effectiveMass = 1.f;
};

inline constexpr VrImpactType vrImpactTypeForSource(VrImpactSource source) {
	switch(source) {
		case VrImpactSource::Fist:
			return VrImpactType::Fist;
		case VrImpactSource::HeldObject:
			return VrImpactType::HeldObject;
		case VrImpactSource::EquippedWeapon:
			return VrImpactType::EquippedWeapon;
		case VrImpactSource::Shield:
			return VrImpactType::Shield;
		case VrImpactSource::Projectile:
			return VrImpactType::Projectile;
		case VrImpactSource::None:
			return VrImpactType::None;
	}
	return VrImpactType::None;
}

} // namespace arxvr
