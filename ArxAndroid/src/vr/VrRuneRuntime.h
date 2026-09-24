#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "vr/VrRuneSystem.h"

namespace arxvr {

struct VrRuneViewportPoint {
	float x = 0.f;
	float y = 0.f;
	bool valid = false;
};

struct VrRuneRuntimeResult {
	VrRuneStatus status = VrRuneStatus::Idle;
	bool livePointValid = false;
	VrRunePoint2 livePoint{};
	bool strokeEnded = false;
	bool gestureReady = false;
	bool cancelled = false;
	bool blockedUntilRelease = false;
	VrRuneGesture gesture{};
};

struct VrRuneRuntimeConfig {
	VrRuneConfig capture{};
	// A normalized half-extent of 1 maps to this fraction of the shorter
	// viewport dimension. Keeping it below 0.5 leaves margin for overshoot.
	float viewportHalfSpanFraction = 0.34f;
	// After a tracking discontinuity, the trigger must be released before a
	// new rune can begin. This prevents one held trigger from stitching two
	// unrelated tracking epochs into separate accidental glyphs.
	bool requireReleaseAfterTrackingReset = true;
};

inline bool vrRuneRuntimeConfigValid(const VrRuneRuntimeConfig & config) {
	return vrRuneConfigValid(config.capture)
	    && vrRuneFinite(config.viewportHalfSpanFraction)
	    && config.viewportHalfSpanFraction > 0.f
	    && config.viewportHalfSpanFraction < 0.5f;
}

// Runtime bridge around VrRuneSystem. It keeps the capture plane locked for the
// full physical stroke, publishes filtered live points for legacy flare drawing,
// and owns the release-after-tracking-reset invariant. Rune meaning remains the
// responsibility of Arx's existing spell recognizer.
class VrRuneRuntime {
public:
	explicit VrRuneRuntime(VrRuneRuntimeConfig config = {})
		: m_config(config), m_system(config.capture) { }

	VrRuneRuntimeResult update(const VrRuneSample & sample, const VrRunePlane & plane) {
		VrRuneRuntimeResult result;

		if(!vrRuneRuntimeConfigValid(m_config)) {
			reset();
			result.status = VrRuneStatus::InvalidConfiguration;
			return result;
		}

		if(m_releaseRequired) {
			result.blockedUntilRelease = true;
			result.status = VrRuneStatus::TrackingReset;
			if(!sample.paintPressed) {
				m_releaseRequired = false;
				result.strokeEnded = true;
				result.cancelled = true;
				result.blockedUntilRelease = false;
				result.status = VrRuneStatus::Idle;
			}
			return result;
		}

		const bool wasCapturing = m_system.capturing();
		if(sample.paintPressed && !wasCapturing) {
			m_runtimePlaneValid = lockRuntimePlane(plane);
			m_haveLivePoint = false;
			m_livePointCount = 0;
		}

		result.status = m_system.update(sample, plane);

		if(result.status == VrRuneStatus::Capturing && m_runtimePlaneValid) {
			VrRunePoint2 projected{};
			if(project(sample.handPosition, projected) && shouldEmitLivePoint(projected)) {
				result.livePoint = projected;
				result.livePointValid = true;
				m_lastLivePoint = projected;
				m_haveLivePoint = true;
				++m_livePointCount;
			}
		}

		if(result.status == VrRuneStatus::GestureReady) {
			result.strokeEnded = true;
			result.gestureReady = m_system.consumeGesture(result.gesture);
			clearRuntimePlane();
			return result;
		}

		const bool endedWithoutGesture = wasCapturing && !sample.paintPressed
		                               && result.status == VrRuneStatus::Idle;
		if(endedWithoutGesture) {
			result.strokeEnded = true;
			clearRuntimePlane();
			return result;
		}

		if(result.status == VrRuneStatus::TrackingReset
		   || result.status == VrRuneStatus::InvalidPlane
		   || result.status == VrRuneStatus::InvalidConfiguration) {
			result.strokeEnded = wasCapturing;
			result.cancelled = wasCapturing;
			clearRuntimePlane();
			// A held trigger defines one physical paint epoch. If its very first
			// tracked sample is invalid (including paint-down outside the finite
			// drawing slab), do not let later motion silently begin a rune inside
			// that same hold. A physical release must delimit the next attempt.
			if(sample.paintPressed && m_config.requireReleaseAfterTrackingReset
			   && result.status == VrRuneStatus::TrackingReset) {
				m_releaseRequired = true;
				result.blockedUntilRelease = true;
			}
		}

		return result;
	}

	VrRuneViewportPoint mapToViewport(const VrRunePoint2 & point,
	                                 int width, int height) const {
		VrRuneViewportPoint result;
		if(width <= 1 || height <= 1 || !vrRuneFinite(point.x) || !vrRuneFinite(point.y)
		   || !vrRuneRuntimeConfigValid(m_config)) {
			return result;
		}

		const float extent = static_cast<float>(std::min(width, height))
		                   * m_config.viewportHalfSpanFraction;
		const float centerX = static_cast<float>(width - 1) * 0.5f;
		const float centerY = static_cast<float>(height - 1) * 0.5f;
		result.x = std::clamp(centerX + point.x * extent, 0.f,
		                      static_cast<float>(width - 1));
		result.y = std::clamp(centerY - point.y * extent, 0.f,
		                      static_cast<float>(height - 1));
		result.valid = vrRuneFinite(result.x) && vrRuneFinite(result.y);
		return result;
	}

	bool capturing() const {
		return m_system.capturing();
	}

	bool releaseRequired() const {
		return m_releaseRequired;
	}

	void reset() {
		m_system.reset();
		m_releaseRequired = false;
		clearRuntimePlane();
	}

private:
	bool lockRuntimePlane(const VrRunePlane & plane) {
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
		if(!vrRuneNormalize(right, m_planeRight)
		   || !vrRuneNormalize(vrRuneCross(normal, m_planeRight), m_planeUp)) {
			return false;
		}
		m_planeOrigin = plane.origin;
		return true;
	}

	bool project(const VrRuneVector3 & world, VrRunePoint2 & point) const {
		if(!m_runtimePlaneValid || !vrRuneFinite(world)) {
			return false;
		}
		const VrRuneVector3 delta = vrRuneSubtract(world, m_planeOrigin);
		point.x = vrRuneDot(delta, m_planeRight) / m_config.capture.halfWidth;
		point.y = vrRuneDot(delta, m_planeUp) / m_config.capture.halfHeight;
		return vrRuneFinite(point.x) && vrRuneFinite(point.y);
	}

	float projectedWorldDistance(const VrRunePoint2 & a, const VrRunePoint2 & b) const {
		const float dx = (a.x - b.x) * m_config.capture.halfWidth;
		const float dy = (a.y - b.y) * m_config.capture.halfHeight;
		return std::sqrt(dx * dx + dy * dy);
	}

	bool shouldEmitLivePoint(const VrRunePoint2 & point) const {
		if(m_livePointCount >= m_config.capture.maximumPointCount) {
			return false;
		}
		if(!m_haveLivePoint) {
			return true;
		}
		const float distance = projectedWorldDistance(m_lastLivePoint, point);
		return vrRuneFinite(distance) && distance >= m_config.capture.minimumPointDistance;
	}

	void clearRuntimePlane() {
		m_runtimePlaneValid = false;
		m_planeOrigin = {};
		m_planeRight = { 1.f, 0.f, 0.f };
		m_planeUp = { 0.f, 1.f, 0.f };
		m_haveLivePoint = false;
		m_lastLivePoint = {};
		m_livePointCount = 0;
	}

	VrRuneRuntimeConfig m_config{};
	VrRuneSystem m_system;
	bool m_releaseRequired = false;
	bool m_runtimePlaneValid = false;
	VrRuneVector3 m_planeOrigin{};
	VrRuneVector3 m_planeRight{ 1.f, 0.f, 0.f };
	VrRuneVector3 m_planeUp{ 0.f, 1.f, 0.f };
	bool m_haveLivePoint = false;
	VrRunePoint2 m_lastLivePoint{};
	std::size_t m_livePointCount = 0;
};

} // namespace arxvr
