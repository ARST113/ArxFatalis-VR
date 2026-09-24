#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "vr/VrWeaponContact.h"

namespace arxvr {

// Runtime-neutral tracked pose used by the physical weapon constraint. The
// engine adapter supplies world-space controller data; this layer deliberately
// stays independent from GLM/OpenXR so it can be deterministic-tested on CI.
struct VrWeaponTrackingSample {
	std::uint64_t weaponToken = 0;
	VrImpactVector3 primaryPosition{};
	VrImpactVector3 primaryForward{};
	VrImpactVector3 primaryUp{ 0.f, 1.f, 0.f };
	bool primaryValid = false;

	VrImpactVector3 secondaryPosition{};
	bool secondaryValid = false;
	bool secondaryGripPressed = false;
};

struct VrWeaponConstraintConfig {
	// Arx world units. Separate engage/release radii provide hysteresis so the
	// off-hand does not chatter between one- and two-handed states at the edge.
	float secondaryEngageDistance = 18.f;
	float secondaryReleaseDistance = 30.f;
	float minimumHandSeparation = 8.f;
};

struct VrWeaponPose {
	VrImpactVector3 position{};
	VrImpactVector3 forward{};
	VrImpactVector3 up{ 0.f, 1.f, 0.f };
	VrImpactVector3 secondaryGripAnchor{};
	bool twoHanded = false;
	bool valid = false;
};

inline VrImpactVector3 vrWeaponVectorAdd(const VrImpactVector3 & a,
                                         const VrImpactVector3 & b) {
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline VrImpactVector3 vrWeaponVectorSubtract(const VrImpactVector3 & a,
                                              const VrImpactVector3 & b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline VrImpactVector3 vrWeaponVectorScale(const VrImpactVector3 & value, float scale) {
	return { value.x * scale, value.y * scale, value.z * scale };
}

inline float vrWeaponVectorDot(const VrImpactVector3 & a, const VrImpactVector3 & b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline VrImpactVector3 vrWeaponVectorCross(const VrImpactVector3 & a,
                                           const VrImpactVector3 & b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

inline bool vrWeaponNormalize(const VrImpactVector3 & value, VrImpactVector3 & normalized) {
	if(!vrVectorFinite(value)) {
		return false;
	}
	const float length = vrVectorLength(value);
	if(!std::isfinite(length) || length <= 0.0001f) {
		return false;
	}
	normalized = vrWeaponVectorScale(value, 1.f / length);
	return vrVectorFinite(normalized);
}

inline bool vrWeaponBuildBasis(const VrImpactVector3 & forwardInput,
                               const VrImpactVector3 & upInput,
                               VrImpactVector3 & forward,
                               VrImpactVector3 & right,
                               VrImpactVector3 & up) {
	if(!vrWeaponNormalize(forwardInput, forward)) {
		return false;
	}

	VrImpactVector3 candidateUp = upInput;
	if(!vrVectorFinite(candidateUp)) {
		candidateUp = { 0.f, 1.f, 0.f };
	}
	candidateUp = vrWeaponVectorSubtract(
		candidateUp, vrWeaponVectorScale(forward, vrWeaponVectorDot(candidateUp, forward)));
	if(!vrWeaponNormalize(candidateUp, up)) {
		// Controller forward can be close to global-up. Pick a deterministic
		// fallback axis and orthogonalise it instead of producing a NaN basis.
		VrImpactVector3 fallback = std::abs(forward.y) < 0.90f
		                             ? VrImpactVector3{ 0.f, 1.f, 0.f }
		                             : VrImpactVector3{ 1.f, 0.f, 0.f };
		fallback = vrWeaponVectorSubtract(
			fallback, vrWeaponVectorScale(forward, vrWeaponVectorDot(fallback, forward)));
		if(!vrWeaponNormalize(fallback, up)) {
			return false;
		}
	}

	if(!vrWeaponNormalize(vrWeaponVectorCross(forward, up), right)) {
		return false;
	}
	// Reconstruct up from the orthonormal pair so accumulated controller-pose
	// error cannot skew local secondary-grip offsets.
	return vrWeaponNormalize(vrWeaponVectorCross(right, forward), up);
}

inline VrImpactVector3 vrWeaponSecondaryGripAnchor(const VrWeaponProfile & profile,
                                                   const VrWeaponPose & primaryPose) {
	VrImpactVector3 right{};
	VrImpactVector3 forward{};
	VrImpactVector3 up{};
	if(!primaryPose.valid
	   || !vrWeaponBuildBasis(primaryPose.forward, primaryPose.up, forward, right, up)) {
		return primaryPose.position;
	}
	return vrWeaponVectorAdd(
		primaryPose.position,
		vrWeaponVectorAdd(
			vrWeaponVectorScale(right, profile.secondaryGripLocal.x),
			vrWeaponVectorAdd(vrWeaponVectorScale(up, profile.secondaryGripLocal.y),
			                  vrWeaponVectorScale(forward, profile.secondaryGripLocal.z))));
}

// Stateful one-/two-handed weapon constraint. Dominant hand remains the pivot;
// when the off-hand deliberately grips close to the profile's secondary anchor,
// weapon direction follows the vector from off-hand to dominant hand as the
// Phase-5 roadmap requires. Hysteresis prevents boundary chatter.
class VrWeaponSystem {
public:
	explicit VrWeaponSystem(VrWeaponConstraintConfig config = {})
		: m_config(sanitizeConfig(config)) { }

	VrWeaponPose update(const VrWeaponProfile & profile,
	                    const VrWeaponTrackingSample & sample) {
		if(sample.weaponToken != m_weaponToken) {
			m_twoHanded = false;
			m_weaponToken = sample.weaponToken;
		}

		VrWeaponPose pose;
		if(sample.weaponToken == 0 || !profile.physicalMelee || !sample.primaryValid
		   || !vrVectorFinite(sample.primaryPosition)) {
			m_twoHanded = false;
			return pose;
		}

		VrImpactVector3 forward{};
		VrImpactVector3 right{};
		VrImpactVector3 up{};
		if(!vrWeaponBuildBasis(sample.primaryForward, sample.primaryUp,
		                       forward, right, up)) {
			m_twoHanded = false;
			return pose;
		}

		pose.position = sample.primaryPosition;
		pose.forward = forward;
		pose.up = up;
		pose.valid = true;
		pose.secondaryGripAnchor = vrWeaponSecondaryGripAnchor(profile, pose);

		const bool secondaryUsable = profile.supportsTwoHand
		                          && sample.secondaryGripPressed
		                          && sample.secondaryValid
		                          && vrVectorFinite(sample.secondaryPosition);
		const float anchorDistance = secondaryUsable
		                           ? vrVectorLength(vrWeaponVectorSubtract(
		                                 sample.secondaryPosition, pose.secondaryGripAnchor))
		                           : 0.f;
		const float handSeparation = secondaryUsable
		                           ? vrVectorLength(vrWeaponVectorSubtract(
		                                 sample.primaryPosition, sample.secondaryPosition))
		                           : 0.f;
		const bool geometryUsable = secondaryUsable
		                         && std::isfinite(anchorDistance)
		                         && std::isfinite(handSeparation)
		                         && handSeparation >= m_config.minimumHandSeparation;

		if(m_twoHanded) {
			if(!geometryUsable || anchorDistance > m_config.secondaryReleaseDistance) {
				m_twoHanded = false;
			}
		} else if(geometryUsable && anchorDistance <= m_config.secondaryEngageDistance) {
			m_twoHanded = true;
		}

		if(m_twoHanded) {
			VrImpactVector3 constrainedForward{};
			if(!vrWeaponNormalize(vrWeaponVectorSubtract(
			       sample.primaryPosition, sample.secondaryPosition), constrainedForward)) {
				m_twoHanded = false;
			} else {
				pose.forward = constrainedForward;
				VrImpactVector3 constrainedRight{};
				VrImpactVector3 constrainedUp{};
				if(vrWeaponBuildBasis(pose.forward, sample.primaryUp,
				                      constrainedForward, constrainedRight, constrainedUp)) {
					pose.forward = constrainedForward;
					pose.up = constrainedUp;
				}
			}
		}

		pose.twoHanded = m_twoHanded;
		return pose;
	}

	VrWeaponSegment buildContactSegment(const VrWeaponProfile & profile,
	                                    const VrWeaponPose & pose) const {
		if(!pose.valid) {
			return {};
		}
		return vrBuildWeaponSegment(profile, pose.position, pose.forward);
	}

	bool twoHanded() const {
		return m_twoHanded;
	}

	std::uint64_t weaponToken() const {
		return m_weaponToken;
	}

	void reset() {
		m_twoHanded = false;
		m_weaponToken = 0;
	}

private:
	static VrWeaponConstraintConfig sanitizeConfig(VrWeaponConstraintConfig config) {
		if(!std::isfinite(config.secondaryEngageDistance)
		   || config.secondaryEngageDistance < 0.f) {
			config.secondaryEngageDistance = 18.f;
		}
		if(!std::isfinite(config.secondaryReleaseDistance)
		   || config.secondaryReleaseDistance < config.secondaryEngageDistance) {
			config.secondaryReleaseDistance = std::max(30.f,
			                                           config.secondaryEngageDistance);
		}
		if(!std::isfinite(config.minimumHandSeparation)
		   || config.minimumHandSeparation < 0.f) {
			config.minimumHandSeparation = 8.f;
		}
		return config;
	}

	VrWeaponConstraintConfig m_config{};
	std::uint64_t m_weaponToken = 0;
	bool m_twoHanded = false;
};

} // namespace arxvr
