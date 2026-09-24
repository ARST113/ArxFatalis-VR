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

class VrSpellAimService {
public:
	VrSpellAimRay update(const VrSpellAimSample & sample) {
		m_ray = {};
		if(!sample.trackingValid || !vrSpellFinite(sample.origin)
		   || !vrSpellFinite(sample.forward)) {
			return m_ray;
		}

		const float lengthSquared = vrSpellLengthSquared(sample.forward);
		if(!vrSpellFinite(lengthSquared) || lengthSquared <= 1.0e-8f) {
			return m_ray;
		}

		const float inverseLength = 1.f / std::sqrt(lengthSquared);
		if(!vrSpellFinite(inverseLength)) {
			return m_ray;
		}

		m_ray.origin = sample.origin;
		m_ray.direction = {
			sample.forward.x * inverseLength,
			sample.forward.y * inverseLength,
			sample.forward.z * inverseLength,
		};
		m_ray.valid = vrSpellFinite(m_ray.direction);
		if(!m_ray.valid) {
			m_ray = {};
		}
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
