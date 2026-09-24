#include "vr/VrWeaponSystem.h"

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

arxvr::VrWeaponTrackingSample baseSample(std::uint64_t token) {
	arxvr::VrWeaponTrackingSample sample;
	sample.weaponToken = token;
	sample.primaryPosition = { 0.f, 0.f, 0.f };
	sample.primaryForward = { 0.f, 0.f, 4.f };
	sample.primaryUp = { 0.f, 2.f, 0.f };
	sample.primaryValid = true;
	return sample;
}

void oneHandedWeaponFollowsTrackedPrimaryPose() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	arxvr::VrWeaponSystem system;
	auto sample = baseSample(0x101u);
	sample.secondaryPosition = { 0.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;

	const arxvr::VrWeaponPose pose = system.update(profile, sample);
	expect(pose.valid, "valid one-handed tracked pose should produce a weapon pose");
	expect(!pose.twoHanded && !system.twoHanded(),
	       "one-handed profile must not enter a secondary-hand constraint");
	expect(near(pose.forward.x, 0.f) && near(pose.forward.y, 0.f)
	       && near(pose.forward.z, 1.f),
	       "primary controller forward should be normalized");
	const arxvr::VrWeaponSegment segment = system.buildContactSegment(profile, pose);
	expect(segment.valid && near(segment.end.z, profile.reach),
	       "one-handed pose should feed existing physical contact geometry");
}

void twoHandedGripEngagesNearProfileAnchor() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = baseSample(0x201u);
	// Default secondaryGripLocal is 28 units behind the dominant grip.
	sample.secondaryPosition = { 2.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;

	const arxvr::VrWeaponPose pose = system.update(profile, sample);
	expect(pose.valid && pose.twoHanded && system.twoHanded(),
	       "two-handed profile should engage when the off-hand grips near its anchor");
	expect(pose.forward.z > 0.99f && pose.forward.x < 0.f,
	       "constrained weapon direction should follow off-hand to dominant-hand vector");
	expect(near(arxvr::vrVectorLength(pose.forward), 1.f),
	       "two-hand constrained forward must remain normalized");
}

void twoHandedGripUsesHysteresisAndReleasesCleanly() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponConstraintConfig config;
	config.secondaryEngageDistance = 10.f;
	config.secondaryReleaseDistance = 20.f;
	config.minimumHandSeparation = 5.f;
	arxvr::VrWeaponSystem system(config);
	auto sample = baseSample(0x301u);
	sample.secondaryPosition = { 0.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;
	expect(system.update(profile, sample).twoHanded,
	       "near secondary grip should engage constraint");

	// Once latched, release distance is measured against the constrained weapon
	// pose, so moving the hands farther apart must exceed the release radius
	// around the profile-defined secondary-grip distance.
	sample.secondaryPosition = { 0.f, 0.f, -43.f };
	expect(system.update(profile, sample).twoHanded,
	       "engaged two-hand grip should remain latched inside release hysteresis");

	sample.secondaryPosition = { 0.f, 0.f, -53.f };
	expect(!system.update(profile, sample).twoHanded,
	       "off-hand moving beyond release radius should return to one-hand mode");
	expect(!system.twoHanded(), "released constraint should update persistent state");
}

void latchedGripFollowsBothHandsInsteadOfPrimaryWristAim() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = baseSample(0x351u);
	sample.secondaryPosition = { 0.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;
	const arxvr::VrWeaponPose engaged = system.update(profile, sample);
	expect(engaged.twoHanded, "control pose should engage the secondary grip");

	// Rotate only the dominant controller's pointing direction by ninety degrees
	// while both physical hand positions remain in a valid two-hand arrangement.
	// A latched weapon should remain constrained by the hands rather than drop
	// because the one-hand wrist-derived secondary anchor moved elsewhere.
	sample.primaryForward = { 4.f, 0.f, 0.f };
	const arxvr::VrWeaponPose constrained = system.update(profile, sample);
	expect(constrained.twoHanded && system.twoHanded(),
	       "latched grip should not release from dominant-wrist aim changes alone");
	expect(near(constrained.forward.x, 0.f) && near(constrained.forward.y, 0.f)
	       && constrained.forward.z > 0.99f,
	       "latched weapon forward should continue to follow the vector between hands");
	expect(near(constrained.secondaryGripAnchor.z, -28.f, 0.01f),
	       "reported secondary anchor should be rebuilt from the final constrained pose");
}

void weaponSwapCannotCarrySecondaryConstraintAcrossItems() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = baseSample(0x401u);
	sample.secondaryPosition = { 0.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;
	expect(system.update(profile, sample).twoHanded,
	       "first weapon should engage the off-hand");

	sample.weaponToken = 0x402u;
	sample.secondaryGripPressed = false;
	const arxvr::VrWeaponPose swapped = system.update(profile, sample);
	expect(swapped.valid && !swapped.twoHanded,
	       "changing equipped weapon identity must clear the previous two-hand constraint");
	expect(system.weaponToken() == 0x402u,
	       "weapon system should expose the currently tracked weapon identity");
}

void trackingLossAndDegenerateHandsFailClosed() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = baseSample(0x501u);
	sample.secondaryPosition = { 0.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;
	expect(system.update(profile, sample).twoHanded,
	       "control sample should engage before tracking-loss test");

	sample.primaryValid = false;
	expect(!system.update(profile, sample).valid && !system.twoHanded(),
	       "primary tracking loss must invalidate pose and drop secondary constraint");

	sample = baseSample(0x502u);
	sample.secondaryPosition = sample.primaryPosition;
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;
	const arxvr::VrWeaponPose degenerate = system.update(profile, sample);
	expect(degenerate.valid && !degenerate.twoHanded,
	       "coincident hands must not create an undefined two-hand direction");

	sample.primaryForward.x = std::numeric_limits<float>::quiet_NaN();
	expect(!system.update(profile, sample).valid,
	       "non-finite primary orientation must fail closed");
}

void nearVerticalControllerStillBuildsStableBasis() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = baseSample(0x601u);
	sample.primaryForward = { 0.f, 1.f, 0.001f };
	sample.primaryUp = { 0.f, 1.f, 0.f };
	const arxvr::VrWeaponPose pose = system.update(profile, sample);
	expect(pose.valid,
	       "controller forward near global-up should use deterministic basis fallback");
	expect(near(arxvr::vrVectorLength(pose.forward), 1.f)
	       && near(arxvr::vrVectorLength(pose.up), 1.f),
	       "fallback basis vectors should remain normalized");
	expect(std::abs(arxvr::vrWeaponVectorDot(pose.forward, pose.up)) < 0.001f,
	       "fallback basis should remain orthogonal");
}

} // namespace

int main() {
	oneHandedWeaponFollowsTrackedPrimaryPose();
	twoHandedGripEngagesNearProfileAnchor();
	twoHandedGripUsesHysteresisAndReleasesCleanly();
	latchedGripFollowsBothHandsInsteadOfPrimaryWristAim();
	weaponSwapCannotCarrySecondaryConstraintAcrossItems();
	trackingLossAndDegenerateHandsFailClosed();
	nearVerticalControllerStillBuildsStableBasis();

	if(g_failures != 0) {
		std::cerr << g_failures << " weapon-system test(s) failed\n";
		return 1;
	}
	std::cout << "VrWeaponSystem: one/two-hand constraint tests passed\n";
	return 0;
}
