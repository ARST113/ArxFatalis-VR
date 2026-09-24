/*
 * Runtime-neutral physical spell aiming for the standalone VR port.
 */
#pragma once

#include <cmath>

namespace arxvr {

struct VrSpellVector3 {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;
};

struct VrSpellAimSample {
	VrSpellVector3 origin{};
	VrSpellVector3 forward{};
	bool trackingValid = false;
};

struct VrSpellAimRay {
	VrSpellVector3 origin{};
	VrSpellVector3 direction{};
	bool valid = false;
};

inline bool vrSpellFinite(float value) {
	return std::isfinite(value);
}

inline bool vrSpellFinite(const VrSpellVector3 & value) {
	return vrSpellFinite(value.x) && vrSpellFinite(value.y) && vrSpellFinite(value.z);
}

inline float vrSpellLengthSquared(const VrSpellVector3 & value) {
	return value.x * value.x + value.y * value.y + value.z * value.z;
}

inline bool vrSpellNormalizeDirection(const VrSpellVector3 & value,
                                      VrSpellVector3 & normalized) {
	normalized = {};
	if(!vrSpellFinite(value)) {
		return false;
	}
	const float lengthSquared = vrSpellLengthSquared(value);
	if(!vrSpellFinite(lengthSquared) || lengthSquared <= 1.0e-8f) {
		return false;
	}
	const float inverseLength = 1.f / std::sqrt(lengthSquared);
	if(!vrSpellFinite(inverseLength)) {
		return false;
	}
	normalized = { value.x * inverseLength, value.y * inverseLength,
	               value.z * inverseLength };
	if(!vrSpellFinite(normalized)) {
		normalized = {};
		return false;
	}
	return true;
}

class VrSpellAimService {
public:
	VrSpellAimRay update(const VrSpellAimSample & sample) {
		m_ray = {};
		if(!sample.trackingValid || !vrSpellFinite(sample.origin)
		   || !vrSpellFinite(sample.forward)) {
			return m_ray;
		}

		VrSpellVector3 normalized;
		if(!vrSpellNormalizeDirection(sample.forward, normalized)) {
			return m_ray;
		}
		m_ray.origin = sample.origin;
		m_ray.direction = normalized;
		m_ray.valid = true;
		return m_ray;
	}

	void clear() {
		m_ray = {};
	}

	VrSpellAimRay ray() const {
		return m_ray;
	}

private:
	VrSpellAimRay m_ray{};
};

inline VrSpellAimService & vrSpellAimService() {
	static VrSpellAimService service;
	return service;
}

} // namespace arxvr
