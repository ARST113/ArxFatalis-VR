#include "vr/VrBowSystem.h"

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

arxvr::VrBowTrackingSample baseSample(std::uint64_t token, std::uint64_t timestampUs) {
	arxvr::VrBowTrackingSample sample;
	sample.bowToken = token;
	sample.bowPosition = { 0.f, 0.f, 0.f };
	sample.bowForward = { 0.f, 0.f, 1.f };
	sample.bowUp = { 0.f, 1.f, 0.f };
	sample.bowValid = true;
	sample.stringPosition = { 0.f, 0.f, 0.f };
	sample.stringValid = true;
	sample.timestampUs = timestampUs;
	return sample;
}

void requiresDeliberateNockNearBowHand() {
	arxvr::VrBowSystem system;
	auto sample = baseSample(0x101u, 1000u);
	sample.stringPosition = { 0.f, 0.f, -30.f };
	sample.stringGripPressed = true;
	const arxvr::VrBowPose farPose = system.update(sample);
	expect(farPose.valid && !farPose.nocked && !system.nocked(),
	       "grip far from the nock anchor must not acquire a bow string");

	sample.timestampUs = 2000u;
	sample.stringPosition = { 3.f, 0.f, -4.f };
	const arxvr::VrBowPose nearPose = system.update(sample);
	expect(nearPose.valid && nearPose.nocked && system.nocked(),
	       "grip close to the physical nock anchor should acquire the string");
}

void drawUsesBothHandsAndClampsEnergy() {
	arxvr::VrBowConfig config;
	config.maximumDrawDistance = 60.f;
	arxvr::VrBowSystem system(config);
	auto sample = baseSample(0x201u, 1000u);
	sample.stringGripPressed = true;
	expect(system.update(sample).nocked, "control sample should nock the bow");

	sample.timestampUs = 2000u;
	sample.stringPosition = { -20.f, 0.f, -40.f };
	const arxvr::VrBowPose drawn = system.update(sample);
	expect(drawn.nocked && drawn.drawDistance > 44.f && drawn.drawDistance < 45.f,
	       "draw distance should be measured from physical hand separation");
	expect(drawn.aimDirection.x > 0.44f && drawn.aimDirection.z > 0.89f,
	       "aim direction should run from string hand through bow hand");
	expect(near(arxvr::vrVectorLength(drawn.aimDirection), 1.f),
	       "physical bow aim direction must remain normalized");

	sample.timestampUs = 3000u;
	sample.stringPosition = { 0.f, 0.f, -120.f };
	const arxvr::VrBowPose overdrawn = system.update(sample);
	expect(near(overdrawn.drawDistance, 60.f) && near(overdrawn.drawRatio, 1.f),
	       "tracking beyond maximum draw must clamp charge instead of adding energy");
}

void qualifiedReleaseEmitsOneShotEvent() {
	arxvr::VrBowConfig config;
	config.maximumDrawDistance = 50.f;
	config.minimumReleaseDraw = 10.f;
	config.arrowSpawnOffset = 6.f;
	arxvr::VrBowSystem system(config);
	auto sample = baseSample(0x301u, 1000u);
	sample.stringGripPressed = true;
	system.update(sample);

	sample.timestampUs = 2000u;
	sample.stringPosition = { 0.f, 0.f, -25.f };
	const arxvr::VrBowPose drawing = system.update(sample);
	expect(near(drawing.drawRatio, 0.5f), "half physical draw should map to half charge");

	sample.timestampUs = 3000u;
	sample.stringGripPressed = false;
	const arxvr::VrBowPose releasedPose = system.update(sample);
	expect(releasedPose.nocked && !system.nocked(),
	       "release frame should report final nocked geometry then clear persistent state");

	arxvr::VrBowRelease release;
	expect(system.consumeRelease(release) && release.valid,
	       "qualified grip release should emit a bow-release event");
	expect(release.bowToken == 0x301u && near(release.drawDistance, 25.f)
	       && near(release.drawRatio, 0.5f),
	       "release should preserve bow identity and physical draw strength");
	expect(release.direction.z > 0.999f && near(release.origin.z, 6.f),
	       "release should publish physical aim and an origin in front of the bow hand");

	arxvr::VrBowRelease duplicate;
	expect(!system.consumeRelease(duplicate) && !duplicate.valid,
	       "a physical string release must be consumable exactly once");
}

void shortReleaseCancelsWithoutLaunching() {
	arxvr::VrBowConfig config;
	config.minimumReleaseDraw = 12.f;
	arxvr::VrBowSystem system(config);
	auto sample = baseSample(0x401u, 1000u);
	sample.stringGripPressed = true;
	system.update(sample);

	sample.timestampUs = 2000u;
	sample.stringPosition = { 0.f, 0.f, -5.f };
	system.update(sample);
	sample.timestampUs = 3000u;
	sample.stringGripPressed = false;
	system.update(sample);

	arxvr::VrBowRelease release;
	expect(!system.consumeRelease(release) && !release.valid,
	       "an accidental short draw should cancel instead of firing");
	expect(!system.nocked(), "cancelled short release should leave bow unnocked");
}

void trackingLossAndTimestampRegressionFailClosed() {
	arxvr::VrBowSystem system;
	auto sample = baseSample(0x501u, 1000u);
	sample.stringGripPressed = true;
	system.update(sample);

	sample.timestampUs = 2000u;
	sample.stringPosition = { 0.f, 0.f, -30.f };
	system.update(sample);
	sample.timestampUs = 3000u;
	sample.stringValid = false;
	sample.stringGripPressed = false;
	const arxvr::VrBowPose lost = system.update(sample);
	expect(lost.valid && !lost.nocked && !system.nocked(),
	       "string-hand tracking loss must cancel a pending physical shot");
	arxvr::VrBowRelease release;
	expect(!system.consumeRelease(release),
	       "tracking loss must never synthesize a release from stale coordinates");

	sample = baseSample(0x502u, 5000u);
	sample.stringGripPressed = true;
	system.update(sample);
	sample.timestampUs = 4000u;
	sample.stringPosition = { 0.f, 0.f, -30.f };
	expect(!system.update(sample).nocked && !system.nocked(),
	       "timestamp regression while drawing must fail closed and require re-nocking");
}

void weaponIdentityAndMalformedPoseResetState() {
	arxvr::VrBowSystem system;
	auto sample = baseSample(0x601u, 1000u);
	sample.stringGripPressed = true;
	expect(system.update(sample).nocked, "first bow should nock before identity test");

	sample.bowToken = 0x602u;
	sample.timestampUs = 2000u;
	sample.stringGripPressed = false;
	const arxvr::VrBowPose swapped = system.update(sample);
	expect(swapped.valid && !swapped.nocked && !system.nocked(),
	       "changing equipped bow identity must discard the old string latch");
	expect(system.bowToken() == 0x602u, "bow system should expose current bow identity");

	sample = baseSample(0x603u, 3000u);
	sample.bowForward.x = std::numeric_limits<float>::quiet_NaN();
	expect(!system.update(sample).valid,
	       "non-finite bow-controller orientation must fail closed");
}

void nearVerticalBowPoseBuildsStableBasis() {
	arxvr::VrBowSystem system;
	auto sample = baseSample(0x701u, 1000u);
	sample.bowForward = { 0.f, 1.f, 0.001f };
	sample.bowUp = { 0.f, 1.f, 0.f };
	const arxvr::VrBowPose pose = system.update(sample);
	expect(pose.valid, "near-collinear bow forward/up should use deterministic basis fallback");
	expect(near(arxvr::vrVectorLength(pose.aimDirection), 1.f)
	       && near(arxvr::vrVectorLength(pose.up), 1.f),
	       "bow fallback basis vectors should remain normalized");
	expect(std::abs(arxvr::vrBowDot(pose.aimDirection, pose.up)) < 0.001f,
	       "bow fallback basis should remain orthogonal");
}

} // namespace

int main() {
	requiresDeliberateNockNearBowHand();
	drawUsesBothHandsAndClampsEnergy();
	qualifiedReleaseEmitsOneShotEvent();
	shortReleaseCancelsWithoutLaunching();
	trackingLossAndTimestampRegressionFailClosed();
	weaponIdentityAndMalformedPoseResetState();
	nearVerticalBowPoseBuildsStableBasis();

	if(g_failures != 0) {
		std::cerr << g_failures << " bow-system test(s) failed\n";
		return 1;
	}
	std::cout << "VrBowSystem: two-hand nock/draw/release tests passed\n";
	return 0;
}
