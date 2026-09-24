#include "vr/VrBowRuntime.h"

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

bool near(float a, float b, float epsilon = 0.001f) {
	return std::abs(a - b) <= epsilon;
}

arxvr::VrBowTrackingSample sample(std::uint64_t token, std::uint64_t timestampUs,
                                  float stringZ, bool grip) {
	arxvr::VrBowTrackingSample value;
	value.bowToken = token;
	value.bowPosition = { 0.f, 0.f, 0.f };
	value.bowForward = { 0.f, 0.f, 1.f };
	value.bowUp = { 0.f, 1.f, 0.f };
	value.bowValid = true;
	value.stringPosition = { 0.f, 0.f, stringZ };
	value.stringValid = true;
	value.stringGripPressed = grip;
	value.timestampUs = timestampUs;
	return value;
}

void assistDistanceDoesNotBecomeLaunchEnergy() {
	arxvr::VrBowRuntime runtime;
	expect(runtime.update(sample(0x101u, 100000u, -15.f, true)).nocked,
	       "string acquired near assist edge should nock");

	// Absolute hand separation reaches the low-level release threshold, but the
	// player has only moved three world units after acquiring the assisted nock.
	runtime.update(sample(0x101u, 110000u, -18.f, false));
	arxvr::VrBowRuntimeRelease release;
	expect(!runtime.consumeRelease(release) && !release.valid,
	       "nock-assist distance must not become free physical draw energy");
}

void realTravelIsNormalizedFromAcquisitionPoint() {
	arxvr::VrBowRuntime runtime;
	runtime.update(sample(0x201u, 100000u, -15.f, true));
	runtime.update(sample(0x201u, 110000u, -45.f, true));
	runtime.update(sample(0x201u, 120000u, -45.f, false));

	arxvr::VrBowRuntimeRelease release;
	expect(runtime.consumeRelease(release) && release.valid,
	       "real post-acquisition string travel should qualify a release");
	expect(near(release.nockSeparation, 15.f)
	       && near(release.rawHandSeparation, 45.f)
	       && near(release.drawTravel, 30.f),
	       "runtime release should expose assisted baseline, raw separation and real travel");
	expect(near(release.release.drawDistance, 30.f),
	       "gameplay draw distance should exclude nock-assist separation");
	expect(near(release.release.drawRatio, 30.f / 55.f),
	       "charge ratio should normalize over travel available after acquisition");
	expect(release.release.direction.z > 0.999f,
	       "runtime filtering must preserve the physical two-hand aim direction");

	arxvr::VrBowRuntimeRelease duplicate;
	expect(!runtime.consumeRelease(duplicate),
	       "qualified gameplay release should remain one-shot consumable");
}

void teleportCannotCreateFullDraw() {
	arxvr::VrBowRuntime runtime;
	runtime.update(sample(0x301u, 100000u, 0.f, true));

	// 70 world units in 1 ms is 70,000 units/s, well above the deliberate
	// runtime discontinuity ceiling.
	const arxvr::VrBowPose teleported = runtime.update(sample(0x301u, 101000u, -70.f, false));
	expect(!runtime.nocked() && !teleported.nocked,
	       "string-hand teleport must cancel the pending nock before release qualification");
	arxvr::VrBowRuntimeRelease release;
	expect(!runtime.consumeRelease(release),
	       "tracking teleport must never synthesize maximum bow charge");
}

void longTrackingGapRequiresFreshNock() {
	arxvr::VrBowRuntime runtime;
	runtime.update(sample(0x401u, 100000u, 0.f, true));
	const arxvr::VrBowPose recovered = runtime.update(sample(0x401u, 500001u, -35.f, false));
	expect(!runtime.nocked() && !recovered.nocked,
	       "tracking gap beyond runtime budget should cancel the old string latch");
	arxvr::VrBowRuntimeRelease release;
	expect(!runtime.consumeRelease(release),
	       "stale pre-gap pose must not bridge into a physical shot");
}

void bowIdentityResetsAssistedBaseline() {
	arxvr::VrBowRuntime runtime;
	runtime.update(sample(0x501u, 100000u, -15.f, true));
	expect(runtime.nocked(), "first bow should acquire before identity swap");

	const arxvr::VrBowPose swapped = runtime.update(sample(0x502u, 110000u, -45.f, false));
	expect(runtime.bowToken() == 0x502u && !runtime.nocked() && !swapped.nocked,
	       "equipped bow identity change must discard old nock and draw baseline");
	arxvr::VrBowRuntimeRelease release;
	expect(!runtime.consumeRelease(release),
	       "draw energy from a previous bow must not leak into the newly equipped bow");
}

void malformedRuntimeConfigurationFailsClosed() {
	arxvr::VrBowRuntimeConfig invalid;
	invalid.maxStringSpeed = std::numeric_limits<float>::quiet_NaN();
	expect(!arxvr::vrBowRuntimeConfigValid(invalid),
	       "non-finite tracking speed limit should be rejected");

	arxvr::VrBowRuntime runtime({}, invalid);
	const arxvr::VrBowPose pose = runtime.update(sample(0x601u, 100000u, 0.f, true));
	expect(!pose.valid && !runtime.nocked(),
	       "invalid runtime guard configuration must fail closed before nocking");
}

void realisticContinuousMotionRemainsAccepted() {
	arxvr::VrBowRuntimeConfig config;
	config.maxStringSpeed = 4000.f;
	config.minimumDrawTravel = 18.f;
	arxvr::VrBowRuntime runtime({}, config);

	runtime.update(sample(0x701u, 100000u, 0.f, true));
	runtime.update(sample(0x701u, 110000u, -20.f, true)); // 2000 units/s
	runtime.update(sample(0x701u, 120000u, -40.f, true)); // 2000 units/s
	runtime.update(sample(0x701u, 130000u, -40.f, false));

	arxvr::VrBowRuntimeRelease release;
	expect(runtime.consumeRelease(release) && release.valid,
	       "continuous plausible controller motion should survive teleport filtering");
	expect(near(release.drawTravel, 40.f),
	       "continuous draw should preserve actual physical travel");
}

} // namespace

int main() {
	assistDistanceDoesNotBecomeLaunchEnergy();
	realTravelIsNormalizedFromAcquisitionPoint();
	teleportCannotCreateFullDraw();
	longTrackingGapRequiresFreshNock();
	bowIdentityResetsAssistedBaseline();
	malformedRuntimeConfigurationFailsClosed();
	realisticContinuousMotionRemainsAccepted();

	if(g_failures != 0) {
		std::cerr << g_failures << " bow-runtime test(s) failed\n";
		return 1;
	}
	std::cout << "VrBowRuntime: assisted-draw and tracking guards passed\n";
	return 0;
}
