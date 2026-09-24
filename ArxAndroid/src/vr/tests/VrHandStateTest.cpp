#include "vr/VrHandState.h"

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

arxvr::VrImpactSample sample(float x, float y, std::uint64_t timeUs,
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

void feedSwing(arxvr::VrHandState & hand, std::uint64_t startUs,
               float offsetX, arxvr::VrImpactSource source,
               std::uint64_t sourceToken) {
	hand.update(sample(offsetX + 0.f, 0.f, startUs + 0,
	                   source, sourceToken, true));
	hand.update(sample(offsetX + 2.f, 0.2f, startUs + 20000,
	                   source, sourceToken, true));
	hand.update(sample(offsetX + 5.f, 0.6f, startUs + 40000,
	                   source, sourceToken, true));
	hand.update(sample(offsetX + 9.f, 1.2f, startUs + 60000,
	                   source, sourceToken, true));
	hand.update(sample(offsetX + 14.f, 2.f, startUs + 80000,
	                   source, sourceToken, true));
}

void qualifiedImpactCarriesMotionSnapshot() {
	arxvr::VrHandState hand;
	feedSwing(hand, 0, 0.f, arxvr::VrImpactSource::Fist, 0);
	expect(hand.canImpact(), "deliberate fist swing should qualify");

	arxvr::VrQualifiedImpact impact;
	expect(hand.consumeQualifiedImpact(impact),
	       "qualified fist swing should produce one impact snapshot");
	expect(impact.source == arxvr::VrImpactSource::Fist,
	       "snapshot should retain the semantic impact source");
	expect(impact.sourceToken == 0,
	       "fist snapshot should retain its source token");
	expect(impact.motion.timestampUs == 80000,
	       "snapshot should retain the latest tracked motion timestamp");
	expect(impact.motion.x == 14.f,
	       "snapshot should retain the latest tracked position");
	expect(impact.metrics.valid && impact.metrics.sampleCount >= 3,
	       "snapshot should carry the qualifying motion metrics");
	expect(impact.metrics.pathLength >= hand.profile().minPathLength,
	       "snapshot path length should satisfy the active profile");
	expect(!hand.armed(), "consuming an impact should disarm the hand gate");
}

void fistToHeldObjectPreservesHandCooldown() {
	arxvr::VrHandState hand;
	feedSwing(hand, 0, 0.f, arxvr::VrImpactSource::Fist, 0);
	arxvr::VrQualifiedImpact fistImpact;
	expect(hand.consumeQualifiedImpact(fistImpact),
	       "initial fist impact should be consumable");
	const std::uint64_t fistRearmAt = hand.rearmNotBeforeUs();
	expect(fistRearmAt == 430000,
	       "fist impact should establish the fist-profile cooldown");

	// Change semantic source immediately. The held-object profile has a shorter
	// cooldown, but the already-established fist deadline belongs to the hand and
	// must not be shortened by equipping/grabbing something.
	feedSwing(hand, 100000, 30.f, arxvr::VrImpactSource::HeldObject, 77);
	expect(hand.source() == arxvr::VrImpactSource::HeldObject,
	       "hand should transition to the held-object source");
	expect(hand.sourceToken() == 77,
	       "held object identity should be retained");
	expect(hand.profile().cooldownUs == arxvr::vrHeldObjectStrikeProfile().cooldownUs,
	       "held-object source should activate the held-object profile");
	expect(hand.rearmNotBeforeUs() == fistRearmAt,
	       "source/profile transition must preserve the previous cooldown deadline");
	expect(!hand.canImpact(),
	       "a held-object swing during the fist cooldown must not qualify");

	// Feed ordinary low-speed tracked motion continuously so this is a normal
	// rearm rather than a synthetic time jump / tracking discontinuity.
	hand.update(sample(44.1f, 2.f, 260000,
	                   arxvr::VrImpactSource::HeldObject, 77, false));
	hand.update(sample(44.2f, 2.f, 350000,
	                   arxvr::VrImpactSource::HeldObject, 77, false));
	hand.update(sample(44.3f, 2.f, 440000,
	                   arxvr::VrImpactSource::HeldObject, 77, false));
	expect(hand.armed(),
	       "slow tracked motion after the original cooldown should rearm the hand");

	feedSwing(hand, 460000, 44.3f, arxvr::VrImpactSource::HeldObject, 77);
	expect(hand.canImpact(),
	       "fresh held-object swing after neutral rearm should qualify");
	arxvr::VrQualifiedImpact heldImpact;
	expect(hand.consumeQualifiedImpact(heldImpact),
	       "rearmed held-object swing should produce an impact");
	expect(heldImpact.source == arxvr::VrImpactSource::HeldObject,
	       "second impact should be reported as a held-object impact");
}

void sourceTransitionCannotReuseBankedMotion() {
	arxvr::VrHandState hand;
	const auto fist = arxvr::VrImpactSource::Fist;
	const auto held = arxvr::VrImpactSource::HeldObject;

	// Build a complete high-energy path while the fist gesture is inactive.
	hand.update(sample(0.f, 0.f, 0, fist, 0, false));
	hand.update(sample(2.f, 0.2f, 20000, fist, 0, false));
	hand.update(sample(5.f, 0.6f, 40000, fist, 0, false));
	hand.update(sample(9.f, 1.2f, 60000, fist, 0, false));
	hand.update(sample(14.f, 2.f, 80000, fist, 0, false));

	const auto transition = hand.update(sample(14.f, 2.f, 100000,
	                                           held, 501, true));
	expect(transition == arxvr::VrImpactGateStatus::Building,
	       "changing source should establish a fresh impact path");
	expect(hand.metrics().sampleCount == 1,
	       "profile/source transition should discard banked motion history");
	expect(!hand.canImpact(),
	       "fist motion must not become held-object damage after a source switch");

	feedSwing(hand, 120000, 14.f, held, 501);
	expect(hand.canImpact(),
	       "new held-object motion after the transition should qualify normally");
}

void heldObjectSwapCannotResetCooldown() {
	arxvr::VrHandState hand;
	const auto held = arxvr::VrImpactSource::HeldObject;
	feedSwing(hand, 0, 0.f, held, 101);
	arxvr::VrQualifiedImpact first;
	expect(hand.consumeQualifiedImpact(first),
	       "first held-object swing should be consumed");
	const std::uint64_t rearmAt = hand.rearmNotBeforeUs();

	feedSwing(hand, 100000, 30.f, held, 202);
	expect(hand.sourceToken() == 202,
	       "hand should track the newly held object identity");
	expect(hand.rearmNotBeforeUs() == rearmAt,
	       "object-token transition must preserve hand cooldown");
	expect(!hand.canImpact(),
	       "rapidly swapping held objects must not create an extra impact");
}

void trackingLossFailsClosedAcrossSourceChanges() {
	arxvr::VrHandState hand;
	const auto fist = arxvr::VrImpactSource::Fist;
	hand.update(sample(0.f, 0.f, 0, fist, 0, false));
	const auto lost = hand.update(sample(0.f, 0.f, 100000,
	                                     fist, 0, false, false));
	expect(lost == arxvr::VrImpactGateStatus::Invalid,
	       "tracking loss should fail closed at the per-hand layer");
	expect(!hand.armed(), "tracking loss should disarm the hand");
	const std::uint64_t lostRearmAt = hand.rearmNotBeforeUs();

	// Even changing to a held object cannot erase the tracking-loss deadline.
	hand.update(sample(0.f, 0.f, 180000,
	                   arxvr::VrImpactSource::HeldObject, 90, false));
	expect(hand.rearmNotBeforeUs() == lostRearmAt,
	       "source change after tracking loss must preserve recovery cooldown");
	expect(!hand.canImpact(),
	       "source change after tracking loss must remain fail-closed");
}

void sessionResetIsExplicitlyDifferentFromGestureReset() {
	arxvr::VrHandState hand;
	feedSwing(hand, 0, 0.f, arxvr::VrImpactSource::Fist, 0);
	arxvr::VrQualifiedImpact impact;
	expect(hand.consumeQualifiedImpact(impact), "fixture impact should be consumed");
	expect(!hand.armed(), "fixture should leave hand in cooldown");

	hand.resetGesture();
	expect(!hand.armed(), "gesture reset must preserve active cooldown");
	hand.resetSession();
	expect(hand.armed(), "explicit session reset should restore initial armed state");
	expect(hand.source() == arxvr::VrImpactSource::None,
	       "session reset should clear the semantic source");
	expect(hand.rearmNotBeforeUs() == 0,
	       "session reset should clear the cooldown deadline");
}

} // namespace

int main() {
	qualifiedImpactCarriesMotionSnapshot();
	fistToHeldObjectPreservesHandCooldown();
	sourceTransitionCannotReuseBankedMotion();
	heldObjectSwapCannotResetCooldown();
	trackingLossFailsClosedAcrossSourceChanges();
	sessionResetIsExplicitlyDifferentFromGestureReset();

	if(g_failures != 0) {
		std::cerr << g_failures << " hand-impact test(s) failed\n";
		return 1;
	}
	std::cout << "VrHandState: unified per-hand impact tests passed\n";
	return 0;
}
