#include "vr/VrImpactGate.h"

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

arxvr::VrImpactSample impactSample(float x, float y, std::uint64_t timeUs,
                                   arxvr::VrImpactSource source,
                                   std::uint64_t sourceToken,
                                   bool gestureActive,
                                   bool trackingValid = true) {
	arxvr::VrImpactSample result;
	result.motion = { x, y, 0.f, timeUs };
	result.source = source;
	result.sourceToken = sourceToken;
	result.gestureActive = gestureActive;
	result.trackingValid = trackingValid;
	return result;
}

void feedActivePunch(arxvr::VrImpactGate & gate, std::uint64_t startUs,
                     float offsetX, arxvr::VrImpactSource source,
                     std::uint64_t sourceToken) {
	gate.update(impactSample(offsetX + 0.f, 0.f, startUs + 0,
	                         source, sourceToken, true));
	gate.update(impactSample(offsetX + 2.f, 0.2f, startUs + 20000,
	                         source, sourceToken, true));
	gate.update(impactSample(offsetX + 5.f, 0.6f, startUs + 40000,
	                         source, sourceToken, true));
	gate.update(impactSample(offsetX + 9.f, 1.2f, startUs + 60000,
	                         source, sourceToken, true));
	gate.update(impactSample(offsetX + 14.f, 2.f, startUs + 80000,
	                         source, sourceToken, true));
}

void preGestureMotionCannotBecomeAFistHit() {
	arxvr::VrImpactGate gate(arxvr::vrFistStrikeProfile());
	const auto source = arxvr::VrImpactSource::Fist;

	// Fast open-hand motion is deliberately identical to the qualifying punch
	// fixture. Closing the fist at the end must establish a fresh baseline.
	gate.update(impactSample(0.f, 0.f, 0, source, 0, false));
	gate.update(impactSample(2.f, 0.2f, 20000, source, 0, false));
	gate.update(impactSample(5.f, 0.6f, 40000, source, 0, false));
	gate.update(impactSample(9.f, 1.2f, 60000, source, 0, false));
	gate.update(impactSample(14.f, 2.f, 80000, source, 0, false));
	const auto closeStatus = gate.update(impactSample(14.f, 2.f, 100000,
	                                                   source, 0, true));
	expect(closeStatus == arxvr::VrImpactGateStatus::Building,
	       "closing fist after open-hand motion should start a fresh gesture");
	expect(gate.metrics().sampleCount == 1,
	       "fist close should discard open-hand strike history");
	expect(!gate.canImpact(), "open-hand motion must not leak into a fist impact");

	feedActivePunch(gate, 120000, 14.f, source, 0);
	expect(gate.canImpact(), "deliberate motion after fist close should qualify");
	expect(gate.consumeImpact(), "qualified fist impact should be consumable");
}

void heldObjectSourceSwitchPreservesCooldown() {
	arxvr::VrImpactGate gate(arxvr::vrHeldObjectStrikeProfile());
	const auto source = arxvr::VrImpactSource::HeldObject;
	feedActivePunch(gate, 0, 0.f, source, 101);
	expect(gate.canImpact(), "first held-object swing should qualify");
	expect(gate.consumeImpact(), "first held-object impact should be consumed");
	const std::uint64_t rearmAt = gate.rearmNotBeforeUs();

	// Simulate dropping object 101 and immediately gripping object 202. The new
	// entity identity resets motion history but must not reset the hand cooldown.
	gate.resetGesture();
	feedActivePunch(gate, 100000, 30.f, source, 202);
	expect(gate.sourceToken() == 202, "impact gate should track the new object identity");
	expect(!gate.armed(), "object swap must not rearm a consumed impact");
	expect(gate.rearmNotBeforeUs() == rearmAt,
	       "object swap must preserve the original cooldown deadline");
	expect(!gate.canImpact(), "rapid object swap must not create a second impact");
}

void trackingLossRequiresNeutralRearm() {
	arxvr::VrImpactGate gate(arxvr::vrFistStrikeProfile());
	const auto source = arxvr::VrImpactSource::Fist;
	gate.update(impactSample(0.f, 0.f, 0, source, 0, false));
	const auto lost = gate.update(impactSample(0.f, 0.f, 100000, source, 0,
	                                           false, false));
	expect(lost == arxvr::VrImpactGateStatus::Invalid,
	       "explicit tracking loss should fail closed");
	expect(!gate.armed(), "tracking loss should disarm the impact gate");
	expect(!gate.gestureActive(), "tracking loss should cancel any active gesture");

	gate.update(impactSample(0.1f, 0.f, 200000, source, 0, false));
	gate.update(impactSample(0.2f, 0.f, 300000, source, 0, false));
	gate.update(impactSample(0.3f, 0.f, 400000, source, 0, false));
	gate.update(impactSample(0.4f, 0.f, 460000, source, 0, false));
	expect(gate.armed(), "slow neutral hand after tracking-loss cooldown should rearm");
	expect(gate.metrics().sampleCount == 1,
	       "tracking-loss rearm should start a fresh motion history");

	feedActivePunch(gate, 480000, 0.4f, source, 0);
	expect(gate.canImpact(), "fresh post-recovery fist swing should qualify");
}

void heldObjectCurvedSwingUsesSamePipeline() {
	arxvr::VrImpactGate gate(arxvr::vrHeldObjectStrikeProfile());
	const auto source = arxvr::VrImpactSource::HeldObject;
	gate.update(impactSample(0.f, 0.f, 0, source, 77, true));
	gate.update(impactSample(2.f, 0.6f, 20000, source, 77, true));
	gate.update(impactSample(5.f, 1.7f, 40000, source, 77, true));
	gate.update(impactSample(9.f, 3.3f, 60000, source, 77, true));
	const auto status = gate.update(impactSample(14.f, 5.5f, 80000,
	                                             source, 77, true));
	expect(status == arxvr::VrImpactGateStatus::Qualified,
	       "curved held-object swing should qualify through common impact gate");
	expect(gate.metrics().directionalConsistency > 0.9f,
	       "held-object swing should retain directional consistency metric");
}

void inactiveSourceNeverReportsImpact() {
	arxvr::VrImpactGate gate(arxvr::vrFistStrikeProfile());
	feedActivePunch(gate, 0, 0.f, arxvr::VrImpactSource::None, 0);
	expect(!gate.canImpact(), "source=None must never produce an impact opportunity");
	expect(!gate.consumeImpact(), "source=None must not consume an impact");
}

} // namespace

int main() {
	preGestureMotionCannotBecomeAFistHit();
	heldObjectSourceSwitchPreservesCooldown();
	trackingLossRequiresNeutralRearm();
	heldObjectCurvedSwingUsesSamePipeline();
	inactiveSourceNeverReportsImpact();

	if(g_failures != 0) {
		std::cerr << g_failures << " impact-gate test(s) failed\n";
		return 1;
	}
	std::cout << "VrImpactGate: all deterministic interaction tests passed\n";
	return 0;
}
