#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "vr/VrWeaponContact.h"

namespace arxvr {

enum class VrDefenseEventType : std::uint8_t {
	None = 0,
	ShieldBlock,
	WeaponParry
};

struct VrShieldProfile {
	// Arx world units. These are conservative geometry defaults intended for
	// deterministic validation before per-model PICO calibration.
	float halfWidth = 24.f;
	float halfHeight = 32.f;
	float halfThickness = 4.f;
	// Incoming velocity must point into the outward shield normal by at least
	// this cosine. A value of 0.20 rejects rear and near-tangential contacts.
	float minimumFrontalCosine = 0.20f;
	std::uint64_t blockCooldownUs = 140000;
};

struct VrShieldPose {
	VrImpactVector3 center{};
	VrImpactVector3 normal{ 0.f, 0.f, 1.f };
	VrImpactVector3 up{ 0.f, 1.f, 0.f };
	bool valid = false;
};

struct VrIncomingContact {
	VrImpactVector3 start{};
	VrImpactVector3 end{};
	VrImpactVector3 linearVelocity{};
	std::uint64_t timestampUs = 0;
};

struct VrParryConfig {
	float contactPadding = 2.f;
	float minimumRelativeSpeed = 75.f;
	// Relative motion must cross the defender weapon rather than travel almost
	// directly along its length. Stored as sin(angle) for a cheap robust test.
	float minimumRelativeCrossSin = 0.30f;
	// Reject almost-collinear weapon overlap as a parry. It is handled as normal
	// contact by gameplay instead of repeatedly generating a parry latch.
	float minimumWeaponCrossSin = 0.12f;
	std::uint64_t parryCooldownUs = 160000;
};

struct VrParrySample {
	VrWeaponSegment defender{};
	VrWeaponSegment attacker{};
	VrImpactVector3 defenderVelocity{};
	VrImpactVector3 attackerVelocity{};
	std::uint64_t timestampUs = 0;
};

struct VrDefenseEvent {
	VrDefenseEventType type = VrDefenseEventType::None;
	VrImpactVector3 position{};
	VrImpactVector3 normal{};
	float relativeSpeed = 0.f;
	float contactDistance = 0.f;
	float angleSin = 0.f;
	std::uint64_t timestampUs = 0;
};

inline VrImpactVector3 vrDefenseAdd(const VrImpactVector3 & a,
                                    const VrImpactVector3 & b) {
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline VrImpactVector3 vrDefenseSubtract(const VrImpactVector3 & a,
                                         const VrImpactVector3 & b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline VrImpactVector3 vrDefenseScale(const VrImpactVector3 & value, float scale) {
	return { value.x * scale, value.y * scale, value.z * scale };
}

inline float vrDefenseDot(const VrImpactVector3 & a, const VrImpactVector3 & b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline VrImpactVector3 vrDefenseCross(const VrImpactVector3 & a,
                                      const VrImpactVector3 & b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

inline bool vrDefenseNormalize(const VrImpactVector3 & value,
                               VrImpactVector3 & normalized) {
	if(!vrVectorFinite(value)) {
		return false;
	}
	const float length = vrVectorLength(value);
	if(!std::isfinite(length) || length <= 0.0001f) {
		return false;
	}
	normalized = vrDefenseScale(value, 1.f / length);
	return vrVectorFinite(normalized);
}

inline bool vrDefenseBuildShieldBasis(const VrShieldPose & pose,
                                      VrImpactVector3 & right,
                                      VrImpactVector3 & up,
                                      VrImpactVector3 & normal) {
	if(!pose.valid || !vrVectorFinite(pose.center)
	   || !vrDefenseNormalize(pose.normal, normal)) {
		return false;
	}

	VrImpactVector3 candidateUp = pose.up;
	if(!vrVectorFinite(candidateUp)) {
		candidateUp = { 0.f, 1.f, 0.f };
	}
	candidateUp = vrDefenseSubtract(
		candidateUp, vrDefenseScale(normal, vrDefenseDot(candidateUp, normal)));
	if(!vrDefenseNormalize(candidateUp, up)) {
		VrImpactVector3 fallback = std::abs(normal.y) < 0.90f
		                             ? VrImpactVector3{ 0.f, 1.f, 0.f }
		                             : VrImpactVector3{ 1.f, 0.f, 0.f };
		fallback = vrDefenseSubtract(
			fallback, vrDefenseScale(normal, vrDefenseDot(fallback, normal)));
		if(!vrDefenseNormalize(fallback, up)) {
			return false;
		}
	}
	if(!vrDefenseNormalize(vrDefenseCross(up, normal), right)) {
		return false;
	}
	return vrDefenseNormalize(vrDefenseCross(normal, right), up);
}

inline bool vrDefenseShieldProfileValid(const VrShieldProfile & profile) {
	return std::isfinite(profile.halfWidth) && profile.halfWidth > 0.f
	    && std::isfinite(profile.halfHeight) && profile.halfHeight > 0.f
	    && std::isfinite(profile.halfThickness) && profile.halfThickness >= 0.f
	    && std::isfinite(profile.minimumFrontalCosine)
	    && profile.minimumFrontalCosine >= 0.f
	    && profile.minimumFrontalCosine <= 1.f;
}

inline VrImpactVector3 vrDefenseToShieldLocal(const VrImpactVector3 & point,
                                              const VrShieldPose & pose,
                                              const VrImpactVector3 & right,
                                              const VrImpactVector3 & up,
                                              const VrImpactVector3 & normal) {
	const VrImpactVector3 relative = vrDefenseSubtract(point, pose.center);
	return {
		vrDefenseDot(relative, right),
		vrDefenseDot(relative, up),
		vrDefenseDot(relative, normal)
	};
}

inline bool vrDefenseSegmentIntersectsShieldBox(const VrIncomingContact & incoming,
                                                const VrShieldProfile & profile,
                                                const VrShieldPose & pose,
                                                const VrImpactVector3 & right,
                                                const VrImpactVector3 & up,
                                                const VrImpactVector3 & normal,
                                                float & entryT) {
	entryT = 0.f;
	if(!vrVectorFinite(incoming.start) || !vrVectorFinite(incoming.end)) {
		return false;
	}
	const VrImpactVector3 start = vrDefenseToShieldLocal(
		incoming.start, pose, right, up, normal);
	const VrImpactVector3 end = vrDefenseToShieldLocal(
		incoming.end, pose, right, up, normal);
	const VrImpactVector3 delta = vrDefenseSubtract(end, start);
	const float minimum[3] = {
		-profile.halfWidth, -profile.halfHeight, -profile.halfThickness
	};
	const float maximum[3] = {
		profile.halfWidth, profile.halfHeight, profile.halfThickness
	};
	const float origin[3] = { start.x, start.y, start.z };
	const float direction[3] = { delta.x, delta.y, delta.z };

	float tMin = 0.f;
	float tMax = 1.f;
	for(int axis = 0; axis < 3; ++axis) {
		if(std::abs(direction[axis]) <= 0.000001f) {
			if(origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
				return false;
			}
			continue;
		}
		const float inverse = 1.f / direction[axis];
		float nearT = (minimum[axis] - origin[axis]) * inverse;
		float farT = (maximum[axis] - origin[axis]) * inverse;
		if(nearT > farT) {
			std::swap(nearT, farT);
		}
		tMin = std::max(tMin, nearT);
		tMax = std::min(tMax, farT);
		if(tMin > tMax) {
			return false;
		}
	}
	entryT = std::clamp(tMin, 0.f, 1.f);
	return true;
}

inline bool vrDefenseParryConfigValid(const VrParryConfig & config) {
	return std::isfinite(config.contactPadding) && config.contactPadding >= 0.f
	    && std::isfinite(config.minimumRelativeSpeed)
	    && config.minimumRelativeSpeed >= 0.f
	    && std::isfinite(config.minimumRelativeCrossSin)
	    && config.minimumRelativeCrossSin >= 0.f
	    && config.minimumRelativeCrossSin <= 1.f
	    && std::isfinite(config.minimumWeaponCrossSin)
	    && config.minimumWeaponCrossSin >= 0.f
	    && config.minimumWeaponCrossSin <= 1.f;
}

inline bool vrDefenseClosestSegmentPoints(const VrWeaponSegment & first,
                                          const VrWeaponSegment & second,
                                          VrImpactVector3 & firstPoint,
                                          VrImpactVector3 & secondPoint,
                                          float & distance) {
	if(!first.valid || !second.valid
	   || !vrVectorFinite(first.start) || !vrVectorFinite(first.end)
	   || !vrVectorFinite(second.start) || !vrVectorFinite(second.end)) {
		return false;
	}

	const VrImpactVector3 d1 = vrDefenseSubtract(first.end, first.start);
	const VrImpactVector3 d2 = vrDefenseSubtract(second.end, second.start);
	const VrImpactVector3 r = vrDefenseSubtract(first.start, second.start);
	const float a = vrDefenseDot(d1, d1);
	const float e = vrDefenseDot(d2, d2);
	const float f = vrDefenseDot(d2, r);
	if(!std::isfinite(a) || !std::isfinite(e) || a <= 0.000001f || e <= 0.000001f) {
		return false;
	}

	float s = 0.f;
	float t = 0.f;
	const float c = vrDefenseDot(d1, r);
	const float b = vrDefenseDot(d1, d2);
	const float denominator = a * e - b * b;
	if(std::abs(denominator) > 0.000001f) {
		s = std::clamp((b * f - c * e) / denominator, 0.f, 1.f);
	}
	t = (b * s + f) / e;
	if(t < 0.f) {
		t = 0.f;
		s = std::clamp(-c / a, 0.f, 1.f);
	} else if(t > 1.f) {
		t = 1.f;
		s = std::clamp((b - c) / a, 0.f, 1.f);
	}

	firstPoint = vrDefenseAdd(first.start, vrDefenseScale(d1, s));
	secondPoint = vrDefenseAdd(second.start, vrDefenseScale(d2, t));
	const VrImpactVector3 separation = vrDefenseSubtract(firstPoint, secondPoint);
	const float distanceSquared = vrDefenseDot(separation, separation);
	if(!std::isfinite(distanceSquared) || distanceSquared < 0.f) {
		return false;
	}
	distance = std::sqrt(distanceSquared);
	return std::isfinite(distance);
}

class VrDefenseSystem {
public:
	explicit VrDefenseSystem(VrParryConfig parryConfig = {})
		: m_parryConfig(parryConfig) { }

	bool evaluateShieldBlock(const VrShieldProfile & profile,
	                         const VrShieldPose & pose,
	                         const VrIncomingContact & incoming,
	                         VrDefenseEvent & event) {
		event = VrDefenseEvent{};
		if(!vrDefenseShieldProfileValid(profile)
		   || !cooldownReady(incoming.timestampUs, m_lastBlockUs,
		                     m_haveBlock, profile.blockCooldownUs)) {
			return false;
		}

		VrImpactVector3 right{};
		VrImpactVector3 up{};
		VrImpactVector3 normal{};
		if(!vrDefenseBuildShieldBasis(pose, right, up, normal)) {
			return false;
		}

		VrImpactVector3 incomingDirection = incoming.linearVelocity;
		if(!vrDefenseNormalize(incomingDirection, incomingDirection)) {
			incomingDirection = vrDefenseSubtract(incoming.end, incoming.start);
			if(!vrDefenseNormalize(incomingDirection, incomingDirection)) {
				return false;
			}
		}
		const float frontalCosine = -vrDefenseDot(incomingDirection, normal);
		if(!std::isfinite(frontalCosine)
		   || frontalCosine < profile.minimumFrontalCosine) {
			return false;
		}

		float entryT = 0.f;
		if(!vrDefenseSegmentIntersectsShieldBox(incoming, profile, pose,
		                                      right, up, normal, entryT)) {
			return false;
		}

		const VrImpactVector3 delta = vrDefenseSubtract(incoming.end, incoming.start);
		event.type = VrDefenseEventType::ShieldBlock;
		event.position = vrDefenseAdd(incoming.start, vrDefenseScale(delta, entryT));
		event.normal = normal;
		event.relativeSpeed = vrVectorLength(incoming.linearVelocity);
		if(!std::isfinite(event.relativeSpeed)) {
			event.relativeSpeed = 0.f;
		}
		event.angleSin = std::sqrt(std::max(0.f, 1.f - frontalCosine * frontalCosine));
		event.timestampUs = incoming.timestampUs;
		m_lastBlockUs = incoming.timestampUs;
		m_haveBlock = true;
		return true;
	}

	bool evaluateParry(const VrParrySample & sample, VrDefenseEvent & event) {
		event = VrDefenseEvent{};
		if(!vrDefenseParryConfigValid(m_parryConfig)
		   || !cooldownReady(sample.timestampUs, m_lastParryUs,
		                     m_haveParry, m_parryConfig.parryCooldownUs)
		   || !vrVectorFinite(sample.defenderVelocity)
		   || !vrVectorFinite(sample.attackerVelocity)) {
			return false;
		}

		VrImpactVector3 defenderPoint{};
		VrImpactVector3 attackerPoint{};
		float distance = 0.f;
		if(!vrDefenseClosestSegmentPoints(sample.defender, sample.attacker,
		                                defenderPoint, attackerPoint, distance)) {
			return false;
		}
		const float allowedDistance = sample.defender.radius + sample.attacker.radius
		                            + m_parryConfig.contactPadding;
		if(!std::isfinite(allowedDistance) || allowedDistance < 0.f
		   || distance > allowedDistance) {
			return false;
		}

		VrImpactVector3 defenderAxis{};
		VrImpactVector3 attackerAxis{};
		if(!vrDefenseNormalize(vrDefenseSubtract(sample.defender.end,
		                                         sample.defender.start), defenderAxis)
		   || !vrDefenseNormalize(vrDefenseSubtract(sample.attacker.end,
		                                            sample.attacker.start), attackerAxis)) {
			return false;
		}
		const float weaponCrossSin = vrVectorLength(vrDefenseCross(defenderAxis, attackerAxis));
		if(!std::isfinite(weaponCrossSin)
		   || weaponCrossSin < m_parryConfig.minimumWeaponCrossSin) {
			return false;
		}

		const VrImpactVector3 relativeVelocity = vrDefenseSubtract(
			sample.defenderVelocity, sample.attackerVelocity);
		const float relativeSpeed = vrVectorLength(relativeVelocity);
		if(!std::isfinite(relativeSpeed)
		   || relativeSpeed < m_parryConfig.minimumRelativeSpeed) {
			return false;
		}
		VrImpactVector3 relativeDirection{};
		if(!vrDefenseNormalize(relativeVelocity, relativeDirection)) {
			return false;
		}
		const float relativeCrossSin = vrVectorLength(
			vrDefenseCross(relativeDirection, defenderAxis));
		if(!std::isfinite(relativeCrossSin)
		   || relativeCrossSin < m_parryConfig.minimumRelativeCrossSin) {
			return false;
		}

		VrImpactVector3 contactNormal{};
		if(!vrDefenseNormalize(vrDefenseSubtract(defenderPoint, attackerPoint), contactNormal)) {
			if(!vrDefenseNormalize(vrDefenseCross(defenderAxis, attackerAxis), contactNormal)) {
				return false;
			}
		}
		event.type = VrDefenseEventType::WeaponParry;
		event.position = vrDefenseScale(vrDefenseAdd(defenderPoint, attackerPoint), 0.5f);
		event.normal = contactNormal;
		event.relativeSpeed = relativeSpeed;
		event.contactDistance = distance;
		event.angleSin = relativeCrossSin;
		event.timestampUs = sample.timestampUs;
		m_lastParryUs = sample.timestampUs;
		m_haveParry = true;
		return true;
	}

	void resetSession() {
		m_lastBlockUs = 0;
		m_lastParryUs = 0;
		m_haveBlock = false;
		m_haveParry = false;
	}

private:
	static bool cooldownReady(std::uint64_t timestampUs,
	                          std::uint64_t previousUs,
	                          bool havePrevious,
	                          std::uint64_t cooldownUs) {
		if(!havePrevious) {
			return true;
		}
		if(timestampUs < previousUs) {
			return false;
		}
		return timestampUs - previousUs >= cooldownUs;
	}

	VrParryConfig m_parryConfig{};
	std::uint64_t m_lastBlockUs = 0;
	std::uint64_t m_lastParryUs = 0;
	bool m_haveBlock = false;
	bool m_haveParry = false;
};

} // namespace arxvr
