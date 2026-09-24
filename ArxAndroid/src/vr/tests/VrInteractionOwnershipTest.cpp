#include "vr/VrInteractionSystem.h"

#include <cstdint>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char * message) {
	if(!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

arxvr::VrImpactSample fistSample(float x, float y, std::uint64_t timeUs) {
	arxvr::VrImpactSample sample;
	sample.motion = { x, y, 0.f, timeUs };
	sample.source = arxvr::VrImpactSource::Fist;
	sample.gestureActive = true;
	sample.trackingValid = true;
	return sample;
}

void feedFistSwing(arxvr::VrInteractionSystem & system, std::uint64_t startUs,
                   float offsetX) {
	system.updateHand(arxvr::VrHand::Left, fistSample(offsetX + 0.f, 0.f, startUs + 0));
	system.updateHand(arxvr::VrHand::Left, fistSample(offsetX + 2.f, 0.2f, startUs + 20000));
	system.updateHand(arxvr::VrHand::Left, fistSample(offsetX + 5.f, 0.6f, startUs + 40000));
	system.updateHand(arxvr::VrHand::Left, fistSample(offsetX + 9.f, 1.2f, startUs + 60000));
	system.updateHand(arxvr::VrHand::Left, fistSample(offsetX + 14.f, 2.f, startUs + 80000));
}

void ownershipResetDiscardsBankedFistTrajectory() {
	arxvr::VrInteractionSystem system;
	feedFistSwing(system, 0, 0.f);
	expect(system.canImpact(arxvr::VrHand::Left),
	       "control fist swing should qualify before ownership transfer");

	// The live two-hand weapon adapter calls this while the off hand is latched
	// to the secondary grip. A pre-existing fist trajectory must not survive the
	// interval in which the weapon system owns that physical hand.
	system.resetGesture(arxvr::VrHand::Left);
	expect(!system.canImpact(arxvr::VrHand::Left),
	       "secondary-grip ownership must discard a banked fist qualification");
	arxvr::VrImpactEvent staleImpact;
	expect(!system.consumeImpact(arxvr::VrHand::Left, staleImpact),
	       "discarded fist motion must not emit after the hand changes owner");

	feedFistSwing(system, 120000, 30.f);
	expect(system.canImpact(arxvr::VrHand::Left),
	       "a genuinely new fist swing should qualify after ownership returns");
}

void ownershipResetPreservesConsumedImpactGuards() {
	arxvr::VrInteractionSystem system;
	feedFistSwing(system, 0, 0.f);
	arxvr::VrImpactEvent impact;
	expect(system.consumeImpact(arxvr::VrHand::Left, impact),
	       "control impact should establish cooldown and retraction guards");

	system.resetGesture(arxvr::VrHand::Left);
	feedFistSwing(system, 100000, 30.f);
	expect(!system.canImpact(arxvr::VrHand::Left),
	       "ownership reset must not erase the cooldown established by a consumed hit");
	arxvr::VrImpactEvent tooEarly;
	expect(!system.consumeImpact(arxvr::VrHand::Left, tooEarly),
	       "a two-hand ownership transition must not become a cooldown bypass");
}

} // namespace

int main() {
	ownershipResetDiscardsBankedFistTrajectory();
	ownershipResetPreservesConsumedImpactGuards();

	if(g_failures != 0) {
		std::cerr << g_failures << " interaction ownership test(s) failed\n";
		return 1;
	}
	std::cout << "VrInteractionSystem: hand ownership transition tests passed\n";
	return 0;
}
