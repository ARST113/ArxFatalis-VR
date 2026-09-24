#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "vr/VrDefenseSystem.h"

namespace arxvr {

enum class VrProjectileDeflectionType : std::uint8_t {
	None = 0,
	Shield,
	Weapon
};

struct VrProjectileDefenseConfig {
	// Arx world units / second. Projectiles below this speed are treated as
	// resting/embedded objects and cannot be reflected by tracked defense poses.
	float minimumSpeed = 50.f;
	// Reflection coefficients intentionally do not add energy. Final values are
	// expected to be tuned on-device after controller/contact calibration.
	float shieldRestitution = 0.75f;
	float weaponRestitution = 0.85f;
	float weaponContactPadding = 2.f;
	// Projectile motion must cross the weapon axis by at least this sin(angle).
	// This rejects a projectile travelling almost exactly along a blade.
	float minimumWeaponCrossSin = 0.18f;
	// A reflected projectile must physically leave the contact neighbourhood
	// before the same token can generate another deflection.
	float rearmDistance = 12.f;
};

struct VrProjectileSample {
	std::uint64_t token = 0;
	VrImpactVector3 start{};
	VrImpactVector3 end{};
	VrImpactVector3 velocity{};
	float radius = 0.f;
	std::uint64_t timestampUs = 0;
};

struct VrProjectileDeflection {
	VrProjectileDeflectionType type = VrProjectileDeflectionType::None;
	VrImpactVector3 position{};
	VrImpactVector3 normal{};
	VrImpactVector3 outgoingVelocity{};
	float incomingSpeed = 0.f;
	float outgoingSpeed = 0.f;
	std::uint64_t timestampUs = 0;
};

inline bool vrProjectileDefenseConfigValid(const VrProjectileDefenseConfig & config) {
	return std::isfinite(config.minimumSpeed) && config.minimumSpeed >= 0.f
	    && std::isfinite(config.shieldRestitution)
	    && config.shieldRestitution >= 0.f && config.shieldRestitution <= 1.f
	    && std::isfinite(config.weaponRestitution)
	    && config.weaponRestitution >= 0.f && config.weaponRestitution <= 1.f
	    && std::isfinite(config.weaponContactPadding) && config.weaponContactPadding >= 0.f
	    && std::isfinite(config.minimumWeaponCrossSin)
	    && config.minimumWeaponCrossSin >= 0.f && config.minimumWeaponCrossSin <= 1.f
	    && std::isfinite(config.rearmDistance) && config.rearmDistance > 0.f;
}

inline bool vrProjectileSampleValid(const VrProjectileSample & sample) {
	if(sample.token == 0 || sample.timestampUs == 0 || sample.radius < 0.f
	   || !std::isfinite(sample.radius) || !vrVectorFinite(sample.start)
	   || !vrVectorFinite(sample.end) || !vrVectorFinite(sample.velocity)) {
		return false;
	}
	const float pathLength = vrVectorLength(vrDefenseSubtract(sample.end, sample.start));
	const float speed = vrVectorLength(sample.velocity);
	return std::isfinite(pathLength) && pathLength > 0.0001f
	    && std::isfinite(speed) && speed > 0.0001f;
}

inline bool vrProjectileReflect(const VrImpactVector3 & velocity,
                                VrImpactVector3 normal,
                                float restitution,
                                VrImpactVector3 & reflected) {
	reflected = {};
	if(!vrVectorFinite(velocity) || !std::isfinite(restitution)
	   || restitution < 0.f || restitution > 1.f
	   || !vrDefenseNormalize(normal, normal)) {
		return false;
	}
	// Orient the contact normal against the incoming velocity. This makes the
	// reflection stable when the closest-point geometry returns either sign.
	if(vrDefenseDot(velocity, normal) > 0.f) {
		normal = vrDefenseScale(normal, -1.f);
	}
	const float projection = vrDefenseDot(velocity, normal);
	const VrImpactVector3 ideal = vrDefenseSubtract(
		velocity, vrDefenseScale(normal, 2.f * projection));
	reflected = vrDefenseScale(ideal, restitution);
	return vrVectorFinite(reflected) && std::isfinite(vrVectorLength(reflected));
}

class VrProjectileDefenseSystem {
public:
	explicit VrProjectileDefenseSystem(VrProjectileDefenseConfig config = {})
		: m_config(config) { }

	bool evaluateShield(const VrProjectileSample & sample,
	                    const VrShieldProfile & shieldProfile,
	                    const VrShieldPose & shieldPose,
	                    VrProjectileDeflection & result) {
		result = {};
		if(!prepareSample(sample) || !vrDefenseShieldProfileValid(shieldProfile)) {
			return false;
		}

		VrImpactVector3 right{};
		VrImpactVector3 up{};
		VrImpactVector3 normal{};
		if(!vrDefenseBuildShieldBasis(shieldPose, right, up, normal)) {
			return false;
		}

		VrImpactVector3 direction{};
		if(!vrDefenseNormalize(sample.velocity, direction)) {
			return false;
		}
		const float frontalCosine = -vrDefenseDot(direction, normal);
		if(!std::isfinite(frontalCosine)
		   || frontalCosine < shieldProfile.minimumFrontalCosine) {
			return false;
		}

		// Expand the finite shield volume by the projectile radius so grazing
		// physical contacts are represented without converting the shield into an
		// infinite plane.
		VrShieldProfile expanded = shieldProfile;
		expanded.halfWidth += sample.radius;
		expanded.halfHeight += sample.radius;
		expanded.halfThickness += sample.radius;
		VrIncomingContact incoming;
		incoming.start = sample.start;
		incoming.end = sample.end;
		incoming.linearVelocity = sample.velocity;
		incoming.timestampUs = sample.timestampUs;
		float entryT = 0.f;
		if(!vrDefenseSegmentIntersectsShieldBox(incoming, expanded, shieldPose,
		                                      right, up, normal, entryT)) {
			return false;
		}

		VrImpactVector3 reflected{};
		if(!vrProjectileReflect(sample.velocity, normal,
		                       m_config.shieldRestitution, reflected)) {
			return false;
		}
		const VrImpactVector3 path = vrDefenseSubtract(sample.end, sample.start);
		return commit(sample, VrProjectileDeflectionType::Shield,
		              vrDefenseAdd(sample.start, vrDefenseScale(path, entryT)),
		              normal, reflected, result);
	}

	bool evaluateWeapon(const VrProjectileSample & sample,
	                    const VrWeaponSegment & defender,
	                    VrProjectileDeflection & result) {
		result = {};
		if(!prepareSample(sample) || !weaponValid(defender)) {
			return false;
		}

		VrWeaponSegment projectileSegment;
		projectileSegment.start = sample.start;
		projectileSegment.end = sample.end;
		projectileSegment.radius = sample.radius;
		projectileSegment.valid = true;

		VrImpactVector3 projectilePoint{};
		VrImpactVector3 weaponPoint{};
		float distance = 0.f;
		if(!vrDefenseClosestSegmentPoints(projectileSegment, defender,
		                                projectilePoint, weaponPoint, distance)) {
			return false;
		}
		const float allowedDistance = sample.radius + defender.radius
		                            + m_config.weaponContactPadding;
		if(!std::isfinite(allowedDistance) || allowedDistance < 0.f
		   || distance > allowedDistance) {
			return false;
		}

		VrImpactVector3 projectileDirection{};
		VrImpactVector3 weaponAxis{};
		if(!vrDefenseNormalize(sample.velocity, projectileDirection)
		   || !vrDefenseNormalize(vrDefenseSubtract(defender.end, defender.start),
		                         weaponAxis)) {
			return false;
		}
		const float crossSin = vrVectorLength(
			vrDefenseCross(projectileDirection, weaponAxis));
		if(!std::isfinite(crossSin) || crossSin < m_config.minimumWeaponCrossSin) {
			return false;
		}

		VrImpactVector3 normal = vrDefenseSubtract(projectilePoint, weaponPoint);
		if(!vrDefenseNormalize(normal, normal)) {
			// Exact segment intersections have no radial closest-point normal. Build
			// the component of projectile travel perpendicular to the blade, then
			// orient it against travel in vrProjectileReflect().
			normal = vrDefenseCross(weaponAxis,
			                        vrDefenseCross(projectileDirection, weaponAxis));
			if(!vrDefenseNormalize(normal, normal)) {
				return false;
			}
		}

		VrImpactVector3 reflected{};
		if(!vrProjectileReflect(sample.velocity, normal,
		                       m_config.weaponRestitution, reflected)) {
			return false;
		}
		const VrImpactVector3 contact = vrDefenseScale(
			vrDefenseAdd(projectilePoint, weaponPoint), 0.5f);
		return commit(sample, VrProjectileDeflectionType::Weapon,
		              contact, normal, reflected, result);
	}

	void resetSession() {
		for(ContactLatch & latch : m_latches) {
			latch = {};
		}
	}

private:
	struct ContactLatch {
		std::uint64_t token = 0;
		VrImpactVector3 contact{};
		std::uint64_t timestampUs = 0;
		bool active = false;
	};

	bool prepareSample(const VrProjectileSample & sample) {
		if(!vrProjectileDefenseConfigValid(m_config)
		   || !vrProjectileSampleValid(sample)) {
			return false;
		}
		const float speed = vrVectorLength(sample.velocity);
		if(!std::isfinite(speed) || speed < m_config.minimumSpeed) {
			return false;
		}

		if(ContactLatch * latch = findLatch(sample.token)) {
			if(sample.timestampUs <= latch->timestampUs) {
				return false;
			}
			const float separation = vrVectorLength(
				vrDefenseSubtract(sample.end, latch->contact));
			if(!std::isfinite(separation) || separation < m_config.rearmDistance) {
				return false;
			}
			*latch = {};
		}
		return true;
	}

	bool commit(const VrProjectileSample & sample,
	            VrProjectileDeflectionType type,
	            const VrImpactVector3 & position,
	            VrImpactVector3 normal,
	            const VrImpactVector3 & outgoingVelocity,
	            VrProjectileDeflection & result) {
		if(type == VrProjectileDeflectionType::None || !vrVectorFinite(position)
		   || !vrDefenseNormalize(normal, normal)
		   || !vrVectorFinite(outgoingVelocity)) {
			return false;
		}
		const float incomingSpeed = vrVectorLength(sample.velocity);
		const float outgoingSpeed = vrVectorLength(outgoingVelocity);
		if(!std::isfinite(incomingSpeed) || !std::isfinite(outgoingSpeed)
		   || outgoingSpeed > incomingSpeed + 0.001f) {
			return false;
		}

		ContactLatch & latch = allocateLatch(sample.token);
		latch.token = sample.token;
		latch.contact = position;
		latch.timestampUs = sample.timestampUs;
		latch.active = true;

		result.type = type;
		result.position = position;
		result.normal = normal;
		result.outgoingVelocity = outgoingVelocity;
		result.incomingSpeed = incomingSpeed;
		result.outgoingSpeed = outgoingSpeed;
		result.timestampUs = sample.timestampUs;
		return true;
	}

	ContactLatch * findLatch(std::uint64_t token) {
		for(ContactLatch & latch : m_latches) {
			if(latch.active && latch.token == token) {
				return &latch;
			}
		}
		return nullptr;
	}

	ContactLatch & allocateLatch(std::uint64_t token) {
		if(ContactLatch * existing = findLatch(token)) {
			return *existing;
		}
		for(ContactLatch & latch : m_latches) {
			if(!latch.active) {
				return latch;
			}
		}
		// Fixed-size deterministic storage: evict the oldest contact when many
		// projectiles overlap, avoiding allocations in the gameplay frame path.
		ContactLatch * oldest = &m_latches.front();
		for(ContactLatch & latch : m_latches) {
			if(latch.timestampUs < oldest->timestampUs) {
				oldest = &latch;
			}
		}
		return *oldest;
	}

	static bool weaponValid(const VrWeaponSegment & segment) {
		return segment.valid && vrVectorFinite(segment.start)
		    && vrVectorFinite(segment.end) && std::isfinite(segment.radius)
		    && segment.radius >= 0.f
		    && vrVectorLength(vrDefenseSubtract(segment.end, segment.start)) > 0.0001f;
	}

	VrProjectileDefenseConfig m_config{};
	std::array<ContactLatch, 16> m_latches{};
};

} // namespace arxvr
