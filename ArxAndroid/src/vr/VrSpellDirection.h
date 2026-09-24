#pragma once

#include "vr/VrSpellAim.h"

namespace arxvr {

inline bool vrSpellHorizontalDirection(const VrSpellAimRay & ray,
                                       VrSpellVector3 & horizontal) {
	horizontal = {};
	if(!ray.valid || !vrSpellFinite(ray.direction)) {
		return false;
	}
	const VrSpellVector3 value { ray.direction.x, 0.f, ray.direction.z };
	const float lengthSquared = vrSpellLengthSquared(value);
	if(!vrSpellFinite(lengthSquared) || lengthSquared <= 1.0e-8f) {
		return false;
	}
	const float inverseLength = 1.f / std::sqrt(lengthSquared);
	if(!vrSpellFinite(inverseLength)) {
		return false;
	}
	horizontal = { value.x * inverseLength, 0.f, value.z * inverseLength };
	return vrSpellFinite(horizontal);
}

} // namespace arxvr
