#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "vr/VrBowSystem.h"

namespace arxvr {

struct VrBowRuntimeConfig {
	// A tracked string hand may disappear briefly because of controller/host
	// scheduling, but a long gap must never be bridged into one synthetic draw.
	std::uint64_t maxSampleGapUs = 250000;
	// Current Arx VR world scale is approximately 100 units per metre. Sixty
	// metres/second is intentionally above plausible bow-hand motion while still
	// rejecting controller teleports and stale-pose recovery jumps.
	float maxStringSpeed = 6000.f;
	// Physical travel after the string was actually acquired. The nock-assist
	// radius is deliberately excluded from this value so grabbing the edge of the
	// assist zone cannot become free launch energy.
	float minimumDrawTravel = 18.f;
};

struct VrBowRuntimeRelease {
	VrBowRelease release{};
	float nockSeparation = 0.f;
	float rawHandSeparation = 0.f;
	float drawTravel = 0.f;
	bool valid = false;
};

inline bool vrBowRuntimeConfigValid(const VrBowRuntimeConfig & config) {
	return config.maxSampleGapUs > 0
	    && std::isfinite(config.maxStringSpeed) && config.maxStringSpeed > 0.f
	    && std::isfinite(config.minimumDrawTravel) && config.minimumDrawTravel >= 0.f;
}

// Temporal/runtime guard around VrBowSystem. VrBowSystem owns the physical
// nock/draw geometry; this layer makes the resulting release safe for gameplay
// by removing nock-assist distance from charge and rejecting discontinuous
// tracking before a release can reach Arx projectile code.
class VrBowRuntime {
public:
	explicit VrBowRuntime(VrBowConfig bowConfig = {}, VrBowRuntimeConfig runtimeConfig = {})
		: m_bowConfig(bowConfig), m_runtimeConfig(runtimeConfig), m_system(bowConfig) { }

	VrBowPose update(const VrBowTrackingSample & sample) {
		m_pendingRelease = {};

		if(!vrBowConfigValid(m_bowConfig) || !vrBowRuntimeConfigValid(m_runtimeConfig)) {
			reset();
			return {};
		}

		if(sample.bowToken != m_bowToken) {
			clearGestureState();
			m_bowToken = sample.bowToken;
		}

		// Reject stale/teleported string-hand tracking before VrBowSystem sees the
		// sample. A recovered pose can immediately establish a fresh nock baseline
		// when it is physically back inside the nock zone.
		if(m_system.nocked() && !stringContinuityValid(sample)) {
			m_system.reset();
			clearGestureState();
			m_bowToken = sample.bowToken;
		}

		const bool wasNocked = m_system.nocked();
		VrBowPose pose = m_system.update(sample);
		const bool nowNocked = m_system.nocked();

		if(!wasNocked && nowNocked) {
			const float separation = currentHandSeparation(sample);
			if(std::isfinite(separation)) {
				m_nockSeparation = separation;
				m_haveNockSeparation = true;
			}
		}

		if(nowNocked && sample.stringValid && vrVectorFinite(sample.stringPosition)
		   && sample.timestampUs != 0) {
			m_lastStringPosition = sample.stringPosition;
			m_lastStringTimestampUs = sample.timestampUs;
			m_haveLastStringSample = true;
		}

		VrBowRelease rawRelease;
		if(m_system.consumeRelease(rawRelease)) {
			qualifyRelease(rawRelease);
			clearGestureState();
		} else if(wasNocked && !nowNocked) {
			// Tracking loss, timestamp regression and short/aborted releases all
			// require a completely fresh physical acquisition.
			clearGestureState();
		}

		return pose;
	}

	bool consumeRelease(VrBowRuntimeRelease & release) {
		release = m_pendingRelease;
		const bool valid = m_pendingRelease.valid;
		m_pendingRelease = {};
		return valid;
	}

	bool nocked() const {
		return m_system.nocked();
	}

	std::uint64_t bowToken() const {
		return m_bowToken;
	}

	void reset() {
		m_system.reset();
		m_bowToken = 0;
		m_pendingRelease = {};
		clearGestureState();
	}

private:
	float currentHandSeparation(const VrBowTrackingSample & sample) const {
		if(!sample.bowValid || !sample.stringValid
		   || !vrVectorFinite(sample.bowPosition) || !vrVectorFinite(sample.stringPosition)) {
			return std::numeric_limits<float>::quiet_NaN();
		}
		return vrVectorLength(vrBowSubtract(sample.bowPosition, sample.stringPosition));
	}

	bool stringContinuityValid(const VrBowTrackingSample & sample) const {
		if(!sample.stringValid || !vrVectorFinite(sample.stringPosition) || sample.timestampUs == 0) {
			return false;
		}
		if(!m_haveLastStringSample) {
			return true;
		}
		if(sample.timestampUs <= m_lastStringTimestampUs) {
			return false;
		}

		const std::uint64_t deltaUs = sample.timestampUs - m_lastStringTimestampUs;
		if(deltaUs > m_runtimeConfig.maxSampleGapUs) {
			return false;
		}
		const float seconds = static_cast<float>(deltaUs) * 0.000001f;
		if(!std::isfinite(seconds) || seconds <= 0.f) {
			return false;
		}
		const float distance = vrVectorLength(
			vrBowSubtract(sample.stringPosition, m_lastStringPosition));
		if(!std::isfinite(distance)) {
			return false;
		}
		const float speed = distance / seconds;
		return std::isfinite(speed) && speed <= m_runtimeConfig.maxStringSpeed;
	}

	void qualifyRelease(const VrBowRelease & rawRelease) {
		if(!rawRelease.valid || !m_haveNockSeparation
		   || !std::isfinite(rawRelease.drawDistance)
		   || !std::isfinite(m_nockSeparation)
		   || !vrVectorFinite(rawRelease.origin)
		   || !vrVectorFinite(rawRelease.direction)) {
			return;
		}

		const float rawSeparation = rawRelease.drawDistance;
		const float drawTravel = std::max(0.f, rawSeparation - m_nockSeparation);
		if(!std::isfinite(drawTravel) || drawTravel < m_runtimeConfig.minimumDrawTravel) {
			return;
		}

		const float availableTravel = m_bowConfig.maximumDrawDistance - m_nockSeparation;
		if(!std::isfinite(availableTravel) || availableTravel <= 0.001f) {
			return;
		}

		VrBowRelease adjusted = rawRelease;
		adjusted.drawDistance = drawTravel;
		adjusted.drawRatio = std::clamp(drawTravel / availableTravel, 0.f, 1.f);

		m_pendingRelease.release = adjusted;
		m_pendingRelease.nockSeparation = m_nockSeparation;
		m_pendingRelease.rawHandSeparation = rawSeparation;
		m_pendingRelease.drawTravel = drawTravel;
		m_pendingRelease.valid = true;
	}

	void clearGestureState() {
		m_haveLastStringSample = false;
		m_lastStringTimestampUs = 0;
		m_lastStringPosition = {};
		m_haveNockSeparation = false;
		m_nockSeparation = 0.f;
	}

	VrBowConfig m_bowConfig{};
	VrBowRuntimeConfig m_runtimeConfig{};
	VrBowSystem m_system;
	std::uint64_t m_bowToken = 0;
	bool m_haveLastStringSample = false;
	VrImpactVector3 m_lastStringPosition{};
	std::uint64_t m_lastStringTimestampUs = 0;
	bool m_haveNockSeparation = false;
	float m_nockSeparation = 0.f;
	VrBowRuntimeRelease m_pendingRelease{};
};

} // namespace arxvr
