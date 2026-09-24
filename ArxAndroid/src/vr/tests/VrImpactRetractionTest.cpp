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

arxvr::VrImpactSample sample(float x, std::uint64_t timeUs,
                             arxvr::VrImpactSource source,
                             std::uint64_t sourceToken = 0,
                             bool gestureActive = true) {
	arxvr::VrImpactSample result;
	result.motion = { x, 0.f, 0.f, timeUs };
	result.source = source;
	result.sourceToken = sourceToken;
	result.gestureActive = gestureActive;
	result.trackingValid = true;
	return result;
}

void feedQualifyingSwing(arxvr::VrImpactGate & gate,
                         arxvr::VrImpactSource source,
                         std::uint64_t sourceToken = 0) {
	gate.update(sample(0.f, 0, source, sourceToken));
	gate.update(sample(2.f, 20000, source, sourceToken));
	gate.update(sample(5.f, 40000, source, sourceToken));
	gate.update(sample(9.f, 60000, source, sourceToken));
	gate.update(sample(14.f, 80000, source, sourceToken));
}

void cooldownAloneCannotRearmAtContactPoint() {
	arxvr::VrImpactGate gate(arxvr::vrFistStrikeProfile());
	feedQualifyingSwing(gate, arxvr::VrImpactSource::Fist);
	expect(gate.canImpact(), "fixture fist swing should qualify");
	expect(gate.consumeImpact(), "fixture fist swing should be consumed");
	expect(!gate.rearmDistanceSatisfied(),
	       "consuming an impact should arm the physical-retraction gate");
	expect(gate.requiredRearmDistance() == arxvr::vrFistStrikeProfile().minRearmDistance,
	       "fist impact should retain the fist retraction distance");

	// Stay almost exactly at the impact point until after the temporal cooldown.
	// The low-speed samples are enough for the strike classifier to rearm, but
	// the impact gate must still reject a second hit until the hand has actually
	// left the previous contact region.
	gate.update(sample(14.1f, 180000, arxvr::VrImpactSource::Fist, 0, false));
	gate.update(sample(14.2f, 280000, arxvr::VrImpactSource::Fist, 0, false));
	gate.update(sample(14.3f, 380000, arxvr::VrImpactSource::Fist, 0, false));
	gate.update(sample(14.4f, 440000, arxvr::VrImpactSource::Fist, 0, false));

	expect(gate.armed(),
	       "temporal cooldown plus slow motion should rearm the classifier");
	expect(!gate.rearmDistanceSatisfied(),
	       "remaining at the contact point must keep the impact gate blocked");
	expect(!gate.canImpact(),
	       "time-only cooldown must not create a second impact opportunity");

	// Retract slowly far enough to leave the contact region. The crossing sample
	// becomes a fresh baseline, so the retract motion itself cannot be banked as
	// part of the next damaging swing.
	gate.update(sample(12.f, 520000, arxvr::VrImpactSource::Fist, 0, false));
	gate.update(sample(9.f, 620000, arxvr::VrImpactSource::Fist, 0, false));
	expect(gate.rearmDistanceSatisfied(),
	       "moving the configured distance from the impact point should release the gate");
	expect(gate.metrics().sampleCount == 1,
	       "the retraction boundary should establish a fresh motion baseline");
	expect(!gate.canImpact(),
	       "retraction itself must not be interpreted as a new strike");
}

void sourceChangeCannotReduceOutstandingRetractionDistance() {
	arxvr::VrImpactGate gate(arxvr::vrHeldObjectStrikeProfile());
	feedQualifyingSwing(gate, arxvr::VrImpactSource::HeldObject, 77);
	expect(gate.canImpact(), "fixture held-object swing should qualify");
	expect(gate.consumeImpact(), "fixture held-object swing should be consumed");
	const float heldDistance = arxvr::vrHeldObjectStrikeProfile().minRearmDistance;
	expect(gate.requiredRearmDistance() == heldDistance,
	       "held-object impact should establish the held-object retraction distance");

	gate.transitionProfile(arxvr::vrFistStrikeProfile());
	expect(gate.profile().minRearmDistance
	       == arxvr::vrFistStrikeProfile().minRearmDistance,
	       "source transition should install the new strike profile");
	expect(gate.requiredRearmDistance() == heldDistance,
	       "profile transition must preserve the distance established by the consumed impact");
	expect(!gate.rearmDistanceSatisfied(),
	       "profile transition must not clear the outstanding physical retraction");
}

void trackingTeleportCannotSatisfyRetraction() {
	arxvr::VrImpactGate gate(arxvr::vrFistStrikeProfile());
	feedQualifyingSwing(gate, arxvr::VrImpactSource::Fist);
	expect(gate.consumeImpact(), "fixture impact should be consumed before teleport test");

	const auto status = gate.update(sample(1000.f, 100000,
	                                      arxvr::VrImpactSource::Fist));
	expect(status == arxvr::VrImpactGateStatus::TrackingReset,
	       "implausible position jump should remain a tracking discontinuity");
	expect(!gate.rearmDistanceSatisfied(),
	       "tracking teleport must not count as physical retraction");
	expect(!gate.canImpact(),
	       "tracking discontinuity must leave the impact path fail-closed");
}

} // namespace

int main() {
	cooldownAloneCannotRearmAtContactPoint();
	sourceChangeCannotReduceOutstandingRetractionDistance();
	trackingTeleportCannotSatisfyRetraction();

	if(g_failures != 0) {
		std::cerr << g_failures << " impact-retraction test(s) failed\n";
		return 1;
	}
	std::cout << "VrImpactGate: physical retraction tests passed\n";
	return 0;
}
