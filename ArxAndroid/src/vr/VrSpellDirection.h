#pragma once

#include "vr/VrSpellAim.h"

namespace arxvr {

inline bool vrSpellHorizontalDirection(const VrSpellAimRay & ray,
                                       VrSpellVector3 & horizontal) {
	horizontal = {};
	if(!ray.valid || !vrSpellFinite(ray.direction)) {
		return false;
	}
	return vrSpellNormalizeDirection(
		{ ray.direction.x, 0.f, ray.direction.z }, horizontal);
}

} // namespace arxvr
