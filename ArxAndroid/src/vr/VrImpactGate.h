#pragma once

#include <cmath>
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
//  * a consumed impact requires physical retraction from the contact point;
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
			m_haveLatestMotion = false;
			return VrImpactGateStatus::Invalid;
		}

		m_latestMotion = sample.motion;
		m_haveLatestMotion = true;

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
			m_haveLatestMotion = false;
			return VrImpactGateStatus::Invalid;
		}
		if(motionStatus == VrMotionSampleStatus::TrackingReset) {
			m_gestureActive = false;
			return VrImpactGateStatus::TrackingReset;
		}

		updateRetractionBoundary(sample.motion);
		if(!m_gestureActive || m_source == VrImpactSource::None) {
			return VrImpactGateStatus::Inactive;
		}
		return canImpact() ? VrImpactGateStatus::Qualified
		                   : VrImpactGateStatus::Building;
	}

	bool canImpact() const {
		return m_rearmDistanceSatisfied
		    && m_gestureActive
		    && m_source != VrImpactSource::None
		    && m_classifier.canStrike(m_profile);
	}

	bool consumeImpact() {
		if(!canImpact() || !m_haveLatestMotion) {
			return false;
		}
		if(!m_classifier.consumeStrike(m_profile)) {
			return false;
		}

		m_lastImpactMotion = m_latestMotion;
		m_haveLastImpactMotion = true;
		m_requiredRearmDistance = m_profile.minRearmDistance > 0.f
		                        ? m_profile.minRearmDistance : 0.f;
		m_rearmDistanceSatisfied = m_requiredRearmDistance == 0.f;
		return true;
	}

	// Change the qualification profile when one physical hand changes semantic
	// source (for example fist -> held object) without erasing a cooldown that
	// was established by the previous source. Motion metrics cannot be compared
	// across profiles, so the current path is always discarded. If an impact is
	// still awaiting physical retraction, its original distance requirement is
	// retained instead of being replaced by the new source profile.
	void transitionProfile(const VrStrikeProfile & profile) {
		m_profile = profile;
		m_classifier.resetHistory();
	}

	// End the current semantic gesture/source while keeping cooldown and any
	// outstanding physical-retraction requirement.
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
		m_latestMotion = VrMotionSample{};
		m_lastImpactMotion = VrMotionSample{};
		m_haveLatestMotion = false;
		m_haveLastImpactMotion = false;
		m_requiredRearmDistance = 0.f;
		m_rearmDistanceSatisfied = true;
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

	bool rearmDistanceSatisfied() const {
		return m_rearmDistanceSatisfied;
	}

	float requiredRearmDistance() const {
		return m_requiredRearmDistance;
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
	static float motionDistance(const VrMotionSample & a,
	                            const VrMotionSample & b) {
		const float dx = b.x - a.x;
		const float dy = b.y - a.y;
		const float dz = b.z - a.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	void updateRetractionBoundary(const VrMotionSample & sample) {
		if(m_rearmDistanceSatisfied || !m_haveLastImpactMotion) {
			return;
		}
		const float distanceFromImpact = motionDistance(m_lastImpactMotion, sample);
		if(!std::isfinite(distanceFromImpact)
		   || distanceFromImpact < m_requiredRearmDistance) {
			return;
		}

		// Establish the retracted pose as a new motion baseline. This prevents
		// the movement used merely to leave the previous contact point from being
		// banked as the first half of the next damaging swing.
		m_rearmDistanceSatisfied = true;
		m_classifier.resetHistory();
		(void)m_classifier.update(sample, m_profile);
	}

	VrStrikeProfile m_profile;
	VrStrikeClassifier m_classifier;
	VrImpactSource m_source = VrImpactSource::None;
	std::uint64_t m_sourceToken = 0;
	bool m_gestureActive = false;
	VrMotionSample m_latestMotion{};
	VrMotionSample m_lastImpactMotion{};
	bool m_haveLatestMotion = false;
	bool m_haveLastImpactMotion = false;
	float m_requiredRearmDistance = 0.f;
	bool m_rearmDistanceSatisfied = true;
};

} // namespace arxvr
