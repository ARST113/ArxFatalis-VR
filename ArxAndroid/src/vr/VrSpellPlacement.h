#pragma once

#include <cmath>

#include "vr/VrSpellDirection.h"

namespace arxvr {

struct VrSpellPlacement {
	VrSpellVector3 position{};
	VrSpellVector3 direction{};
	float yawDegrees = 0.f;
	bool valid = false;
};

inline bool vrSpellHorizontalPlacement(const VrSpellAimRay & ray, float planeY,
                                       float distance, VrSpellPlacement & placement) {
	placement = {};
	if(!vrSpellFinite(planeY) || !vrSpellFinite(distance) || distance < 0.f) {
		return false;
	}
	VrSpellVector3 horizontal;
	if(!vrSpellHorizontalDirection(ray, horizontal)) {
		return false;
	}
	const VrSpellVector3 position {
		ray.origin.x + horizontal.x * distance,
		planeY,
		ray.origin.z + horizontal.z * distance
	};
	if(!vrSpellFinite(position)) {
		return false;
	}
	constexpr float radiansToDegrees = 57.295779513082320876f;
	const float yawDegrees = std::atan2(-horizontal.x, horizontal.z) * radiansToDegrees;
	if(!vrSpellFinite(yawDegrees)) {
		return false;
	}
	placement.position = position;
	placement.direction = horizontal;
	placement.yawDegrees = yawDegrees;
	placement.valid = true;
	return true;
}

} // namespace arxvr
