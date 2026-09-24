#pragma once

#include <cmath>
#include <cstdint>

#include "vr/VrImpact.h"

namespace arxvr {

// Compatibility name used by the current ArxGame integration. The underlying
// payload is now the common semantic impact event that the reusable interaction
// service also emits.
using VrQualifiedImpact = VrImpactEvent;

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

// Owns impact qualification and terminal tracked kinematics for one physical
// hand. One hand keeps one cooldown/retraction state across fist/object/weapon
// source transitions; source boundaries discard only motion that must not be
// banked into the next semantic gesture.
class VrHandState {
public:
	VrHandState()
		: m_gate(vrFistStrikeProfile()) { }

	VrImpactGateStatus update(const VrImpactSample & sample) {
		const bool profileChanged = sample.source != m_profileSource;
		if(profileChanged) {
			m_gate.transitionProfile(vrImpactProfileForSource(sample.source));
			m_profileSource = sample.source;
		}

		const bool sourceChanged = sample.source != m_kinematicSource
		                        || sample.sourceToken != m_kinematicSourceToken;
		const bool gestureStarted = sample.gestureActive && !m_kinematicGestureActive;
		if(sourceChanged || gestureStarted) {
			m_haveTerminalVelocity = false;
		}

		if(sample.trackingValid) {
			updateTerminalKinematics(sample.motion, sourceChanged || gestureStarted);
			m_latestMotion = sample.motion;
			m_haveLatestMotion = true;
		} else {
			m_haveLatestMotion = false;
			m_haveTerminalVelocity = false;
		}

		const VrImpactGateStatus status = m_gate.update(sample);
		if(status == VrImpactGateStatus::Invalid
		   || status == VrImpactGateStatus::TrackingReset) {
			m_haveTerminalVelocity = false;
		}

		m_kinematicSource = sample.source;
		m_kinematicSourceToken = sample.sourceToken;
		m_kinematicGestureActive = sample.gestureActive && sample.trackingValid;
		return status;
	}

	bool canImpact() const {
		return m_gate.canImpact();
	}

	bool consumeQualifiedImpact(VrQualifiedImpact & impact) {
		if(!m_gate.canImpact() || !m_haveLatestMotion) {
			return false;
		}

		VrQualifiedImpact candidate;
		candidate.type = vrImpactTypeForSource(m_gate.source());
		candidate.source = m_gate.source();
		candidate.sourceToken = m_gate.sourceToken();
		candidate.motion = m_latestMotion;
		candidate.metrics = m_gate.metrics();
		candidate.position = { m_latestMotion.x, m_latestMotion.y, m_latestMotion.z };
		if(m_haveTerminalVelocity) {
			candidate.linearVelocity = m_terminalVelocity;
			const float speed = vectorLength(m_terminalVelocity);
			if(speed > 0.0001f) {
				candidate.direction = {
					m_terminalVelocity.x / speed,
					m_terminalVelocity.y / speed,
					m_terminalVelocity.z / speed
				};
			}
		}
		if(candidate.source == VrImpactSource::HeldObject
		   || candidate.source == VrImpactSource::EquippedWeapon
		   || candidate.source == VrImpactSource::Shield) {
			candidate.weaponToken = candidate.sourceToken;
		}

		if(!m_gate.consumeImpact()) {
			return false;
		}
		impact = candidate;
		return true;
	}

	void resetGesture() {
		m_gate.resetGesture();
		m_haveTerminalVelocity = false;
		m_kinematicGestureActive = false;
	}

	void resetSession() {
		m_gate.transitionProfile(vrFistStrikeProfile());
		m_gate.resetSession();
		m_profileSource = VrImpactSource::None;
		m_kinematicSource = VrImpactSource::None;
		m_kinematicSourceToken = 0;
		m_kinematicGestureActive = false;
		m_latestMotion = VrMotionSample{};
		m_terminalVelocity = VrImpactVector3{};
		m_haveLatestMotion = false;
		m_haveTerminalVelocity = false;
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
	static float vectorLength(const VrImpactVector3 & vector) {
		return std::sqrt(vector.x * vector.x + vector.y * vector.y
		                 + vector.z * vector.z);
	}

	void updateTerminalKinematics(const VrMotionSample & motion, bool resetSegment) {
		if(resetSegment || !m_haveLatestMotion
		   || motion.timestampUs <= m_latestMotion.timestampUs) {
			m_haveTerminalVelocity = false;
			return;
		}

		const std::uint64_t deltaUs = motion.timestampUs - m_latestMotion.timestampUs;
		const float deltaSeconds = static_cast<float>(deltaUs) * 0.000001f;
		if(deltaSeconds <= 0.f || !std::isfinite(deltaSeconds)) {
			m_haveTerminalVelocity = false;
			return;
		}

		VrImpactVector3 velocity {
			(motion.x - m_latestMotion.x) / deltaSeconds,
			(motion.y - m_latestMotion.y) / deltaSeconds,
			(motion.z - m_latestMotion.z) / deltaSeconds
		};
		if(!std::isfinite(velocity.x) || !std::isfinite(velocity.y)
		   || !std::isfinite(velocity.z)) {
			m_haveTerminalVelocity = false;
			return;
		}
		m_terminalVelocity = velocity;
		m_haveTerminalVelocity = true;
	}

	VrImpactGate m_gate;
	VrImpactSource m_profileSource = VrImpactSource::None;
	VrImpactSource m_kinematicSource = VrImpactSource::None;
	std::uint64_t m_kinematicSourceToken = 0;
	bool m_kinematicGestureActive = false;
	VrMotionSample m_latestMotion{};
	VrImpactVector3 m_terminalVelocity{};
	bool m_haveLatestMotion = false;
	bool m_haveTerminalVelocity = false;
};

} // namespace arxvr
