#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace arxvr {

struct VrRuneVector3 {
	float x = 0.f;
	float y = 0.f;
	float z = 0.f;
};

struct VrRunePoint2 {
	float x = 0.f;
	float y = 0.f;
};

struct VrRuneBounds2 {
	VrRunePoint2 min{};
	VrRunePoint2 max{};
	bool valid = false;
};

// A gesture locks one plane at paint-down and keeps it for the full stroke.
// The game-facing adapter may derive this from the HMD or casting hand, but
// camera/head movement after paint-down cannot bend an in-progress rune.
struct VrRunePlane {
	VrRuneVector3 origin{};
	VrRuneVector3 normal{0.f, 0.f, 1.f};
	VrRuneVector3 up{0.f, 1.f, 0.f};
	bool valid = true;
};

struct VrRuneSample {
	std::uint64_t timestampUs = 0;
	VrRuneVector3 handPosition{};
	bool trackingValid = false;
	bool paintPressed = false;
};

struct VrRuneConfig {
	// Physical half-extents of the virtual drawing plane in Arx world units.
	float halfWidth = 55.f;
	float halfHeight = 55.f;
	// Reject sub-millimetre/controller jitter before feeding the legacy rune
	// recogniser. Distances are evaluated in world units on the locked plane.
	float minimumPointDistance = 1.5f;
	float minimumPathLength = 12.f;
	std::size_t minimumPointCount = 4;
	std::size_t maximumPointCount = 256;
	// Tracking discontinuities cancel the stroke instead of connecting stale
	// poses with one synthetic line segment.
	std::uint64_t maximumSampleGapUs = 150000;
	float maximumHandSpeed = 6000.f;
};

struct VrRuneGesture {
	// Normalized coordinates relative to the locked plane half-extents. Values
	// may exceed [-1, 1]; clipping/render policy belongs to the live adapter.
	std::vector<VrRunePoint2> points;
	VrRuneBounds2 bounds{};
	float pathLength = 0.f;
	std::uint64_t durationUs = 0;
	bool capacityLimited = false;
	bool valid = false;
};

enum class VrRuneStatus {
	Idle,
	Capturing,
	GestureReady,
	InvalidConfiguration,
	InvalidPlane,
	TrackingReset
};

inline bool vrRuneFinite(float value) {
	return std::isfinite(value);
}

inline bool vrRuneFinite(const VrRuneVector3 & value) {
	return vrRuneFinite(value.x) && vrRuneFinite(value.y) && vrRuneFinite(value.z);
}

inline VrRuneVector3 vrRuneSubtract(const VrRuneVector3 & a, const VrRuneVector3 & b) {
	return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline VrRuneVector3 vrRuneCross(const VrRuneVector3 & a, const VrRuneVector3 & b) {
	return {
		a.y * b.z - a.z * b.y,
		a.z * b.x - a.x * b.z,
		a.x * b.y - a.y * b.x
	};
}

inline float vrRuneDot(const VrRuneVector3 & a, const VrRuneVector3 & b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float vrRuneLength(const VrRuneVector3 & value) {
	return std::sqrt(vrRuneDot(value, value));
}

inline VrRuneVector3 vrRuneScale(const VrRuneVector3 & value, float scale) {
	return { value.x * scale, value.y * scale, value.z * scale };
}

inline bool vrRuneNormalize(const VrRuneVector3 & value, VrRuneVector3 & normalized) {
	if(!vrRuneFinite(value)) {
		return false;
	}
	const float length = vrRuneLength(value);
	if(!vrRuneFinite(length) || length <= 0.0001f) {
		return false;
	}
	normalized = vrRuneScale(value, 1.f / length);
	return vrRuneFinite(normalized);
}

inline bool vrRuneConfigValid(const VrRuneConfig & config) {
	return vrRuneFinite(config.halfWidth) && config.halfWidth > 0.f
	    && vrRuneFinite(config.halfHeight) && config.halfHeight > 0.f
	    && vrRuneFinite(config.minimumPointDistance) && config.minimumPointDistance >= 0.f
	    && vrRuneFinite(config.minimumPathLength) && config.minimumPathLength >= 0.f
	    && config.minimumPointCount >= 2
	    && config.maximumPointCount >= config.minimumPointCount
	    && config.maximumSampleGapUs > 0
	    && vrRuneFinite(config.maximumHandSpeed) && config.maximumHandSpeed > 0.f;
}

// Runtime-neutral physical stroke capture. It owns gesture boundaries and
// filtering only; existing Arx rune recognition remains the authority for what
// glyph a completed normalized stroke means.
class VrRuneSystem {
public:
	explicit VrRuneSystem(VrRuneConfig config = {}) : m_config(config) { }

	VrRuneStatus update(const VrRuneSample & sample, const VrRunePlane & plane) {
		if(!vrRuneConfigValid(m_config)) {
			reset();
			return VrRuneStatus::InvalidConfiguration;
		}

		if(!sample.paintPressed) {
			if(!m_capturing) {
				return VrRuneStatus::Idle;
			}
			if(!sampleValidForContinuation(sample)) {
				cancelCapture();
				return VrRuneStatus::TrackingReset;
			}
			return finishGesture();
		}

		if(!m_capturing) {
			m_pending = {};
			cancelCapture();
			if(!sample.trackingValid || sample.timestampUs == 0 || !vrRuneFinite(sample.handPosition)) {
				return VrRuneStatus::TrackingReset;
			}
			if(!lockPlane(plane)) {
				return VrRuneStatus::InvalidPlane;
			}

			m_capturing = true;
			m_startTimestampUs = sample.timestampUs;
			m_lastTimestampUs = sample.timestampUs;
			m_lastHandPosition = sample.handPosition;
			m_haveLastHand = true;
			addProjectedPoint(sample.handPosition, true);
			return VrRuneStatus::Capturing;
		}

		if(!sampleValidForContinuation(sample)) {
			cancelCapture();
			return VrRuneStatus::TrackingReset;
		}

		const std::uint64_t deltaUs = sample.timestampUs - m_lastTimestampUs;
		const float seconds = static_cast<float>(deltaUs) * 0.000001f;
		const float handDistance = vrRuneLength(vrRuneSubtract(sample.handPosition, m_lastHandPosition));
		if(!vrRuneFinite(seconds) || seconds <= 0.f || !vrRuneFinite(handDistance)
		   || handDistance / seconds > m_config.maximumHandSpeed) {
			cancelCapture();
			return VrRuneStatus::TrackingReset;
		}

		m_lastTimestampUs = sample.timestampUs;
		m_lastHandPosition = sample.handPosition;
		addProjectedPoint(sample.handPosition, false);
		return VrRuneStatus::Capturing;
	}

	bool consumeGesture(VrRuneGesture & gesture) {
		gesture = m_pending;
		const bool valid = m_pending.valid;
		m_pending = {};
		return valid;
	}

	bool capturing() const {
		return m_capturing;
	}

	void reset() {
		cancelCapture();
		m_pending = {};
	}

private:
	bool lockPlane(const VrRunePlane & plane) {
		m_planeLocked = false;
		if(!plane.valid || !vrRuneFinite(plane.origin) || !vrRuneFinite(plane.normal)
		   || !vrRuneFinite(plane.up)) {
			return false;
		}

		VrRuneVector3 normal;
		if(!vrRuneNormalize(plane.normal, normal)) {
			return false;
		}

		VrRuneVector3 right = vrRuneCross(plane.up, normal);
		if(vrRuneLength(right) <= 0.001f) {
			const VrRuneVector3 fallback = std::abs(normal.y) < 0.9f
			                                 ? VrRuneVector3{ 0.f, 1.f, 0.f }
			                                 : VrRuneVector3{ 1.f, 0.f, 0.f };
			right = vrRuneCross(fallback, normal);
		}
		if(!vrRuneNormalize(right, m_planeRight)) {
			return false;
		}
		if(!vrRuneNormalize(vrRuneCross(normal, m_planeRight), m_planeUp)) {
			return false;
		}

		m_planeOrigin = plane.origin;
		m_planeLocked = true;
		return true;
	}

	bool sampleValidForContinuation(const VrRuneSample & sample) const {
		if(!sample.trackingValid || sample.timestampUs == 0 || !vrRuneFinite(sample.handPosition)
		   || !m_haveLastHand || !m_planeLocked || sample.timestampUs <= m_lastTimestampUs) {
			return false;
		}
		return sample.timestampUs - m_lastTimestampUs <= m_config.maximumSampleGapUs;
	}

	VrRunePoint2 project(const VrRuneVector3 & world) const {
		const VrRuneVector3 delta = vrRuneSubtract(world, m_planeOrigin);
		return {
			vrRuneDot(delta, m_planeRight) / m_config.halfWidth,
			vrRuneDot(delta, m_planeUp) / m_config.halfHeight
		};
	}

	float projectedWorldDistance(const VrRunePoint2 & a, const VrRunePoint2 & b) const {
		const float dx = (a.x - b.x) * m_config.halfWidth;
		const float dy = (a.y - b.y) * m_config.halfHeight;
		return std::sqrt(dx * dx + dy * dy);
	}

	void expandBounds(const VrRunePoint2 & point) {
		if(!m_bounds.valid) {
			m_bounds.min = point;
			m_bounds.max = point;
			m_bounds.valid = true;
			return;
		}
		m_bounds.min.x = std::min(m_bounds.min.x, point.x);
		m_bounds.min.y = std::min(m_bounds.min.y, point.y);
		m_bounds.max.x = std::max(m_bounds.max.x, point.x);
		m_bounds.max.y = std::max(m_bounds.max.y, point.y);
	}

	void addProjectedPoint(const VrRuneVector3 & handPosition, bool force) {
		const VrRunePoint2 point = project(handPosition);
		if(!vrRuneFinite(point.x) || !vrRuneFinite(point.y)) {
			return;
		}

		if(m_haveLastProjectedPoint) {
			const float worldDistance = projectedWorldDistance(m_lastProjectedPoint, point);
			if(!vrRuneFinite(worldDistance)) {
				return;
			}
			if(!force && worldDistance < m_config.minimumPointDistance) {
				return;
			}
			m_pathLength += worldDistance;
		}

		// Keep trajectory metrics tied to the most recent accepted physical
		// sample even after the retained point buffer reaches capacity. Using the
		// last stored vector element here would repeatedly measure from one stale
		// point and inflate pathLength on long strokes.
		m_lastProjectedPoint = point;
		m_haveLastProjectedPoint = true;

		if(m_points.size() < m_config.maximumPointCount) {
			m_points.push_back(point);
			expandBounds(point);
		} else {
			m_capacityLimited = true;
		}
	}

	VrRuneStatus finishGesture() {
		const std::uint64_t durationUs = m_lastTimestampUs >= m_startTimestampUs
		                               ? m_lastTimestampUs - m_startTimestampUs : 0;
		const bool valid = m_points.size() >= m_config.minimumPointCount
		                && vrRuneFinite(m_pathLength)
		                && m_pathLength >= m_config.minimumPathLength;

		if(valid) {
			m_pending.points = m_points;
			m_pending.bounds = m_bounds;
			m_pending.pathLength = m_pathLength;
			m_pending.durationUs = durationUs;
			m_pending.capacityLimited = m_capacityLimited;
			m_pending.valid = true;
		}
		cancelCapture();
		return valid ? VrRuneStatus::GestureReady : VrRuneStatus::Idle;
	}

	void cancelCapture() {
		m_capturing = false;
		m_planeLocked = false;
		m_haveLastHand = false;
		m_startTimestampUs = 0;
		m_lastTimestampUs = 0;
		m_lastHandPosition = {};
		m_points.clear();
		m_bounds = {};
		m_pathLength = 0.f;
		m_capacityLimited = false;
		m_haveLastProjectedPoint = false;
		m_lastProjectedPoint = {};
	}

	VrRuneConfig m_config{};
	bool m_planeLocked = false;
	VrRuneVector3 m_planeOrigin{};
	VrRuneVector3 m_planeRight{ 1.f, 0.f, 0.f };
	VrRuneVector3 m_planeUp{ 0.f, 1.f, 0.f };

	bool m_capturing = false;
	bool m_haveLastHand = false;
	std::uint64_t m_startTimestampUs = 0;
	std::uint64_t m_lastTimestampUs = 0;
	VrRuneVector3 m_lastHandPosition{};
	std::vector<VrRunePoint2> m_points;
	VrRuneBounds2 m_bounds{};
	float m_pathLength = 0.f;
	bool m_capacityLimited = false;
	bool m_haveLastProjectedPoint = false;
	VrRunePoint2 m_lastProjectedPoint{};
	VrRuneGesture m_pending{};
};

} // namespace arxvr