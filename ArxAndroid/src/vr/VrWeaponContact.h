#pragma once

#include <algorithm>
#include <cmath>

#include "vr/VrWeaponProfile.h"

namespace arxvr {

struct VrWeaponSegment {
	VrImpactVector3 start{};
	VrImpactVector3 end{};
	float radius = 0.f;
	bool valid = false;
};

inline float vrVectorLength(const VrImpactVector3 & value) {
	return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

inline bool vrVectorFinite(const VrImpactVector3 & value) {
	return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// Build the physical damaging segment from a tracked grip/aim pose. Controller
// and named-weapon alignment offsets are profile data and will be applied by the
// engine adapter before it supplies the forward vector; this layer stays free
// of GLM/OpenXR and only owns contact geometry.
inline VrWeaponSegment vrBuildWeaponSegment(const VrWeaponProfile & profile,
                                            const VrImpactVector3 & gripPosition,
                                            const VrImpactVector3 & forward) {
	VrWeaponSegment result;
	if(!profile.physicalMelee || profile.reach <= 0.f || profile.contactRadius < 0.f
	   || !std::isfinite(profile.reach) || !std::isfinite(profile.contactRadius)
	   || !vrVectorFinite(gripPosition) || !vrVectorFinite(forward)) {
		return result;
	}

	const float forwardLength = vrVectorLength(forward);
	if(!std::isfinite(forwardLength) || forwardLength <= 0.0001f) {
		return result;
	}
	const float inverseLength = 1.f / forwardLength;
	const VrImpactVector3 unitForward {
		forward.x * inverseLength,
		forward.y * inverseLength,
		forward.z * inverseLength
	};

	result.start = gripPosition;
	result.end = {
		gripPosition.x + unitForward.x * profile.reach,
		gripPosition.y + unitForward.y * profile.reach,
		gripPosition.z + unitForward.z * profile.reach
	};
	result.radius = profile.contactRadius;
	result.valid = vrVectorFinite(result.end);
	return result;
}

inline VrImpactVector3 vrWeaponSegmentPoint(const VrWeaponSegment & segment, float t) {
	const float clamped = std::clamp(t, 0.f, 1.f);
	return {
		segment.start.x + (segment.end.x - segment.start.x) * clamped,
		segment.start.y + (segment.end.y - segment.start.y) * clamped,
		segment.start.z + (segment.end.z - segment.start.z) * clamped
	};
}

// Slab-test the weapon segment against an AABB expanded by the weapon contact
// radius. entryT is the first normalized segment position entering the expanded
// bounds and can be converted into a world-space contact estimate.
inline bool vrWeaponSegmentIntersectsAabb(const VrWeaponSegment & segment,
                                          const VrImpactVector3 & boundsMin,
                                          const VrImpactVector3 & boundsMax,
                                          float & entryT) {
	entryT = 0.f;
	if(!segment.valid || !vrVectorFinite(boundsMin) || !vrVectorFinite(boundsMax)
	   || boundsMin.x > boundsMax.x || boundsMin.y > boundsMax.y
	   || boundsMin.z > boundsMax.z || !std::isfinite(segment.radius)
	   || segment.radius < 0.f) {
		return false;
	}

	const float expandedMin[3] = {
		boundsMin.x - segment.radius,
		boundsMin.y - segment.radius,
		boundsMin.z - segment.radius
	};
	const float expandedMax[3] = {
		boundsMax.x + segment.radius,
		boundsMax.y + segment.radius,
		boundsMax.z + segment.radius
	};
	const float start[3] = { segment.start.x, segment.start.y, segment.start.z };
	const float delta[3] = {
		segment.end.x - segment.start.x,
		segment.end.y - segment.start.y,
		segment.end.z - segment.start.z
	};

	float tMin = 0.f;
	float tMax = 1.f;
	for(int axis = 0; axis < 3; ++axis) {
		if(std::abs(delta[axis]) <= 0.000001f) {
			if(start[axis] < expandedMin[axis] || start[axis] > expandedMax[axis]) {
				return false;
			}
			continue;
		}

		const float inverseDelta = 1.f / delta[axis];
		float nearT = (expandedMin[axis] - start[axis]) * inverseDelta;
		float farT = (expandedMax[axis] - start[axis]) * inverseDelta;
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

} // namespace arxvr
