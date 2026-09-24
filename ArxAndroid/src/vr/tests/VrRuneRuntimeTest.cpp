#include "vr/VrRuneRuntime.h"

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

arxvr::VrRuneSample sample(std::uint64_t timestampUs, float x, float y,
                           bool pressed = true) {
	arxvr::VrRuneSample value;
	value.timestampUs = timestampUs;
	value.handPosition = { x, y, 0.f };
	value.trackingValid = true;
	value.paintPressed = pressed;
	return value;
}

void liveProjectionUsesLockedPlane() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.halfWidth = 50.f;
	config.capture.halfHeight = 100.f;
	config.capture.minimumPointDistance = 0.f;
	config.capture.minimumPathLength = 1.f;
	config.capture.minimumPointCount = 2;
	arxvr::VrRuneRuntime runtime(config);
	auto drawingPlane = plane();

	auto start = runtime.update(sample(100000u, 0.f, 0.f), drawingPlane);
	expect(start.status == arxvr::VrRuneStatus::Capturing && start.livePointValid,
	       "paint-down should start capture and publish the first live point");
	expect(near(start.livePoint.x, 0.f) && near(start.livePoint.y, 0.f),
	       "first live point should be relative to the locked drawing-plane origin");

	// Moving the source plane after paint-down must not bend either the live
	// trail or the completed gesture.
	drawingPlane.origin = { 500.f, 500.f, 500.f };
	drawingPlane.normal = { 1.f, 0.f, 0.f };
	drawingPlane.up = { 0.f, 0.f, 1.f };
	auto moved = runtime.update(sample(120000u, 25.f, 50.f), drawingPlane);
	expect(moved.status == arxvr::VrRuneStatus::Capturing && moved.livePointValid,
	       "continued tracked motion should remain live after source-plane movement");
	expect(near(moved.livePoint.x, 0.5f) && near(moved.livePoint.y, 0.5f),
	       "live projection must remain on the paint-down plane");

	auto released = runtime.update(sample(140000u, 25.f, 50.f, false), drawingPlane);
	expect(released.strokeEnded && released.gestureReady && released.gesture.valid,
	       "release should publish the same completed filtered gesture");
	expect(released.gesture.points.size() == 2,
	       "two accepted samples should survive into the completed gesture");
	expect(near(released.gesture.points.back().x, 0.5f)
	       && near(released.gesture.points.back().y, 0.5f),
	       "completed gesture and live trail must share the locked projection");
}

void liveFilteringMatchesCaptureSpacing() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.minimumPointDistance = 5.f;
	config.capture.minimumPathLength = 5.f;
	config.capture.minimumPointCount = 2;
	config.capture.maximumHandSpeed = 100000.f;
	arxvr::VrRuneRuntime runtime(config);
	auto drawingPlane = plane();

	auto first = runtime.update(sample(100000u, 0.f, 0.f), drawingPlane);
	auto jitter = runtime.update(sample(110000u, 1.f, 0.f), drawingPlane);
	auto realMove = runtime.update(sample(120000u, 6.f, 0.f), drawingPlane);
	expect(first.livePointValid, "first physical sample should seed the visible trail");
	expect(!jitter.livePointValid,
	       "controller jitter below the capture spacing must not create visible trail points");
	expect(realMove.livePointValid,
	       "physical movement beyond the capture spacing should create a live point");

	auto released = runtime.update(sample(130000u, 6.f, 0.f, false), drawingPlane);
	expect(released.gestureReady && released.gesture.points.size() == 2,
	       "visible spacing filter should agree with the completed capture filter");
}

void trackingResetRequiresPhysicalRelease() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.minimumPointDistance = 0.f;
	config.capture.minimumPathLength = 1.f;
	config.capture.minimumPointCount = 2;
	config.capture.maximumHandSpeed = 100.f;
	arxvr::VrRuneRuntime runtime(config);
	auto drawingPlane = plane();

	runtime.update(sample(100000u, 0.f, 0.f), drawingPlane);
	auto reset = runtime.update(sample(101000u, 1000.f, 0.f), drawingPlane);
	expect(reset.status == arxvr::VrRuneStatus::TrackingReset && reset.cancelled
	       && reset.blockedUntilRelease && runtime.releaseRequired(),
	       "tracking teleport during a held trigger must cancel and latch release-required state");

	auto stillHeld = runtime.update(sample(102000u, 0.f, 0.f), drawingPlane);
	expect(stillHeld.blockedUntilRelease && !runtime.capturing(),
	       "continued trigger hold after tracking reset must not silently start a second rune");

	auto released = runtime.update(sample(103000u, 0.f, 0.f, false), drawingPlane);
	expect(released.strokeEnded && released.cancelled && !runtime.releaseRequired(),
	       "physical trigger release should clear the reset latch without publishing a rune");

	auto fresh = runtime.update(sample(200000u, 0.f, 0.f), drawingPlane);
	expect(fresh.status == arxvr::VrRuneStatus::Capturing && runtime.capturing(),
	       "a later paint-down should start from a fresh tracking epoch");
}

void depthEscapeRequiresPhysicalRelease() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.maximumPlaneDistance = 10.f;
	config.capture.minimumPointDistance = 0.f;
	config.capture.minimumPathLength = 1.f;
	config.capture.minimumPointCount = 2;
	config.capture.maximumHandSpeed = 100000.f;
	arxvr::VrRuneRuntime runtime(config);
	auto drawingPlane = plane();

	auto start = sample(100000u, 0.f, 0.f);
	start.handPosition.z = 3.f;
	expect(runtime.update(start, drawingPlane).status == arxvr::VrRuneStatus::Capturing,
	       "depth test precondition should start inside the physical rune slab");

	auto escaped = sample(120000u, 4.f, 0.f);
	escaped.handPosition.z = 12.f;
	auto reset = runtime.update(escaped, drawingPlane);
	expect(reset.status == arxvr::VrRuneStatus::TrackingReset && reset.cancelled
	       && reset.blockedUntilRelease && runtime.releaseRequired(),
	       "leaving the physical rune slab mid-stroke must cancel and require trigger release");

	auto returnedWhileHeld = sample(140000u, 8.f, 0.f);
	returnedWhileHeld.handPosition.z = 0.f;
	auto blocked = runtime.update(returnedWhileHeld, drawingPlane);
	expect(blocked.blockedUntilRelease && !runtime.capturing(),
	       "moving back onto the rune plane while still holding trigger must not resume a split stroke");

	auto released = returnedWhileHeld;
	released.timestampUs = 160000u;
	released.paintPressed = false;
	expect(runtime.update(released, drawingPlane).cancelled && !runtime.releaseRequired(),
	       "release should clear the depth-reset latch without publishing a gesture");
}

void shortStrokeEndsWithoutGesture() {
	arxvr::VrRuneRuntime runtime;
	auto drawingPlane = plane();

	runtime.update(sample(100000u, 0.f, 0.f), drawingPlane);
	runtime.update(sample(120000u, 1.f, 0.f), drawingPlane);
	auto released = runtime.update(sample(140000u, 1.f, 0.f, false), drawingPlane);
	expect(released.strokeEnded && !released.cancelled && !released.gestureReady,
	       "normal release of a sub-threshold stroke should be distinguishable from tracking cancellation");
}

void viewportMappingIsStableAndBounded() {
	arxvr::VrRuneRuntime runtime;
	auto center = runtime.mapToViewport({ 0.f, 0.f }, 1000, 500);
	expect(center.valid && near(center.x, 499.5f) && near(center.y, 249.5f),
	       "normalized rune origin should map to the viewport center");

	auto upperRight = runtime.mapToViewport({ 1.f, 1.f }, 1000, 500);
	expect(upperRight.valid && near(upperRight.x, 669.5f) && near(upperRight.y, 79.5f),
	       "normalized positive X/Y should map right/up with configured physical margin");

	auto clamped = runtime.mapToViewport({ 10.f, 10.f }, 1000, 500);
	expect(clamped.valid && near(clamped.x, 999.f) && near(clamped.y, 0.f),
	       "large physical overshoot should remain inside the legacy recognizer viewport");

	auto invalidSize = runtime.mapToViewport({ 0.f, 0.f }, 0, 500);
	expect(!invalidSize.valid, "invalid viewport dimensions must fail closed");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	auto invalidPoint = runtime.mapToViewport({ nan, 0.f }, 1000, 500);
	expect(!invalidPoint.valid, "non-finite projected rune coordinates must fail closed");
}

void resetClearsCaptureAndLatch() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.maximumHandSpeed = 100.f;
	arxvr::VrRuneRuntime runtime(config);
	auto drawingPlane = plane();

	runtime.update(sample(100000u, 0.f, 0.f), drawingPlane);
	runtime.update(sample(101000u, 1000.f, 0.f), drawingPlane);
	expect(runtime.releaseRequired(), "precondition: tracking reset should arm the release latch");
	runtime.reset();
	expect(!runtime.releaseRequired() && !runtime.capturing(),
	       "runtime reset should clear both capture and release-required state");
}

void invalidRuntimeConfigFailsClosed() {
	arxvr::VrRuneRuntimeConfig config;
	config.viewportHalfSpanFraction = 0.75f;
	arxvr::VrRuneRuntime runtime(config);
	auto result = runtime.update(sample(100000u, 0.f, 0.f), plane());
	expect(result.status == arxvr::VrRuneStatus::InvalidConfiguration,
	       "invalid viewport mapping configuration must fail closed before capture");
}

} // namespace

int main() {
	liveProjectionUsesLockedPlane();
	liveFilteringMatchesCaptureSpacing();
	trackingResetRequiresPhysicalRelease();
	depthEscapeRequiresPhysicalRelease();
	shortStrokeEndsWithoutGesture();
	viewportMappingIsStableAndBounded();
	resetClearsCaptureAndLatch();
	invalidRuntimeConfigFailsClosed();
	if(g_failures != 0) {
		std::cerr << g_failures << " VrRuneRuntime test failure(s)\n";
		return 1;
	}
	std::cout << "VrRuneRuntime tests passed\n";
	return 0;
}
