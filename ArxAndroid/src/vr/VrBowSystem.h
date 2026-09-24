#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "vr/VrWeaponContact.h"

namespace arxvr {

struct VrBowConfig {
	// Arx world units. The string hand must deliberately grip close to the
	// physical nock anchor before a draw can begin.
	float nockAcquireDistance = 16.f;
	// Draw length is measured directly between the bow-hand anchor and the
	// string hand. It is clamped so tracking spikes cannot create extra energy.
	float maximumDrawDistance = 70.f;
	// Releases below this distance are treated as an aborted nock rather than a
	// shot. Final values are hardware-tuning candidates on PICO.
	float minimumReleaseDraw = 10.f;
	// Arrow origin is placed slightly in front of the bow hand to avoid spawning
	// inside the player/bow geometry when the engine adapter launches it.
	float arrowSpawnOffset = 8.f;
};

struct VrBowTrackingSample {
	std::uint64_t bowToken = 0;
	VrImpactVector3 bowPosition{};
	VrImpactVector3 bowForward{ 0.f, 0.f, 1.f };
	VrImpactVector3 bowUp{ 0.f, 1.f, 0.f };
	bool bowValid = false;

	VrImpactVector3 stringPosition{};
	bool stringValid = false;
	bool stringGripPressed = false;
	std::uint64_t timestampUs = 0;
};

struct VrBowPose {
	VrImpactVector3 bowPosition{};
	VrImpactVector3 aimDirection{ 0.f, 0.f, 1.f };
	VrImpactVector3 up{ 0.f, 1.f, 0.f };
	VrImpactVector3 stringAnchor{};
	VrImpactVector3 arrowOrigin{};
	float drawDistance = 0.f;
	float drawRatio = 0.f;
	bool nocked = false;
	bool valid = false;
};

struct VrBowRelease {
	std::uint64_t bowToken = 0;
	VrImpactVector3 origin{};
	VrImpactVector3 direction{};
	float drawDistance = 0.f;
	float drawRatio = 0.f;
	std::uint64_t timestampUs = 0;
	bool valid = false;
};

inline VrImpactVector3 vrBowAdd(const VrImpactVector3 & a, const VrImpactVector3 & b) {
	return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline VrImpactVector3 vrBowSubtract(const VrImpactVector3 & a, const VrImpactVector3 & b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline VrImpactVector3 vrBowScale(const VrImpactVector3 & value, float scale) {
	return { value.x * scale, value.y * scale, value.z * scale };
}

inline float vrBowDot(const VrImpactVector3 & a, const VrImpactVector3 & b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline VrImpactVector3 vrBowCross(const VrImpactVector3 & a, const VrImpactVector3 & b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

inline bool vrBowNormalize(const VrImpactVector3 & value, VrImpactVector3 & normalized) {
	if(!vrVectorFinite(value)) {
		return false;
	}
	const float length = vrVectorLength(value);
	if(!std::isfinite(length) || length <= 0.0001f) {
		return false;
	}
	normalized = vrBowScale(value, 1.f / length);
	return vrVectorFinite(normalized);
}

inline bool vrBowBuildBasis(const VrImpactVector3 & forwardInput,
                            const VrImpactVector3 & upInput,
                            VrImpactVector3 & forward,
                            VrImpactVector3 & up) {
	if(!vrBowNormalize(forwardInput, forward)) {
		return false;
	}

	VrImpactVector3 candidateUp = upInput;
	if(!vrVectorFinite(candidateUp)) {
		candidateUp = { 0.f, 1.f, 0.f };
	}
	candidateUp = vrBowSubtract(
		candidateUp, vrBowScale(forward, vrBowDot(candidateUp, forward)));
	if(!vrBowNormalize(candidateUp, up)) {
		VrImpactVector3 fallback = std::abs(forward.y) < 0.90f
		                             ? VrImpactVector3{ 0.f, 1.f, 0.f }
		                             : VrImpactVector3{ 1.f, 0.f, 0.f };
		fallback = vrBowSubtract(
			fallback, vrBowScale(forward, vrBowDot(fallback, forward)));
		if(!vrBowNormalize(fallback, up)) {
			return false;
		}
	}

	VrImpactVector3 right{};
	if(!vrBowNormalize(vrBowCross(forward, up), right)) {
		return false;
	}
	return vrBowNormalize(vrBowCross(right, forward), up);
}

inline bool vrBowConfigValid(const VrBowConfig & config) {
	return std::isfinite(config.nockAcquireDistance) && config.nockAcquireDistance >= 0.f
	    && std::isfinite(config.maximumDrawDistance) && config.maximumDrawDistance > 0.f
	    && std::isfinite(config.minimumReleaseDraw) && config.minimumReleaseDraw >= 0.f
	    && config.minimumReleaseDraw <= config.maximumDrawDistance
	    && std::isfinite(config.arrowSpawnOffset) && config.arrowSpawnOffset >= 0.f;
}

// Runtime-neutral physical bow state machine. It owns only hand geometry,
// nocking/draw state and one-shot release qualification; ammunition, player
// stats, projectile damage and animation remain in the Arx gameplay adapter.
class VrBowSystem {
public:
	explicit VrBowSystem(VrBowConfig config = {})
		: m_config(config) { }

	VrBowPose update(const VrBowTrackingSample & sample) {
		m_release = {};

		if(sample.bowToken != m_bowToken) {
			resetNock();
			m_bowToken = sample.bowToken;
		}

		VrBowPose pose;
		if(!vrBowConfigValid(m_config) || sample.bowToken == 0 || sample.timestampUs == 0
		   || !sample.bowValid || !vrVectorFinite(sample.bowPosition)) {
			resetNock();
			return pose;
		}

		VrImpactVector3 forward{};
		VrImpactVector3 up{};
		if(!vrBowBuildBasis(sample.bowForward, sample.bowUp, forward, up)) {
			resetNock();
			return pose;
		}

		pose.valid = true;
		pose.bowPosition = sample.bowPosition;
		pose.aimDirection = forward;
		pose.up = up;
		pose.stringAnchor = sample.bowPosition;
		pose.arrowOrigin = vrBowAdd(sample.bowPosition,
		                           vrBowScale(forward, m_config.arrowSpawnOffset));

		const bool stringUsable = sample.stringValid && vrVectorFinite(sample.stringPosition);
		if(!m_nocked) {
			if(sample.stringGripPressed && stringUsable) {
				const float distance = vrVectorLength(
					vrBowSubtract(sample.stringPosition, pose.stringAnchor));
				if(std::isfinite(distance) && distance <= m_config.nockAcquireDistance) {
					m_nocked = true;
					m_lastTimestampUs = sample.timestampUs;
				}
			}
			pose.nocked = m_nocked;
			return pose;
		}

		// Once nocked, losing either hand's physical tracking cancels the shot.
		// Tracking recovery must acquire the nock again instead of releasing an
		// arrow from stale coordinates.
		if(!stringUsable) {
			resetNock();
			return pose;
		}
		if(sample.timestampUs <= m_lastTimestampUs) {
			resetNock();
			return pose;
		}
		m_lastTimestampUs = sample.timestampUs;

		const VrImpactVector3 stringToBow = vrBowSubtract(sample.bowPosition,
		                                                   sample.stringPosition);
		const float rawDrawDistance = vrVectorLength(stringToBow);
		VrImpactVector3 physicalAim{};
		if(!std::isfinite(rawDrawDistance) || !vrBowNormalize(stringToBow, physicalAim)) {
			resetNock();
			return pose;
		}

		VrImpactVector3 physicalUp{};
		if(!vrBowBuildBasis(physicalAim, sample.bowUp, physicalAim, physicalUp)) {
			resetNock();
			return pose;
		}
		pose.aimDirection = physicalAim;
		pose.up = physicalUp;
		pose.drawDistance = std::min(rawDrawDistance, m_config.maximumDrawDistance);
		pose.drawRatio = std::clamp(pose.drawDistance / m_config.maximumDrawDistance, 0.f, 1.f);
		pose.arrowOrigin = vrBowAdd(sample.bowPosition,
		                           vrBowScale(physicalAim, m_config.arrowSpawnOffset));
		pose.nocked = true;

		if(sample.stringGripPressed) {
			m_lastPose = pose;
			return pose;
		}

		// Grip release emits at most one qualified shot. Short accidental draws
		// simply cancel; the caller does not need a second debounce layer.
		if(pose.drawDistance >= m_config.minimumReleaseDraw) {
			m_release.bowToken = sample.bowToken;
			m_release.origin = pose.arrowOrigin;
			m_release.direction = pose.aimDirection;
			m_release.drawDistance = pose.drawDistance;
			m_release.drawRatio = pose.drawRatio;
			m_release.timestampUs = sample.timestampUs;
			m_release.valid = true;
		}
		resetNock();
		return pose;
	}

	bool consumeRelease(VrBowRelease & release) {
		release = m_release;
		const bool valid = m_release.valid;
		m_release = {};
		return valid;
	}

	bool nocked() const {
		return m_nocked;
	}

	std::uint64_t bowToken() const {
		return m_bowToken;
	}

	void reset() {
		m_bowToken = 0;
		m_release = {};
		resetNock();
	}

private:
	void resetNock() {
		m_nocked = false;
		m_lastTimestampUs = 0;
		m_lastPose = {};
	}

	VrBowConfig m_config{};
	std::uint64_t m_bowToken = 0;
	std::uint64_t m_lastTimestampUs = 0;
	bool m_nocked = false;
	VrBowPose m_lastPose{};
	VrBowRelease m_release{};
};

} // namespace arxvr
