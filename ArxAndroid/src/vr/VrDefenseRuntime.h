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
	// A tracked shield pose is refreshed every gameplay frame. Keep a small
	// grace period for scheduling jitter while failing closed on stale input.
	std::uint64_t maxShieldAgeUs = 120000;
	// NPC animation/action-point samples separated by a long stall must not be
	// joined into one artificial weapon sweep.
	std::uint64_t maxIncomingGapUs = 250000;
	// 60 m/s in the current 100 Arx-units-per-metre scale. This is deliberately
	// above plausible melee motion while rejecting teleports/animation resets.
	float maxIncomingSpeed = 6000.f;
};

struct VrPublishedShield {
	std::uint64_t token = 0;
	VrShieldProfile profile{};
	VrShieldPose pose{};
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

	bool runtimeConfigValid() const {
		return m_config.maxShieldAgeUs > 0
		    && m_config.maxIncomingGapUs > 0
		    && std::isfinite(m_config.maxIncomingSpeed)
		    && m_config.maxIncomingSpeed > 0.f;
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
