#include "vr/VrRuneRuntime.h"

#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char * message) {
	if(!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

arxvr::VrRunePlane plane() {
	arxvr::VrRunePlane result;
	result.origin = { 0.f, 0.f, 0.f };
	result.normal = { 0.f, 0.f, 1.f };
	result.up = { 0.f, 1.f, 0.f };
	result.valid = true;
	return result;
}

arxvr::VrRuneSample sample(std::uint64_t timestampUs, float z, bool pressed = true) {
	arxvr::VrRuneSample result;
	result.timestampUs = timestampUs;
	result.handPosition = { 0.f, 0.f, z };
	result.trackingValid = true;
	result.paintPressed = pressed;
	return result;
}

void invalidPaintDownRequiresFreshTriggerEpoch() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.maximumPlaneDistance = 10.f;
	config.capture.maximumHandSpeed = 100000.f;
	arxvr::VrRuneRuntime runtime(config);
	const auto drawingPlane = plane();

	auto outside = runtime.update(sample(100000u, 15.f), drawingPlane);
	expect(outside.status == arxvr::VrRuneStatus::TrackingReset,
	       "paint-down outside the finite rune slab must fail closed");
	expect(outside.blockedUntilRelease && runtime.releaseRequired(),
	       "invalid paint-down while held must latch release-required state");
	expect(!runtime.capturing(),
	       "invalid initial paint sample must not create a rune capture");

	auto enteredWhileHeld = runtime.update(sample(120000u, 0.f), drawingPlane);
	expect(enteredWhileHeld.blockedUntilRelease && !runtime.capturing(),
	       "entering the rune slab during the same trigger hold must not begin a delayed glyph");

	auto release = runtime.update(sample(140000u, 0.f, false), drawingPlane);
	expect(release.status == arxvr::VrRuneStatus::Idle && release.cancelled,
	       "physical release should terminate the invalid paint epoch");
	expect(!runtime.releaseRequired(),
	       "release must clear the paint-epoch latch");

	auto freshPress = runtime.update(sample(200000u, 0.f), drawingPlane);
	expect(freshPress.status == arxvr::VrRuneStatus::Capturing && runtime.capturing(),
	       "a fresh trigger press inside the slab should begin normal rune capture");
}

void featureCanBeExplicitlyDisabled() {
	arxvr::VrRuneRuntimeConfig config;
	config.capture.maximumPlaneDistance = 10.f;
	config.capture.maximumHandSpeed = 100000.f;
	config.requireReleaseAfterTrackingReset = false;
	arxvr::VrRuneRuntime runtime(config);
	const auto drawingPlane = plane();

	auto outside = runtime.update(sample(100000u, 15.f), drawingPlane);
	expect(outside.status == arxvr::VrRuneStatus::TrackingReset && !runtime.releaseRequired(),
	       "configuration should retain the documented opt-out for release latching");
	auto enteredWhileHeld = runtime.update(sample(120000u, 0.f), drawingPlane);
	expect(enteredWhileHeld.status == arxvr::VrRuneStatus::Capturing,
	       "with latching disabled, a later valid held sample may start capture");
}

} // namespace

int main() {
	invalidPaintDownRequiresFreshTriggerEpoch();
	featureCanBeExplicitlyDisabled();
	if(g_failures != 0) {
		std::cerr << g_failures << " VrRunePaintEpoch test failure(s)\n";
		return 1;
	}
	std::cout << "VrRunePaintEpoch tests passed\n";
	return 0;
}
