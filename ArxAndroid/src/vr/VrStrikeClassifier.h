#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace arxvr {

struct VrMotionSample {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;
	std::uint64_t timestampUs = 0;
};

struct VrStrikeProfile {
	std::uint64_t historyWindowUs = 160000;
	std::uint64_t cooldownUs = 350000;
	std::uint64_t maxSampleGapUs = 120000;
	float minPeakSpeed = 110.f;
	float minAverageSpeed = 70.f;
	float minTerminalSpeed = 70.f;
	// Real impacts commonly happen just after peak hand velocity. Preserve a
	// short follow-through window so contact during deliberate deceleration can
	// still qualify without allowing an almost stationary hand to deal damage.
	float minFollowThroughSpeed = 35.f;
	float maxFollowThroughToPeakRatio = 0.72f;
	float minPathLength = 6.5f;
	float minDirectionalConsistency = 0.55f;
	float minEnergy = 700.f;
	float rearmSpeed = 40.f;
	float maxInstantSpeed = 1000.f;
};

inline constexpr VrStrikeProfile vrFistStrikeProfile() {
	return VrStrikeProfile{};
}

inline constexpr VrStrikeProfile vrHeldObjectStrikeProfile() {
	VrStrikeProfile profile;
	profile.historyWindowUs = 190000;
	profile.cooldownUs = 300000;
	profile.minPeakSpeed = 95.f;
	profile.minAverageSpeed = 60.f;
	profile.minTerminalSpeed = 60.f;
	profile.minFollowThroughSpeed = 30.f;
	profile.maxFollowThroughToPeakRatio = 0.78f;
	profile.minPathLength = 8.f;
	profile.minDirectionalConsistency = 0.48f;
	profile.minEnergy = 700.f;
	profile.rearmSpeed = 35.f;
	profile.maxInstantSpeed = 1400.f;
	return profile;
}

struct VrStrikeMetrics {
	std::size_t sampleCount = 0;
	float durationSeconds = 0.f;
	float pathLength = 0.f;
	float displacement = 0.f;
	float averageSpeed = 0.f;
	float peakSpeed = 0.f;
	float terminalSpeed = 0.f;
	float directionalConsistency = 0.f;
	float energy = 0.f;
	bool valid = false;
};

enum class VrMotionSampleStatus {
	Accepted,
	TrackingReset,
	Invalid
};

class VrStrikeClassifier {
public:
	static constexpr std::size_t kMaxSamples = 16;

	VrMotionSampleStatus update(const VrMotionSample & sample,
	                            const VrStrikeProfile & profile) {
		if(!isFinite(sample)) {
			invalidateTracking(sample.timestampUs, profile);
			return VrMotionSampleStatus::Invalid;
		}

		if(m_count == 0) {
			push(sample);
			m_metrics = calculateMetrics();
			return VrMotionSampleStatus::Accepted;
		}

		const VrMotionSample & previous = m_samples[m_count - 1];
		if(sample.timestampUs <= previous.timestampUs) {
			resetAfterTrackingDiscontinuity(sample, profile);
			return VrMotionSampleStatus::TrackingReset;
		}

		const std::uint64_t deltaUs = sample.timestampUs - previous.timestampUs;
		const float deltaSeconds = static_cast<float>(deltaUs) * 0.000001f;
		const float segmentDistance = distance(previous, sample);
		const float segmentSpeed = segmentDistance / deltaSeconds;
		if(deltaUs > profile.maxSampleGapUs || !std::isfinite(segmentSpeed)
		   || segmentSpeed > profile.maxInstantSpeed) {
			resetAfterTrackingDiscontinuity(sample, profile);
			return VrMotionSampleStatus::TrackingReset;
		}

		push(sample);
		prune(sample.timestampUs, profile.historyWindowUs);
		m_metrics = calculateMetrics();

		if(!m_armed && sample.timestampUs >= m_rearmNotBeforeUs
		   && m_metrics.terminalSpeed <= profile.rearmSpeed) {
			// A low-speed sample after the cooldown is the neutral pose boundary.
			// Throw away every pre-rearm segment so accumulated motion from the
			// previous impact cannot contribute to the next strike.
			restartHistoryAt(sample);
			m_armed = true;
		}
		return VrMotionSampleStatus::Accepted;
	}

	bool canStrike(const VrStrikeProfile & profile) const {
		return m_armed && qualifies(m_metrics, profile);
	}

	bool consumeStrike(const VrStrikeProfile & profile) {
		if(!canStrike(profile) || m_count == 0) {
			return false;
		}
		m_armed = false;
		m_rearmNotBeforeUs = saturatingAdd(m_samples[m_count - 1].timestampUs,
		                                    profile.cooldownUs);
		return true;
	}

	const VrStrikeMetrics & metrics() const {
		return m_metrics;
	}

	bool armed() const {
		return m_armed;
	}

	std::uint64_t rearmNotBeforeUs() const {
		return m_rearmNotBeforeUs;
	}

	// Start a new gesture/source without bypassing the cooldown established by
	// an earlier hit. This is used when a fist closes, a different object is
	// grabbed, or another semantic impact source takes ownership of the hand.
	void resetHistory() {
		m_count = 0;
		m_metrics = VrStrikeMetrics{};
	}

	// Lost/non-finite tracking must fail closed even when there is no synthetic
	// position available to feed through update(). The next strike is allowed
	// only after the normal cooldown and low-speed rearm path.
	void invalidateTracking(std::uint64_t timestampUs,
	                        const VrStrikeProfile & profile) {
		resetHistory();
		m_armed = false;
		m_rearmNotBeforeUs = saturatingAdd(timestampUs, profile.cooldownUs);
	}

	// Full session reset: appropriate when starting a level/runtime, not for a
	// gesture boundary. Unlike resetHistory(), this intentionally clears the
	// cooldown and restores the initially armed state.
	void clear() {
		resetHistory();
		m_armed = true;
		m_rearmNotBeforeUs = 0;
	}

	static bool qualifies(const VrStrikeMetrics & metrics,
	                      const VrStrikeProfile & profile) {
		const bool directContact = metrics.terminalSpeed >= profile.minTerminalSpeed;
		const bool controlledFollowThrough =
			metrics.terminalSpeed >= profile.minFollowThroughSpeed
			&& metrics.terminalSpeed
			   <= metrics.peakSpeed * profile.maxFollowThroughToPeakRatio;
		return metrics.valid
		    && metrics.sampleCount >= 3
		    && metrics.peakSpeed >= profile.minPeakSpeed
		    && metrics.averageSpeed >= profile.minAverageSpeed
		    && (directContact || controlledFollowThrough)
		    && metrics.pathLength >= profile.minPathLength
		    && metrics.directionalConsistency >= profile.minDirectionalConsistency
		    && metrics.energy >= profile.minEnergy;
	}

private:
	static bool isFinite(const VrMotionSample & sample) {
		return std::isfinite(sample.x) && std::isfinite(sample.y)
		    && std::isfinite(sample.z);
	}

	static float distance(const VrMotionSample & a, const VrMotionSample & b) {
		const float dx = b.x - a.x;
		const float dy = b.y - a.y;
		const float dz = b.z - a.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	static std::uint64_t saturatingAdd(std::uint64_t value, std::uint64_t delta) {
		const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
		return delta > maximum - value ? maximum : value + delta;
	}

	void push(const VrMotionSample & sample) {
		if(m_count == kMaxSamples) {
			for(std::size_t i = 1; i < m_count; ++i) {
				m_samples[i - 1] = m_samples[i];
			}
			--m_count;
		}
		m_samples[m_count++] = sample;
	}

	void prune(std::uint64_t latestTimestampUs, std::uint64_t windowUs) {
		std::size_t first = 0;
		while(first + 2 < m_count
		      && latestTimestampUs - m_samples[first].timestampUs > windowUs) {
			++first;
		}
		if(first == 0) {
			return;
		}
		for(std::size_t i = first; i < m_count; ++i) {
			m_samples[i - first] = m_samples[i];
		}
		m_count -= first;
	}

	void restartHistoryAt(const VrMotionSample & sample) {
		resetHistory();
		push(sample);
		m_metrics = calculateMetrics();
	}

	void resetAfterTrackingDiscontinuity(const VrMotionSample & sample,
	                                     const VrStrikeProfile & profile) {
		restartHistoryAt(sample);
		m_armed = false;
		m_rearmNotBeforeUs = saturatingAdd(sample.timestampUs, profile.cooldownUs);
	}

	VrStrikeMetrics calculateMetrics() const {
		VrStrikeMetrics result;
		result.sampleCount = m_count;
		if(m_count < 2) {
			return result;
		}

		const std::uint64_t durationUs =
			m_samples[m_count - 1].timestampUs - m_samples[0].timestampUs;
		if(durationUs == 0) {
			return result;
		}

		result.durationSeconds = static_cast<float>(durationUs) * 0.000001f;
		for(std::size_t i = 1; i < m_count; ++i) {
			const std::uint64_t segmentUs =
				m_samples[i].timestampUs - m_samples[i - 1].timestampUs;
			if(segmentUs == 0) {
				return VrStrikeMetrics{};
			}
			const float segmentSeconds = static_cast<float>(segmentUs) * 0.000001f;
			const float segmentDistance = distance(m_samples[i - 1], m_samples[i]);
			const float segmentSpeed = segmentDistance / segmentSeconds;
			if(!std::isfinite(segmentSpeed)) {
				return VrStrikeMetrics{};
			}
			result.pathLength += segmentDistance;
			result.peakSpeed = std::max(result.peakSpeed, segmentSpeed);
			result.terminalSpeed = segmentSpeed;
			result.energy += segmentSpeed * segmentSpeed * segmentSeconds;
		}

		result.displacement = distance(m_samples[0], m_samples[m_count - 1]);
		result.averageSpeed = result.pathLength / result.durationSeconds;
		result.directionalConsistency = result.pathLength > 0.0001f
		                              ? result.displacement / result.pathLength : 0.f;
		result.valid = std::isfinite(result.averageSpeed)
		            && std::isfinite(result.directionalConsistency)
		            && std::isfinite(result.energy);
		return result;
	}

	std::array<VrMotionSample, kMaxSamples> m_samples{};
	std::size_t m_count = 0;
	VrStrikeMetrics m_metrics{};
	bool m_armed = true;
	std::uint64_t m_rearmNotBeforeUs = 0;
};

} // namespace arxvr
