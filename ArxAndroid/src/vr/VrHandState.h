#pragma once

#include <cstdint>

#include "vr/VrImpactGate.h"

namespace arxvr {

// Snapshot returned when one hand has built a physically credible impact. It
// intentionally contains only transport-independent motion data. Arx-specific
// target selection, damage, scripts, sounds and haptics remain downstream.
struct VrQualifiedImpact {
	VrImpactSource source = VrImpactSource::None;
	std::uint64_t sourceToken = 0;
	VrMotionSample motion{};
	VrStrikeMetrics metrics{};
};

inline constexpr VrStrikeProfile vrImpactProfileForSource(VrImpactSource source) {
	switch(source) {
		case VrImpactSource::Fist:
			return vrFistStrikeProfile();
		case VrImpactSource::HeldObject:
		case VrImpactSource::EquippedWeapon:
		case VrImpactSource::Shield:
		case VrImpactSource::Projectile:
			return vrHeldObjectStrikeProfile();
		case VrImpactSource::None:
			return vrFistStrikeProfile();
	}
	return vrFistStrikeProfile();
}

// Owns the impact qualification state for one physical hand. Keeping exactly
// one gate per hand is important: changing from a fist to a grabbed/equipped
// object must not provide a fresh cooldown simply because the source category
// changed. The active source can still use its own strike profile.
class VrHandState {
public:
	VrHandState()
		: m_gate(vrFistStrikeProfile()) { }

	VrImpactGateStatus update(const VrImpactSample & sample) {
		if(sample.source != m_profileSource) {
			m_gate.transitionProfile(vrImpactProfileForSource(sample.source));
			m_profileSource = sample.source;
		}
		if(sample.trackingValid) {
			m_latestMotion = sample.motion;
		}
		return m_gate.update(sample);
	}

	bool canImpact() const {
		return m_gate.canImpact();
	}

	bool consumeQualifiedImpact(VrQualifiedImpact & impact) {
		if(!m_gate.canImpact()) {
			return false;
		}

		VrQualifiedImpact candidate;
		candidate.source = m_gate.source();
		candidate.sourceToken = m_gate.sourceToken();
		candidate.motion = m_latestMotion;
		candidate.metrics = m_gate.metrics();
		if(!m_gate.consumeImpact()) {
			return false;
		}
		impact = candidate;
		return true;
	}

	void resetGesture() {
		m_gate.resetGesture();
	}

	void resetSession() {
		m_gate.transitionProfile(vrFistStrikeProfile());
		m_gate.resetSession();
		m_profileSource = VrImpactSource::None;
		m_latestMotion = VrMotionSample{};
	}

	const VrStrikeMetrics & metrics() const {
		return m_gate.metrics();
	}

	const VrStrikeProfile & profile() const {
		return m_gate.profile();
	}

	VrImpactSource source() const {
		return m_gate.source();
	}

	std::uint64_t sourceToken() const {
		return m_gate.sourceToken();
	}

	bool armed() const {
		return m_gate.armed();
	}

	std::uint64_t rearmNotBeforeUs() const {
		return m_gate.rearmNotBeforeUs();
	}

private:
	VrImpactGate m_gate;
	VrImpactSource m_profileSource = VrImpactSource::None;
	VrMotionSample m_latestMotion{};
};

} // namespace arxvr
