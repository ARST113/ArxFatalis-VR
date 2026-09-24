#pragma once

#include <cstdint>

#include "vr/VrStrikeClassifier.h"

namespace arxvr {

// Semantic source of a physical contact. The gate deliberately stays free of
// Arx entity/physics types so controller tracking can be filtered and tested
// without booting the engine or OpenXR runtime.
enum class VrImpactSource : std::uint8_t {
	None = 0,
	Fist,
	HeldObject,
	EquippedWeapon,
	Shield,
	Projectile
};

enum class VrImpactGateStatus : std::uint8_t {
	Inactive,
	Building,
	Qualified,
	TrackingReset,
	Invalid
};

struct VrImpactSample {
	VrMotionSample motion{};
	VrImpactSource source = VrImpactSource::None;
	// Stable identity inside a source category. For held/equipped items this can
	// be an entity handle converted to an integer; fists can use zero.
	std::uint64_t sourceToken = 0;
	bool gestureActive = false;
	bool trackingValid = true;
};

// Turns raw tracked motion into a single semantic impact opportunity while
// preserving cooldown/rearm state across gesture and weapon boundaries.
//
// Important invariants:
//  * opening/reclosing a fist cannot reuse motion gathered before the close;
//  * swapping held/equipped objects cannot bypass an already active cooldown;
//  * lost tracking fails closed and requires the normal slow rearm;
//  * motion while a gesture is inactive is still observed, allowing a player
//    to satisfy the neutral/slow rearm naturally before beginning a new swing.
class VrImpactGate {
public:
	explicit VrImpactGate(const VrStrikeProfile & profile)
		: m_profile(profile) { }

	VrImpactGateStatus update(const VrImpactSample & sample) {
		if(!sample.trackingValid) {
			m_classifier.invalidateTracking(sample.motion.timestampUs, m_profile);
			m_gestureActive = false;
			m_source = sample.source;
			m_sourceToken = sample.sourceToken;
			return VrImpactGateStatus::Invalid;
		}

		const bool sourceChanged = sample.source != m_source
		                        || sample.sourceToken != m_sourceToken;
		const bool gestureStarted = sample.gestureActive && !m_gestureActive;
		if(sourceChanged || gestureStarted) {
			// Preserve armed/cooldown state. Only the motion path belongs to the
			// previous source/gesture and must be discarded.
			m_classifier.resetHistory();
		}

		const VrMotionSampleStatus motionStatus =
			m_classifier.update(sample.motion, m_profile);
		m_source = sample.source;
		m_sourceToken = sample.sourceToken;
		m_gestureActive = sample.gestureActive;

		if(motionStatus == VrMotionSampleStatus::Invalid) {
			m_gestureActive = false;
			return VrImpactGateStatus::Invalid;
		}
		if(motionStatus == VrMotionSampleStatus::TrackingReset) {
			m_gestureActive = false;
			return VrImpactGateStatus::TrackingReset;
		}
		if(!m_gestureActive || m_source == VrImpactSource::None) {
			return VrImpactGateStatus::Inactive;
		}
		return m_classifier.canStrike(m_profile)
		     ? VrImpactGateStatus::Qualified
		     : VrImpactGateStatus::Building;
	}

	bool canImpact() const {
		return m_gestureActive
		    && m_source != VrImpactSource::None
		    && m_classifier.canStrike(m_profile);
	}

	bool consumeImpact() {
		return canImpact() && m_classifier.consumeStrike(m_profile);
	}

	// End the current semantic gesture/source while keeping cooldown state.
	// This is suitable for release/drop/holster transitions.
	void resetGesture() {
		m_classifier.resetHistory();
		m_gestureActive = false;
		m_source = VrImpactSource::None;
		m_sourceToken = 0;
	}

	// Full runtime/level reset. This intentionally restores the initial armed
	// state and therefore must not be used to transition between normal attacks.
	void resetSession() {
		m_classifier.clear();
		m_gestureActive = false;
		m_source = VrImpactSource::None;
		m_sourceToken = 0;
	}

	const VrStrikeMetrics & metrics() const {
		return m_classifier.metrics();
	}

	const VrStrikeProfile & profile() const {
		return m_profile;
	}

	bool armed() const {
		return m_classifier.armed();
	}

	std::uint64_t rearmNotBeforeUs() const {
		return m_classifier.rearmNotBeforeUs();
	}

	bool gestureActive() const {
		return m_gestureActive;
	}

	VrImpactSource source() const {
		return m_source;
	}

	std::uint64_t sourceToken() const {
		return m_sourceToken;
	}

private:
	VrStrikeProfile m_profile;
	VrStrikeClassifier m_classifier;
	VrImpactSource m_source = VrImpactSource::None;
	std::uint64_t m_sourceToken = 0;
	bool m_gestureActive = false;
};

} // namespace arxvr
