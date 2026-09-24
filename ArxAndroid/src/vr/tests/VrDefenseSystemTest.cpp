#include "vr/VrDefenseSystem.h"

#include <cmath>
#include <cstdint>
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

bool near(float first, float second, float epsilon = 0.001f) {
	return std::abs(first - second) <= epsilon;
}

arxvr::VrShieldPose defaultShield() {
	arxvr::VrShieldPose pose;
	pose.center = { 0.f, 0.f, 0.f };
	pose.normal = { 0.f, 0.f, 1.f };
	pose.up = { 0.f, 1.f, 0.f };
	pose.valid = true;
	return pose;
}

arxvr::VrIncomingContact frontalStrike(std::uint64_t timestampUs) {
	arxvr::VrIncomingContact incoming;
	incoming.start = { 0.f, 0.f, 20.f };
	incoming.end = { 0.f, 0.f, -20.f };
	incoming.linearVelocity = { 0.f, 0.f, -180.f };
	incoming.timestampUs = timestampUs;
	return incoming;
}

arxvr::VrWeaponSegment segment(arxvr::VrImpactVector3 start,
                               arxvr::VrImpactVector3 end,
                               float radius = 1.f) {
	arxvr::VrWeaponSegment result;
	result.start = start;
	result.end = end;
	result.radius = radius;
	result.valid = true;
	return result;
}

arxvr::VrParrySample crossingParry(std::uint64_t timestampUs) {
	arxvr::VrParrySample sample;
	sample.defender = segment({ -12.f, 0.f, 0.f }, { 12.f, 0.f, 0.f });
	sample.attacker = segment({ 0.f, -12.f, 0.f }, { 0.f, 12.f, 0.f });
	sample.defenderVelocity = { 0.f, 0.f, 0.f };
	sample.attackerVelocity = { 0.f, -120.f, 0.f };
	sample.timestampUs = timestampUs;
	return sample;
}

void frontalShieldContactBlocks() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	const arxvr::VrShieldProfile profile;
	const bool blocked = system.evaluateShieldBlock(
		profile, defaultShield(), frontalStrike(100000), event);
	expect(blocked, "front-facing strike crossing the shield volume should block");
	expect(event.type == arxvr::VrDefenseEventType::ShieldBlock,
	       "shield block should publish the correct event type");
	expect(event.relativeSpeed > 170.f,
	       "shield block should preserve incoming physical speed");
	expect(event.position.z <= profile.halfThickness + 0.001f
	       && event.position.z >= -profile.halfThickness - 0.001f,
	       "shield contact point should lie inside the physical shield slab");
	expect(near(event.normal.z, 1.f),
	       "shield block should publish the normalized outward shield normal");
}

void rearAndTangentialContactsDoNotBlock() {
	arxvr::VrDefenseSystem rearSystem;
	arxvr::VrDefenseEvent rearEvent;
	arxvr::VrIncomingContact rear;
	rear.start = { 0.f, 0.f, -20.f };
	rear.end = { 0.f, 0.f, 20.f };
	rear.linearVelocity = { 0.f, 0.f, 180.f };
	rear.timestampUs = 100000;
	expect(!rearSystem.evaluateShieldBlock({}, defaultShield(), rear, rearEvent),
	       "contact entering from behind the shield must not block");

	arxvr::VrDefenseSystem tangentSystem;
	arxvr::VrDefenseEvent tangentEvent;
	arxvr::VrIncomingContact tangent;
	tangent.start = { -40.f, 0.f, 0.f };
	tangent.end = { 40.f, 0.f, 0.f };
	tangent.linearVelocity = { 180.f, 0.f, 0.f };
	tangent.timestampUs = 100000;
	expect(!tangentSystem.evaluateShieldBlock({}, defaultShield(), tangent, tangentEvent),
	       "near-tangential motion through shield thickness must not count as a block");
}

void shieldExtentsAndValidityAreEnforced() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	arxvr::VrIncomingContact outside = frontalStrike(100000);
	outside.start.x = 60.f;
	outside.end.x = 60.f;
	expect(!system.evaluateShieldBlock({}, defaultShield(), outside, event),
	       "contact outside finite shield width must not block");

	arxvr::VrShieldPose invalid = defaultShield();
	invalid.normal.x = std::numeric_limits<float>::quiet_NaN();
	expect(!system.evaluateShieldBlock({}, invalid, frontalStrike(200000), event),
	       "non-finite shield orientation must fail closed");
}

void shieldBlockCooldownDebouncesPersistentContact() {
	arxvr::VrDefenseSystem system;
	arxvr::VrShieldProfile profile;
	profile.blockCooldownUs = 140000;
	arxvr::VrDefenseEvent event;
	expect(system.evaluateShieldBlock(profile, defaultShield(), frontalStrike(100000), event),
	       "first shield block should be accepted");
	expect(!system.evaluateShieldBlock(profile, defaultShield(), frontalStrike(180000), event),
	       "persistent shield contact inside cooldown should be suppressed");
	expect(system.evaluateShieldBlock(profile, defaultShield(), frontalStrike(240000), event),
	       "new shield contact should be accepted after cooldown");
}

void timestampRegressionFailsClosed() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	expect(system.evaluateShieldBlock({}, defaultShield(), frontalStrike(200000), event),
	       "control block should be accepted before timestamp regression");
	expect(!system.evaluateShieldBlock({}, defaultShield(), frontalStrike(150000), event),
	       "shield timestamp regression must fail closed");
}

void crossingWeaponsWithRelativeMotionParry() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	const bool parried = system.evaluateParry(crossingParry(100000), event);
	expect(parried, "crossing weapon segments with sufficient relative speed should parry");
	expect(event.type == arxvr::VrDefenseEventType::WeaponParry,
	       "weapon parry should publish the correct event type");
	expect(event.relativeSpeed >= 120.f,
	       "parry should expose relative weapon speed");
	expect(event.contactDistance <= 0.001f,
	       "crossing weapon segments should resolve near-zero contact distance");
	expect(event.angleSin > 0.9f,
	       "relative motion crossing the defender blade should pass angle gating");
}

void slowOrLongitudinalWeaponMotionDoesNotParry() {
	arxvr::VrDefenseSystem slowSystem;
	arxvr::VrDefenseEvent event;
	arxvr::VrParrySample slow = crossingParry(100000);
	slow.attackerVelocity = { 0.f, -20.f, 0.f };
	expect(!slowSystem.evaluateParry(slow, event),
	       "slow weapon overlap should not generate a parry");

	arxvr::VrDefenseSystem longitudinalSystem;
	arxvr::VrParrySample longitudinal = crossingParry(100000);
	longitudinal.attackerVelocity = { -120.f, 0.f, 0.f };
	expect(!longitudinalSystem.evaluateParry(longitudinal, event),
	       "relative motion along the defender blade should not generate a parry");
}

void collinearAndSeparatedWeaponsDoNotParry() {
	arxvr::VrDefenseSystem collinearSystem;
	arxvr::VrDefenseEvent event;
	arxvr::VrParrySample collinear;
	collinear.defender = segment({ -12.f, 0.f, 0.f }, { 12.f, 0.f, 0.f });
	collinear.attacker = segment({ -10.f, 0.5f, 0.f }, { 10.f, 0.5f, 0.f });
	collinear.defenderVelocity = { 0.f, 100.f, 0.f };
	collinear.attackerVelocity = { 0.f, -100.f, 0.f };
	collinear.timestampUs = 100000;
	expect(!collinearSystem.evaluateParry(collinear, event),
	       "nearly collinear overlapping weapon segments should not latch as a parry");

	arxvr::VrDefenseSystem separatedSystem;
	arxvr::VrParrySample separated = crossingParry(100000);
	separated.attacker.start.z = 20.f;
	separated.attacker.end.z = 20.f;
	expect(!separatedSystem.evaluateParry(separated, event),
	       "weapon segments outside contact radii must not parry");
}

void parryCooldownDebouncesContinuousIntersection() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	expect(system.evaluateParry(crossingParry(100000), event),
	       "first parry should qualify");
	expect(!system.evaluateParry(crossingParry(180000), event),
	       "continuous weapon intersection inside parry cooldown should be suppressed");
	expect(system.evaluateParry(crossingParry(260000), event),
	       "new parry should qualify after cooldown");
}

void malformedParryInputFailsClosed() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	arxvr::VrParrySample invalid = crossingParry(100000);
	invalid.attacker.valid = false;
	expect(!system.evaluateParry(invalid, event),
	       "invalid contact segment must fail closed");

	invalid = crossingParry(200000);
	invalid.attackerVelocity.y = std::numeric_limits<float>::infinity();
	expect(!system.evaluateParry(invalid, event),
	       "non-finite weapon velocity must fail closed");
}

void resetClearsDefenseDebounce() {
	arxvr::VrDefenseSystem system;
	arxvr::VrDefenseEvent event;
	expect(system.evaluateShieldBlock({}, defaultShield(), frontalStrike(100000), event),
	       "control shield block should qualify before reset");
	expect(system.evaluateParry(crossingParry(100000), event),
	       "control parry should qualify before reset");
	system.resetSession();
	expect(system.evaluateShieldBlock({}, defaultShield(), frontalStrike(100000), event),
	       "session reset should clear shield debounce state");
	expect(system.evaluateParry(crossingParry(100000), event),
	       "session reset should clear parry debounce state");
}

} // namespace

int main() {
	frontalShieldContactBlocks();
	rearAndTangentialContactsDoNotBlock();
	shieldExtentsAndValidityAreEnforced();
	shieldBlockCooldownDebouncesPersistentContact();
	timestampRegressionFailsClosed();
	crossingWeaponsWithRelativeMotionParry();
	slowOrLongitudinalWeaponMotionDoesNotParry();
	collinearAndSeparatedWeaponsDoNotParry();
	parryCooldownDebouncesContinuousIntersection();
	malformedParryInputFailsClosed();
	resetClearsDefenseDebounce();

	if(g_failures != 0) {
		std::cerr << g_failures << " VR defense test(s) failed\n";
		return 1;
	}
	std::cout << "VrDefenseSystem: shield block/parry tests passed\n";
	return 0;
}
