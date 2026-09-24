#include "vr/VrRuneSystem.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int g_failures = 0;

void expect(bool condition, const char * message) {
	if(!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

bool near(float a, float b, float epsilon = 0.02f) {
	return std::abs(a - b) <= epsilon;
}

arxvr::VrRunePlane plane() {
	arxvr::VrRunePlane value;
	value.origin = { 0.f, 0.f, 0.f };
	value.normal = { 0.f, 0.f, 1.f };
	value.up = { 0.f, 1.f, 0.f };
	value.valid = true;
	return value;
}

arxvr::VrRuneSample sample(std::uint64_t timestampUs, float x, float y, bool pressed = true) {
	arxvr::VrRuneSample value;
	value.timestampUs = timestampUs;
	value.handPosition = { x, y, 0.f };
	value.trackingValid = true;
	value.paintPressed = pressed;
	return value;
}

void stablePlaneAndProjection() {
	arxvr::VrRuneConfig config;
	config.halfWidth = 50.f;
	config.halfHeight = 100.f;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 3;
	arxvr::VrRuneSystem system(config);
	auto drawingPlane = plane();

	expect(system.update(sample(100000u, 0.f, 0.f), drawingPlane) == arxvr::VrRuneStatus::Capturing,
	       "capture should start from a valid tracked paint-down");
	// Moving the source camera/hand plane during a stroke must not bend it.
	drawingPlane.origin = { 1000.f, 1000.f, 1000.f };
	drawingPlane.normal = { 1.f, 0.f, 0.f };
	system.update(sample(120000u, 25.f, 50.f), drawingPlane);
	system.update(sample(140000u, 50.f, 100.f), drawingPlane);
	expect(system.update(sample(160000u, 50.f, 100.f, false), drawingPlane)
	       == arxvr::VrRuneStatus::GestureReady,
	       "release after a real tracked stroke should publish a completed gesture");

	arxvr::VrRuneGesture gesture;
	expect(system.consumeGesture(gesture) && gesture.valid,
	       "completed physical stroke should be consumable exactly once");
	expect(gesture.points.size() == 3, "three accepted physical samples should survive projection");
	expect(near(gesture.points[1].x, 0.5f) && near(gesture.points[1].y, 0.5f),
	       "locked plane projection should survive later source-plane motion");
	expect(near(gesture.points[2].x, 1.f) && near(gesture.points[2].y, 1.f),
	       "projection should normalize independently by configured plane extents");
	expect(gesture.bounds.valid && near(gesture.bounds.max.x, 1.f) && near(gesture.bounds.max.y, 1.f),
	       "completed gesture should publish normalized bounds for recognizer/render adapters");
}

void anisotropicPlanePreservesWorldPathLength() {
	arxvr::VrRuneConfig config;
	config.halfWidth = 25.f;
	config.halfHeight = 100.f;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 2;
	arxvr::VrRuneSystem system(config);
	auto drawingPlane = plane();

	system.update(sample(100000u, 0.f, 0.f), drawingPlane);
	system.update(sample(120000u, 15.f, 20.f), drawingPlane);
	system.update(sample(140000u, 15.f, 20.f, false), drawingPlane);
	arxvr::VrRuneGesture gesture;
	expect(system.consumeGesture(gesture), "anisotropic plane stroke should qualify");
	expect(near(gesture.pathLength, 25.f),
	       "path length must remain physical world distance when plane axes use different scales");
}

void degenerateUpGetsStableFallback() {
	arxvr::VrRuneConfig config;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 2;
	arxvr::VrRuneSystem system(config);
	auto drawingPlane = plane();
	drawingPlane.up = { 0.f, 0.f, 2.f };

	expect(system.update(sample(100000u, 0.f, 0.f), drawingPlane) == arxvr::VrRuneStatus::Capturing,
	       "parallel up/normal vectors should use a deterministic fallback basis");
	system.update(sample(120000u, 10.f, 0.f), drawingPlane);
	system.update(sample(140000u, 10.f, 0.f, false), drawingPlane);
	arxvr::VrRuneGesture gesture;
	expect(system.consumeGesture(gesture) && gesture.valid,
	       "fallback plane basis should still emit a finite gesture");
}

void discontinuitiesFailClosed() {
	arxvr::VrRuneConfig config;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 2;
	config.maximumHandSpeed = 100.f;
	config.maximumSampleGapUs = 100000u;
	arxvr::VrRuneSystem system(config);
	auto drawingPlane = plane();

	system.update(sample(100000u, 0.f, 0.f), drawingPlane);
	expect(system.update(sample(101000u, 1000.f, 0.f), drawingPlane)
	       == arxvr::VrRuneStatus::TrackingReset,
	       "implausible controller teleport must cancel an in-progress rune");
	arxvr::VrRuneGesture gesture;
	expect(!system.consumeGesture(gesture), "tracking teleport must never synthesize a rune");

	system.update(sample(200000u, 0.f, 0.f), drawingPlane);
	expect(system.update(sample(400001u, 1.f, 0.f), drawingPlane)
	       == arxvr::VrRuneStatus::TrackingReset,
	       "tracking gap beyond the configured budget must cancel the old stroke");

	system.update(sample(500000u, 0.f, 0.f), drawingPlane);
	expect(system.update(sample(499999u, 1.f, 0.f), drawingPlane)
	       == arxvr::VrRuneStatus::TrackingReset,
	       "timestamp regression must fail closed instead of connecting stale points");
}

void filtersJitterAndRequiresRealStroke() {
	arxvr::VrRuneConfig config;
	config.minimumPointDistance = 5.f;
	config.minimumPathLength = 12.f;
	config.minimumPointCount = 3;
	config.maximumHandSpeed = 100000.f;
	arxvr::VrRuneSystem system(config);
	auto drawingPlane = plane();

	system.update(sample(100000u, 0.f, 0.f), drawingPlane);
	system.update(sample(110000u, 1.f, 0.f), drawingPlane);
	system.update(sample(120000u, 2.f, 0.f), drawingPlane);
	expect(system.update(sample(130000u, 2.f, 0.f, false), drawingPlane)
	       == arxvr::VrRuneStatus::Idle,
	       "micro-jitter near one location must not become a recognized stroke candidate");
	arxvr::VrRuneGesture gesture;
	expect(!system.consumeGesture(gesture), "rejected micro-stroke must not be queued");

	system.update(sample(200000u, 0.f, 0.f), drawingPlane);
	system.update(sample(210000u, 6.f, 0.f), drawingPlane);
	system.update(sample(220000u, 12.f, 0.f), drawingPlane);
	system.update(sample(230000u, 18.f, 0.f), drawingPlane);
	expect(system.update(sample(240000u, 18.f, 0.f, false), drawingPlane)
	       == arxvr::VrRuneStatus::GestureReady,
	       "deliberate stroke above distance and point gates should qualify");
	expect(system.consumeGesture(gesture) && gesture.pathLength >= 17.9f,
	       "qualified gesture should preserve real physical path length");
}

void oneShotAndCapacityLimit() {
	arxvr::VrRuneConfig config;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 2;
	config.maximumPointCount = 3;
	config.maximumHandSpeed = 100000.f;
	arxvr::VrRuneSystem system(config);
	auto drawingPlane = plane();

	system.update(sample(100000u, 0.f, 0.f), drawingPlane);
	system.update(sample(110000u, 10.f, 0.f), drawingPlane);
	system.update(sample(120000u, 20.f, 0.f), drawingPlane);
	system.update(sample(130000u, 30.f, 0.f), drawingPlane);
	system.update(sample(140000u, 40.f, 0.f), drawingPlane);
	system.update(sample(150000u, 40.f, 0.f, false), drawingPlane);
	arxvr::VrRuneGesture gesture;
	expect(system.consumeGesture(gesture), "capacity-limited gesture should remain consumable");
	expect(gesture.points.size() == 3 && gesture.capacityLimited,
	       "point cap should be explicit and keep memory bounded");
	expect(near(gesture.pathLength, 40.f),
	       "samples beyond retained-point capacity must keep physical path metrics incremental");
	arxvr::VrRuneGesture duplicate;
	expect(!system.consumeGesture(duplicate), "completed gesture should be a one-shot event");
}

void invalidInputFailsClosed() {
	arxvr::VrRuneConfig badConfig;
	badConfig.halfWidth = 0.f;
	arxvr::VrRuneSystem invalidConfigSystem(badConfig);
	expect(invalidConfigSystem.update(sample(100000u, 0.f, 0.f), plane())
	       == arxvr::VrRuneStatus::InvalidConfiguration,
	       "invalid drawing-plane dimensions must be rejected");

	arxvr::VrRuneSystem system;
	auto invalidPlane = plane();
	invalidPlane.normal = { 0.f, 0.f, 0.f };
	expect(system.update(sample(100000u, 0.f, 0.f), invalidPlane)
	       == arxvr::VrRuneStatus::InvalidPlane,
	       "zero plane normal must fail closed");

	auto drawingPlane = plane();
	auto malformed = sample(200000u, 0.f, 0.f);
	malformed.handPosition.x = std::numeric_limits<float>::quiet_NaN();
	expect(system.update(malformed, drawingPlane) == arxvr::VrRuneStatus::TrackingReset,
	       "non-finite tracked position must fail closed");
}

} // namespace

int main() {
	stablePlaneAndProjection();
	anisotropicPlanePreservesWorldPathLength();
	degenerateUpGetsStableFallback();
	discontinuitiesFailClosed();
	filtersJitterAndRequiresRealStroke();
	oneShotAndCapacityLimit();
	invalidInputFailsClosed();
	if(g_failures != 0) {
		std::cerr << g_failures << " VrRuneSystem test failure(s)\n";
		return 1;
	}
	std::cout << "VrRuneSystem tests passed\n";
	return 0;
}
