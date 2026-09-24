#include "vr/VrInteractionSystem.h"

#include <cmath>
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

void feedSwing(arxvr::VrInteractionSystem & system, arxvr::VrHand hand,
               std::uint64_t startUs, float offsetX,
               arxvr::VrImpactSource source, std::uint64_t sourceToken) {
	system.updateHand(hand, sample(offsetX + 0.f, 0.f, startUs + 0,
	                               source, sourceToken, true));
	system.updateHand(hand, sample(offsetX + 2.f, 0.2f, startUs + 20000,
	                               source, sourceToken, true));
	system.updateHand(hand, sample(offsetX + 5.f, 0.6f, startUs + 40000,
	                               source, sourceToken, true));
	system.updateHand(hand, sample(offsetX + 9.f, 1.2f, startUs + 60000,
	                               source, sourceToken, true));
	system.updateHand(hand, sample(offsetX + 14.f, 2.f, startUs + 80000,
	                               source, sourceToken, true));
}

void semanticFistEventCarriesHandAndKinematics() {
	arxvr::VrInteractionSystem system;
	feedSwing(system, arxvr::VrHand::Right, 0, 0.f,
	          arxvr::VrImpactSource::Fist, 0);
	expect(system.canImpact(arxvr::VrHand::Right),
	       "deliberate right-hand fist swing should qualify");

	arxvr::VrImpactEvent impact;
	expect(system.consumeImpact(arxvr::VrHand::Right, impact),
	       "qualified fist swing should emit a semantic impact event");
	expect(impact.hand == arxvr::VrHand::Right,
	       "interaction service should stamp the physical hand");
	expect(impact.type == arxvr::VrImpactType::Fist,
	       "fist source should map to a fist semantic event");
	expect(impact.source == arxvr::VrImpactSource::Fist,
	       "event should retain the qualification source");
	expect(impact.position.x == 14.f && impact.position.y == 2.f,
	       "event should expose the tracked impact position");
	expect(impact.linearVelocity.x > 0.f,
	       "event should expose terminal tracked linear velocity");
	expect(impact.direction.x > 0.f,
	       "event direction should follow the terminal swing segment");
	const float directionLength = std::sqrt(impact.direction.x * impact.direction.x
	                                      + impact.direction.y * impact.direction.y
	                                      + impact.direction.z * impact.direction.z);
	expect(directionLength > 0.99f && directionLength < 1.01f,
	       "non-zero event direction should be normalized");
	expect(impact.metrics.pathLength
	       >= arxvr::vrFistStrikeProfile().minPathLength,
	       "event should retain anti-waggle path metrics");
}

void handsOwnIndependentQualificationState() {
	arxvr::VrInteractionSystem system;
	feedSwing(system, arxvr::VrHand::Right, 0, 0.f,
	          arxvr::VrImpactSource::Fist, 0);
	arxvr::VrImpactEvent rightImpact;
	expect(system.consumeImpact(arxvr::VrHand::Right, rightImpact),
	       "right-hand fixture should consume an impact");

	feedSwing(system, arxvr::VrHand::Left, 0, 0.f,
	          arxvr::VrImpactSource::Fist, 0);
	expect(system.canImpact(arxvr::VrHand::Left),
	       "right-hand cooldown must not suppress the left hand");
	arxvr::VrImpactEvent leftImpact;
	expect(system.consumeImpact(arxvr::VrHand::Left, leftImpact),
	       "left hand should independently emit an impact");
	expect(leftImpact.hand == arxvr::VrHand::Left,
	       "left-hand event should preserve physical-hand identity");
}

void heldObjectBecomesWeaponBearingSemanticEvent() {
	arxvr::VrInteractionSystem system;
	feedSwing(system, arxvr::VrHand::Right, 0, 0.f,
	          arxvr::VrImpactSource::HeldObject, 0x1234u);
	arxvr::VrImpactEvent impact;
	expect(system.consumeImpact(arxvr::VrHand::Right, impact),
	       "held-object swing should emit an impact event");
	expect(impact.type == arxvr::VrImpactType::HeldObject,
	       "held-object source should map to held-object impact type");
	expect(impact.sourceToken == 0x1234u && impact.weaponToken == 0x1234u,
	       "held object identity should be retained as the weapon token");
}

void equippedWeaponUsesCommonImpactPipeline() {
	arxvr::VrInteractionSystem system;
	feedSwing(system, arxvr::VrHand::Right, 0, 0.f,
	          arxvr::VrImpactSource::EquippedWeapon, 0xbeefu);
	expect(system.canImpact(arxvr::VrHand::Right),
	       "equipped weapon should qualify through the common hand gate");
	arxvr::VrImpactEvent impact;
	expect(system.consumeImpact(arxvr::VrHand::Right, impact),
	       "equipped weapon should emit the common semantic impact event");
	expect(impact.type == arxvr::VrImpactType::EquippedWeapon,
	       "equipped weapon source should retain its semantic impact type");
	expect(impact.sourceToken == 0xbeefu && impact.weaponToken == 0xbeefu,
	       "equipped weapon identity should populate the generic weapon token");
	expect(impact.hand == arxvr::VrHand::Right,
	       "equipped weapon event should retain physical-hand identity");
}

void trackingLossCannotEmitSemanticImpact() {
	arxvr::VrInteractionSystem system;
	const auto lost = system.updateHand(
		arxvr::VrHand::Right,
		sample(0.f, 0.f, 100000, arxvr::VrImpactSource::Fist, 0, true, false));
	expect(lost == arxvr::VrImpactGateStatus::Invalid,
	       "tracking loss should fail closed at the interaction-service boundary");
	arxvr::VrImpactEvent impact;
	expect(!system.consumeImpact(arxvr::VrHand::Right, impact),
	       "invalid tracking must never emit an impact event");
}

void invalidHandIdentityFailsClosed() {
	arxvr::VrInteractionSystem system;
	const auto status = system.updateHand(
		arxvr::VrHand::Unknown,
		sample(0.f, 0.f, 0, arxvr::VrImpactSource::Fist, 0, true));
	expect(status == arxvr::VrImpactGateStatus::Invalid,
	       "unknown hand identity should be rejected at the service boundary");
	expect(!system.canImpact(arxvr::VrHand::Unknown),
	       "unknown hand identity must never alias a physical hand state");
	arxvr::VrImpactEvent impact;
	expect(!system.consumeImpact(arxvr::VrHand::Unknown, impact),
	       "unknown hand identity must not emit a semantic impact");
	system.resetGesture(arxvr::VrHand::Unknown);
}

} // namespace

int main() {
	semanticFistEventCarriesHandAndKinematics();
	handsOwnIndependentQualificationState();
	heldObjectBecomesWeaponBearingSemanticEvent();
	equippedWeaponUsesCommonImpactPipeline();
	trackingLossCannotEmitSemanticImpact();
	invalidHandIdentityFailsClosed();

	if(g_failures != 0) {
		std::cerr << g_failures << " interaction-system test(s) failed\n";
		return 1;
	}
	std::cout << "VrInteractionSystem: semantic impact pipeline tests passed\n";
	return 0;
}
