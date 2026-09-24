#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "vr/VrDefenseSystem.h"

namespace arxvr {

struct VrDefenseRuntimeConfig {
	// Tracked defensive poses are refreshed every gameplay frame. Keep a small
	// grace period for scheduling jitter while failing closed on stale input.
	std::uint64_t maxShieldAgeUs = 120000;
	std::uint64_t maxDefenderWeaponAgeUs = 120000;
	// NPC animation/action-point samples separated by a long stall must not be
	// joined into one artificial weapon sweep.
	std::uint64_t maxIncomingGapUs = 250000;
	// Defender samples use the same discontinuity rule so a controller teleport
	// cannot become a synthetic parry velocity.
	std::uint64_t maxDefenderWeaponGapUs = 250000;
	// 60 m/s in the current 100 Arx-units-per-metre scale. These ceilings are
	// deliberately above plausible melee motion while rejecting teleports.
	float maxIncomingSpeed = 6000.f;
	float maxDefenderWeaponSpeed = 6000.f;
	// Once one physical contact has defended an NPC weapon strike, suppress the
	// rest of that same short strike window. This prevents multiple action points
	// or repeated Strike_Check calls from leaking damage after a valid defense.
	std::uint64_t defenseLatchUs = 300000;
};

struct VrPublishedShield {
	std::uint64_t token = 0;
	VrShieldProfile profile{};
	VrShieldPose pose{};
	std::uint64_t timestampUs = 0;
	bool active = false;
};

struct VrPublishedDefenderWeapon {
	std::uint64_t token = 0;
	VrWeaponSegment segment{};
	VrImpactVector3 linearVelocity{};
	std::uint64_t timestampUs = 0;
	bool active = false;
};

inline std::uint64_t vrDefenseNowMicros() {
	const auto count = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	return count > 0 ? static_cast<std::uint64_t>(count) : 1u;
}

class VrDefenseRuntime {
public:
	explicit VrDefenseRuntime(VrDefenseRuntimeConfig config = {})
		: m_config(config) { }

	void publishShield(std::uint64_t token,
	                   const VrShieldProfile & profile,
	                   const VrShieldPose & pose,
	                   std::uint64_t timestampUs) {
		VrImpactVector3 right{};
		VrImpactVector3 up{};
		VrImpactVector3 normal{};
		if(token == 0 || timestampUs == 0
		   || !runtimeConfigValid()
		   || !vrDefenseShieldProfileValid(profile)
		   || !vrDefenseBuildShieldBasis(pose, right, up, normal)) {
			clearShield();
			return;
		}

		m_shield.token = token;
		m_shield.profile = profile;
		m_shield.pose = pose;
		m_shield.timestampUs = timestampUs;
		m_shield.active = true;
	}

	void clearShield() {
		// Deliberately retain VrDefenseSystem cooldown state. Unequipping and
		// immediately re-equipping a shield must not bypass block debounce.
		m_shield = VrPublishedShield{};
	}

	bool shieldActiveAt(std::uint64_t timestampUs) const {
		if(!m_shield.active || timestampUs == 0
		   || timestampUs < m_shield.timestampUs) {
			return false;
		}
		return timestampUs - m_shield.timestampUs <= m_config.maxShieldAgeUs;
	}

	const VrPublishedShield & shield() const {
		return m_shield;
	}

	void publishDefenderWeapon(std::uint64_t token,
	                           const VrWeaponSegment & segment,
	                           std::uint64_t timestampUs) {
		if(token == 0 || timestampUs == 0 || !runtimeConfigValid()
		   || !weaponSegmentValid(segment)) {
			clearDefenderWeapon();
			return;
		}

		VrImpactVector3 velocity{};
		if(m_defenderWeapon.active && m_defenderWeapon.token == token) {
			if(timestampUs <= m_defenderWeapon.timestampUs) {
				clearDefenderWeapon();
				return;
			}

			const std::uint64_t deltaUs = timestampUs - m_defenderWeapon.timestampUs;
			if(deltaUs <= m_config.maxDefenderWeaponGapUs) {
				const float seconds = static_cast<float>(deltaUs) * 0.000001f;
				if(!std::isfinite(seconds) || seconds <= 0.f) {
					clearDefenderWeapon();
					return;
				}

				const VrImpactVector3 startVelocity = vrDefenseScale(
					vrDefenseSubtract(segment.start, m_defenderWeapon.segment.start),
					1.f / seconds);
				const VrImpactVector3 endVelocity = vrDefenseScale(
					vrDefenseSubtract(segment.end, m_defenderWeapon.segment.end),
					1.f / seconds);
				const float startSpeed = vrVectorLength(startVelocity);
				const float endSpeed = vrVectorLength(endVelocity);
				if(!vrVectorFinite(startVelocity) || !vrVectorFinite(endVelocity)
				   || !std::isfinite(startSpeed) || !std::isfinite(endSpeed)
				   || startSpeed > m_config.maxDefenderWeaponSpeed
				   || endSpeed > m_config.maxDefenderWeaponSpeed) {
					// Fail closed for this frame. The next valid publication establishes a
					// fresh baseline at the recovered tracked pose.
					clearDefenderWeapon();
					return;
				}
				velocity = endSpeed >= startSpeed ? endVelocity : startVelocity;
			}
		}

		m_defenderWeapon.token = token;
		m_defenderWeapon.segment = segment;
		m_defenderWeapon.linearVelocity = velocity;
		m_defenderWeapon.timestampUs = timestampUs;
		m_defenderWeapon.active = true;
	}

	void clearDefenderWeapon() {
		// As with shields, clearing tracked geometry must not reset parry debounce.
		m_defenderWeapon = VrPublishedDefenderWeapon{};
	}

	bool defenderWeaponActiveAt(std::uint64_t timestampUs) const {
		if(!m_defenderWeapon.active || timestampUs == 0
		   || timestampUs < m_defenderWeapon.timestampUs) {
			return false;
		}
		return timestampUs - m_defenderWeapon.timestampUs
		    <= m_config.maxDefenderWeaponAgeUs;
	}

	const VrPublishedDefenderWeapon & defenderWeapon() const {
		return m_defenderWeapon;
	}

	bool sampleIncomingWeapon(std::uint64_t sourceToken,
	                          std::uint64_t actionToken,
	                          const VrImpactVector3 & position,
	                          std::uint64_t timestampUs,
	                          VrIncomingContact & contact) {
		contact = VrIncomingContact{};
		if(sourceToken == 0 || actionToken == 0 || timestampUs == 0
		   || !runtimeConfigValid() || !vrVectorFinite(position)) {
			return false;
		}

		IncomingHistory & history = findOrAllocateHistory(sourceToken, actionToken);
		if(!history.valid) {
			storeHistory(history, sourceToken, actionToken, position, timestampUs);
			return false;
		}

		if(timestampUs <= history.timestampUs) {
			// Treat clock regressions/duplicates as a discontinuity and establish a
			// fresh baseline rather than synthesising a zero/negative-duration hit.
			storeHistory(history, sourceToken, actionToken, position, timestampUs);
			return false;
		}

		const std::uint64_t deltaUs = timestampUs - history.timestampUs;
		if(deltaUs > m_config.maxIncomingGapUs) {
			storeHistory(history, sourceToken, actionToken, position, timestampUs);
			return false;
		}

		const VrImpactVector3 displacement = vrDefenseSubtract(position, history.position);
		const float distance = vrVectorLength(displacement);
		if(!std::isfinite(distance)) {
			storeHistory(history, sourceToken, actionToken, position, timestampUs);
			return false;
		}
		if(distance <= 0.001f) {
			// Preserve the previous meaningful sample. Strike_Check may be invoked
			// repeatedly at the same animation pose; advancing the timestamp here
			// would dilute the velocity of the next actual motion sample.
			return false;
		}

		const float seconds = static_cast<float>(deltaUs) * 0.000001f;
		if(!std::isfinite(seconds) || seconds <= 0.f) {
			storeHistory(history, sourceToken, actionToken, position, timestampUs);
			return false;
		}
		const VrImpactVector3 velocity = vrDefenseScale(displacement, 1.f / seconds);
		const float speed = vrVectorLength(velocity);
		if(!vrVectorFinite(velocity) || !std::isfinite(speed)
		   || speed > m_config.maxIncomingSpeed) {
			storeHistory(history, sourceToken, actionToken, position, timestampUs);
			return false;
		}

		contact.start = history.position;
		contact.end = position;
		contact.linearVelocity = velocity;
		contact.timestampUs = timestampUs;
		storeHistory(history, sourceToken, actionToken, position, timestampUs);
		return true;
	}

	bool evaluateShieldBlock(const VrIncomingContact & incoming,
	                         VrDefenseEvent & event) {
		event = VrDefenseEvent{};
		if(!shieldActiveAt(incoming.timestampUs)) {
			return false;
		}
		return m_defense.evaluateShieldBlock(m_shield.profile, m_shield.pose,
		                                     incoming, event);
	}

	bool evaluatePlayerDefense(std::uint64_t sourceToken,
	                           std::uint64_t strikeToken,
	                           const VrIncomingContact & incoming,
	                           float attackerRadius,
	                           VrDefenseEvent & event) {
		event = VrDefenseEvent{};
		if(sourceToken == 0 || strikeToken == 0 || incoming.timestampUs == 0) {
			return false;
		}

		if(defenderWeaponActiveAt(incoming.timestampUs)
		   && std::isfinite(attackerRadius) && attackerRadius >= 0.f) {
			VrParrySample sample;
			sample.defender = m_defenderWeapon.segment;
			sample.attacker.start = incoming.start;
			sample.attacker.end = incoming.end;
			sample.attacker.radius = attackerRadius;
			sample.attacker.valid = vrVectorFinite(incoming.start)
			                     && vrVectorFinite(incoming.end);
			sample.defenderVelocity = m_defenderWeapon.linearVelocity;
			sample.attackerVelocity = incoming.linearVelocity;
			sample.timestampUs = incoming.timestampUs;
			if(m_defense.evaluateParry(sample, event)) {
				armDefenseLatch(sourceToken, strikeToken, event.type, incoming.timestampUs);
				return true;
			}
		}

		if(evaluateShieldBlock(incoming, event)) {
			armDefenseLatch(sourceToken, strikeToken, event.type, incoming.timestampUs);
			return true;
		}
		return false;
	}

	bool defenseLatched(std::uint64_t sourceToken,
	                    std::uint64_t strikeToken,
	                    std::uint64_t timestampUs) const {
		if(!m_latch.active || sourceToken == 0 || strikeToken == 0
		   || timestampUs == 0 || m_latch.sourceToken != sourceToken
		   || m_latch.strikeToken != strikeToken || timestampUs < m_latch.timestampUs) {
			return false;
		}
		return timestampUs - m_latch.timestampUs <= m_config.defenseLatchUs;
	}

	VrDefenseEventType latchedDefenseType() const {
		return m_latch.active ? m_latch.type : VrDefenseEventType::None;
	}

	bool sampleShieldBlock(std::uint64_t sourceToken,
	                       std::uint64_t actionToken,
	                       const VrImpactVector3 & position,
	                       std::uint64_t timestampUs,
	                       VrDefenseEvent & event) {
		event = VrDefenseEvent{};
		VrIncomingContact incoming;
		return sampleIncomingWeapon(sourceToken, actionToken, position, timestampUs, incoming)
		    && evaluateShieldBlock(incoming, event);
	}

	void resetSession() {
		m_shield = VrPublishedShield{};
		m_defenderWeapon = VrPublishedDefenderWeapon{};
		m_latch = DefenseLatch{};
		for(IncomingHistory & history : m_histories) {
			history = IncomingHistory{};
		}
		m_defense.resetSession();
	}

private:
	struct IncomingHistory {
		std::uint64_t sourceToken = 0;
		std::uint64_t actionToken = 0;
		VrImpactVector3 position{};
		std::uint64_t timestampUs = 0;
		bool valid = false;
	};

	struct DefenseLatch {
		std::uint64_t sourceToken = 0;
		std::uint64_t strikeToken = 0;
		VrDefenseEventType type = VrDefenseEventType::None;
		std::uint64_t timestampUs = 0;
		bool active = false;
	};

	bool runtimeConfigValid() const {
		return m_config.maxShieldAgeUs > 0
		    && m_config.maxDefenderWeaponAgeUs > 0
		    && m_config.maxIncomingGapUs > 0
		    && m_config.maxDefenderWeaponGapUs > 0
		    && m_config.defenseLatchUs > 0
		    && std::isfinite(m_config.maxIncomingSpeed)
		    && m_config.maxIncomingSpeed > 0.f
		    && std::isfinite(m_config.maxDefenderWeaponSpeed)
		    && m_config.maxDefenderWeaponSpeed > 0.f;
	}

	static bool weaponSegmentValid(const VrWeaponSegment & segment) {
		return segment.valid && vrVectorFinite(segment.start)
		    && vrVectorFinite(segment.end) && std::isfinite(segment.radius)
		    && segment.radius >= 0.f
		    && vrVectorLength(vrDefenseSubtract(segment.end, segment.start)) > 0.0001f;
	}

	void armDefenseLatch(std::uint64_t sourceToken,
	                     std::uint64_t strikeToken,
	                     VrDefenseEventType type,
	                     std::uint64_t timestampUs) {
		m_latch.sourceToken = sourceToken;
		m_latch.strikeToken = strikeToken;
		m_latch.type = type;
		m_latch.timestampUs = timestampUs;
		m_latch.active = type != VrDefenseEventType::None;
	}

	IncomingHistory & findOrAllocateHistory(std::uint64_t sourceToken,
	                                        std::uint64_t actionToken) {
		IncomingHistory * oldest = &m_histories[0];
		for(IncomingHistory & history : m_histories) {
			if(history.valid && history.sourceToken == sourceToken
			   && history.actionToken == actionToken) {
				return history;
			}
			if(!history.valid) {
				return history;
			}
			if(history.timestampUs < oldest->timestampUs) {
				oldest = &history;
			}
		}
		return *oldest;
	}

	static void storeHistory(IncomingHistory & history,
	                         std::uint64_t sourceToken,
	                         std::uint64_t actionToken,
	                         const VrImpactVector3 & position,
	                         std::uint64_t timestampUs) {
		history.sourceToken = sourceToken;
		history.actionToken = actionToken;
		history.position = position;
		history.timestampUs = timestampUs;
		history.valid = true;
	}

	VrDefenseRuntimeConfig m_config{};
	VrPublishedShield m_shield{};
	VrPublishedDefenderWeapon m_defenderWeapon{};
	DefenseLatch m_latch{};
	std::array<IncomingHistory, 16> m_histories{};
	VrDefenseSystem m_defense{};
};

// Shared process-local adapter used by live Arx gameplay. The classifier and
// geometry remain ordinary deterministic C++ and are instantiated directly by
// tests; only this accessor provides the live-session singleton.
inline VrDefenseRuntime & vrDefenseRuntime() {
	static VrDefenseRuntime runtime;
	return runtime;
}

} // namespace arxvr
