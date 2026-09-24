#include "vr/VrWeaponSystem.h"

#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char * message) {
	if(!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

arxvr::VrWeaponTrackingSample trackedSample(std::uint64_t token) {
	arxvr::VrWeaponTrackingSample sample;
	sample.weaponToken = token;
	sample.primaryPosition = { 0.f, 0.f, 0.f };
	sample.primaryForward = { 0.f, 0.f, 1.f };
	sample.primaryUp = { 0.f, 1.f, 0.f };
	sample.primaryValid = true;
	sample.secondaryPosition = { 0.f, 0.f, -28.f };
	sample.secondaryValid = true;
	sample.secondaryGripPressed = true;
	return sample;
}

void gripButtonReleaseDropsAndCanReacquireConstraint() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = trackedSample(0x701u);

	expect(system.update(profile, sample).twoHanded,
	       "valid secondary grip should engage two-handed mode");

	sample.secondaryGripPressed = false;
	const arxvr::VrWeaponPose released = system.update(profile, sample);
	expect(released.valid && !released.twoHanded && !system.twoHanded(),
	       "releasing the physical off-hand grip must immediately drop the constraint");

	sample.secondaryGripPressed = true;
	expect(system.update(profile, sample).twoHanded,
	       "pressing grip again near the profile anchor should reacquire the constraint");
}

void offHandTrackingLossDropsConstraintWithoutInvalidatingPrimaryPose() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::TwoHanded);
	arxvr::VrWeaponSystem system;
	auto sample = trackedSample(0x702u);

	expect(system.update(profile, sample).twoHanded,
	       "control sample should engage before secondary tracking loss");

	sample.secondaryValid = false;
	const arxvr::VrWeaponPose fallback = system.update(profile, sample);
	expect(fallback.valid,
	       "loss of the off hand should retain a valid one-handed dominant pose");
	expect(!fallback.twoHanded && !system.twoHanded(),
	       "loss of off-hand tracking must release the two-hand latch fail-closed");

	sample.secondaryValid = true;
	expect(system.update(profile, sample).twoHanded,
	       "restored tracked off-hand grip near the anchor should reacquire cleanly");
}

} // namespace

int main() {
	gripButtonReleaseDropsAndCanReacquireConstraint();
	offHandTrackingLossDropsConstraintWithoutInvalidatingPrimaryPose();

	if(g_failures != 0) {
		std::cerr << g_failures << " secondary-grip test(s) failed\n";
		return 1;
	}
	std::cout << "VrWeaponSystem: secondary grip lifecycle tests passed\n";
	return 0;
}
