#include <cassert>
#include <cmath>
#include <cstdint>

#include "vr/VrDefenseRuntime.h"

namespace {

using arxvr::VrDefenseEvent;
using arxvr::VrDefenseRuntime;
using arxvr::VrDefenseRuntimeConfig;
using arxvr::VrImpactVector3;
using arxvr::VrIncomingContact;
using arxvr::VrShieldPose;
using arxvr::VrShieldProfile;
using arxvr::VrWeaponSegment;

VrShieldPose frontShield() {
	VrShieldPose pose;
	pose.center = { 0.f, 0.f, 0.f };
	pose.normal = { 0.f, 0.f, 1.f };
	pose.up = { 0.f, 1.f, 0.f };
	pose.valid = true;
	return pose;
}

VrWeaponSegment horizontalDefender(float y = 0.f) {
	VrWeaponSegment segment;
	segment.start = { -20.f, y, 0.f };
	segment.end = { 20.f, y, 0.f };
	segment.radius = 2.f;
	segment.valid = true;
	return segment;
}

VrIncomingContact makeIncoming(VrDefenseRuntime & runtime,
                               std::uint64_t source,
                               std::uint64_t action,
                               std::uint64_t firstTime,
                               std::uint64_t secondTime,
                               VrImpactVector3 first = { 0.f, 0.f, 20.f },
                               VrImpactVector3 second = { 0.f, 0.f, -10.f }) {
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(source, action, first, firstTime, contact));
	assert(runtime.sampleIncomingWeapon(source, action, second, secondTime, contact));
	return contact;
}

void testPublishedShieldFreshness() {
	VrDefenseRuntime runtime;
	const std::uint64_t publishedAt = 1000000;
	runtime.publishShield(11, VrShieldProfile{}, frontShield(), publishedAt);
	assert(runtime.shield().active);
	assert(runtime.shield().token == 11);
	assert(runtime.shieldActiveAt(publishedAt));
	assert(runtime.shieldActiveAt(publishedAt + 120000));
	assert(!runtime.shieldActiveAt(publishedAt + 120001));
	assert(!runtime.shieldActiveAt(publishedAt - 1));

	runtime.clearShield();
	assert(!runtime.shield().active);
	assert(!runtime.shieldActiveAt(publishedAt + 1));
}

void testInvalidShieldFailsClosed() {
	VrDefenseRuntime runtime;
	VrShieldPose invalid = frontShield();
	invalid.normal.x = std::nanf("");
	runtime.publishShield(12, VrShieldProfile{}, invalid, 1000000);
	assert(!runtime.shield().active);

	runtime.publishShield(0, VrShieldProfile{}, frontShield(), 1000001);
	assert(!runtime.shield().active);
}

void testPublishedDefenderWeaponFreshnessAndVelocity() {
	VrDefenseRuntime runtime;
	VrWeaponSegment first = horizontalDefender();
	VrWeaponSegment second = first;
	second.start.x += 5.f;
	second.end.x += 5.f;

	runtime.publishDefenderWeapon(101, first, 1000000);
	assert(runtime.defenderWeapon().active);
	assert(runtime.defenderWeapon().token == 101);
	assert(runtime.defenderWeaponActiveAt(1000000));
	assert(runtime.defenderWeapon().linearVelocity.x == 0.f);

	runtime.publishDefenderWeapon(101, second, 1010000);
	assert(runtime.defenderWeapon().active);
	assert(std::abs(runtime.defenderWeapon().linearVelocity.x - 500.f) < 0.01f);
	assert(runtime.defenderWeaponActiveAt(1130000));
	assert(!runtime.defenderWeaponActiveAt(1130001));
}

void testDefenderWeaponTeleportFailsClosed() {
	VrDefenseRuntimeConfig config;
	config.maxDefenderWeaponSpeed = 1000.f;
	VrDefenseRuntime runtime(config);
	VrWeaponSegment first = horizontalDefender();
	VrWeaponSegment teleported = first;
	teleported.start.x += 100.f;
	teleported.end.x += 100.f;

	runtime.publishDefenderWeapon(102, first, 1000000);
	runtime.publishDefenderWeapon(102, teleported, 1010000);
	assert(!runtime.defenderWeapon().active);

	// The recovered frame establishes a fresh zero-velocity baseline.
	runtime.publishDefenderWeapon(102, teleported, 1020000);
	assert(runtime.defenderWeapon().active);
	assert(runtime.defenderWeapon().linearVelocity.x == 0.f);
}

void testIncomingWeaponBuildsRealVelocityAndBlocks() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 1000000;
	const std::uint64_t secondTime = 1010000;
	runtime.publishShield(21, VrShieldProfile{}, frontShield(), secondTime);
	VrIncomingContact contact = makeIncoming(runtime, 31, 41, firstTime, secondTime);
	assert(std::abs(contact.linearVelocity.z + 3000.f) < 0.01f);
	assert(contact.start.z == 20.f);
	assert(contact.end.z == -10.f);

	VrDefenseEvent event;
	assert(runtime.evaluateShieldBlock(contact, event));
	assert(event.type == arxvr::VrDefenseEventType::ShieldBlock);
	assert(event.relativeSpeed > 2999.f && event.relativeSpeed < 3001.f);
}

void testSampleShieldBlockConveniencePath() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 2000000;
	const std::uint64_t secondTime = 2010000;
	runtime.publishShield(51, VrShieldProfile{}, frontShield(), secondTime);

	VrDefenseEvent event;
	assert(!runtime.sampleShieldBlock(61, 71, { 0.f, 0.f, 20.f }, firstTime, event));
	assert(event.type == arxvr::VrDefenseEventType::None);
	assert(runtime.sampleShieldBlock(61, 71, { 0.f, 0.f, -10.f }, secondTime, event));
	assert(event.type == arxvr::VrDefenseEventType::ShieldBlock);
	assert(event.relativeSpeed > 2999.f && event.relativeSpeed < 3001.f);
}

void testParryUsesPublishedPlayerWeapon() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 3000000;
	const std::uint64_t secondTime = 3010000;
	runtime.publishDefenderWeapon(201, horizontalDefender(), secondTime);
	VrIncomingContact contact = makeIncoming(runtime, 211, 221, firstTime, secondTime,
	                                         { 0.f, 0.f, 20.f }, { 0.f, 0.f, -20.f });

	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(211, 231, contact, 2.f, event));
	assert(event.type == arxvr::VrDefenseEventType::WeaponParry);
	assert(event.relativeSpeed > 3999.f && event.relativeSpeed < 4001.f);
	assert(runtime.defenseLatched(211, 231, secondTime));
	assert(runtime.latchedDefenseType() == arxvr::VrDefenseEventType::WeaponParry);
}

void testParryTakesPrecedenceWhenShieldAlsoIntersects() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 4000000;
	const std::uint64_t secondTime = 4010000;
	runtime.publishShield(301, VrShieldProfile{}, frontShield(), secondTime);
	runtime.publishDefenderWeapon(302, horizontalDefender(), secondTime);
	VrIncomingContact contact = makeIncoming(runtime, 311, 321, firstTime, secondTime,
	                                         { 0.f, 0.f, 20.f }, { 0.f, 0.f, -20.f });

	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(311, 331, contact, 2.f, event));
	assert(event.type == arxvr::VrDefenseEventType::WeaponParry);
}

void testShieldFallbackWhenWeaponDoesNotIntersect() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 5000000;
	const std::uint64_t secondTime = 5010000;
	runtime.publishShield(401, VrShieldProfile{}, frontShield(), secondTime);
	runtime.publishDefenderWeapon(402, horizontalDefender(100.f), secondTime);
	VrIncomingContact contact = makeIncoming(runtime, 411, 421, firstTime, secondTime);

	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(411, 431, contact, 2.f, event));
	assert(event.type == arxvr::VrDefenseEventType::ShieldBlock);
}

void testSamplingNearMissDoesNotConsumeDefenseCooldown() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 6000000;
	const std::uint64_t secondTime = 6010000;
	runtime.publishShield(501, VrShieldProfile{}, frontShield(), secondTime);

	// Build a physically valid sweep but deliberately do not evaluate it as a
	// player hit. This models an NPC weapon moving near the shield while Arx's
	// sphere query did not actually select the player.
	VrIncomingContact ignored = makeIncoming(runtime, 511, 521, firstTime, secondTime);
	(void)ignored;

	VrIncomingContact actual = makeIncoming(runtime, 512, 522,
	                                        secondTime + 1000, secondTime + 11000);
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(512, 532, actual, 2.f, event));
	assert(event.type == arxvr::VrDefenseEventType::ShieldBlock);
}

void testDefenseLatchSuppressesWholeWeaponStrike() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 7000000;
	const std::uint64_t secondTime = 7010000;
	runtime.publishDefenderWeapon(601, horizontalDefender(), secondTime);
	VrIncomingContact contact = makeIncoming(runtime, 611, 621, firstTime, secondTime,
	                                         { 0.f, 0.f, 20.f }, { 0.f, 0.f, -20.f });
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(611, 631, contact, 2.f, event));
	assert(runtime.defenseLatched(611, 631, secondTime + 299999));
	assert(!runtime.defenseLatched(611, 631, secondTime + 300001));
	assert(!runtime.defenseLatched(611, 632, secondTime + 1000));
	assert(!runtime.defenseLatched(612, 631, secondTime + 1000));
}

void testStaleDefenderWeaponCannotParry() {
	VrDefenseRuntime runtime;
	runtime.publishDefenderWeapon(701, horizontalDefender(), 8000000);
	VrIncomingContact contact = makeIncoming(runtime, 711, 721, 8120001, 8130001,
	                                         { 0.f, 0.f, 20.f }, { 0.f, 0.f, -20.f });
	VrDefenseEvent event;
	assert(!runtime.evaluatePlayerDefense(711, 731, contact, 2.f, event));
	assert(event.type == arxvr::VrDefenseEventType::None);
}

void testStaleShieldRejectsOtherwiseValidContact() {
	VrDefenseRuntime runtime;
	runtime.publishShield(21, VrShieldProfile{}, frontShield(), 500000);
	VrIncomingContact contact = makeIncoming(runtime, 31, 41, 1000000, 1010000);
	VrDefenseEvent event;
	assert(!runtime.evaluateShieldBlock(contact, event));
}

void testConveniencePathClearsRejectedEvent() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 9000000;
	const std::uint64_t secondTime = 9010000;
	runtime.publishShield(81, VrShieldProfile{}, frontShield(), secondTime);

	VrDefenseEvent event;
	assert(!runtime.sampleShieldBlock(91, 101, { 0.f, 0.f, 20.f }, firstTime, event));
	assert(runtime.sampleShieldBlock(91, 101, { 0.f, 0.f, -10.f }, secondTime, event));
	assert(event.type == arxvr::VrDefenseEventType::ShieldBlock);

	// Clearing the shield must fail closed and erase the prior accepted event,
	// so gameplay cannot accidentally reuse an old block result.
	runtime.clearShield();
	assert(!runtime.sampleShieldBlock(92, 102, { 0.f, 0.f, 20.f }, secondTime + 10000, event));
	assert(event.type == arxvr::VrDefenseEventType::None);
}

void testTimestampRegressionCreatesFreshBaseline() {
	VrDefenseRuntime runtime;
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 20.f }, 1000, contact));
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 10.f }, 900, contact));
	// The regressed sample becomes the new baseline. Advance far enough that the
	// 10-unit recovery motion stays below the production teleport ceiling.
	assert(runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 0.f }, 10900, contact));
	assert(contact.start.z == 10.f);
	assert(contact.end.z == 0.f);
}

void testLargeGapDoesNotFabricateSweep() {
	VrDefenseRuntime runtime;
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 20.f }, 1000, contact));
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, -20.f }, 300000, contact));
	assert(runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, -21.f }, 301000, contact));
	assert(contact.start.z == -20.f);
}

void testTeleportSpeedDoesNotFabricateSweep() {
	VrDefenseRuntimeConfig config;
	config.maxIncomingSpeed = 1000.f;
	VrDefenseRuntime runtime(config);
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 0.f }, 1000000, contact));
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 100.f }, 1010000, contact));
	assert(runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 100.5f }, 1011000, contact));
	assert(contact.start.z == 100.f);
}

void testDuplicatePositionPreservesMeaningfulBaseline() {
	VrDefenseRuntime runtime;
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 10.f }, 1000000, contact));
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 10.f }, 1005000, contact));
	assert(runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 0.f }, 1010000, contact));
	assert(std::abs(contact.linearVelocity.z + 1000.f) < 0.01f);
}

void testSourceAndActionHistoriesAreIndependent() {
	VrDefenseRuntime runtime;
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(1, 10, { 0.f, 0.f, 20.f }, 1000000, contact));
	assert(!runtime.sampleIncomingWeapon(2, 10, { 0.f, 0.f, 30.f }, 1000000, contact));
	assert(!runtime.sampleIncomingWeapon(1, 20, { 0.f, 0.f, 40.f }, 1000000, contact));

	assert(runtime.sampleIncomingWeapon(1, 10, { 0.f, 0.f, 10.f }, 1010000, contact));
	assert(contact.start.z == 20.f);
	assert(runtime.sampleIncomingWeapon(2, 10, { 0.f, 0.f, 20.f }, 1010000, contact));
	assert(contact.start.z == 30.f);
	assert(runtime.sampleIncomingWeapon(1, 20, { 0.f, 0.f, 30.f }, 1010000, contact));
	assert(contact.start.z == 40.f);
}

void testResetClearsAllPublishedDefenseState() {
	VrDefenseRuntime runtime;
	VrIncomingContact contact;
	runtime.publishShield(7, VrShieldProfile{}, frontShield(), 1000000);
	runtime.publishDefenderWeapon(8, horizontalDefender(), 1000000);
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 20.f }, 1000000, contact));

	VrIncomingContact parryContact = makeIncoming(runtime, 3, 4, 1010000, 1020000,
	                                             { 0.f, 0.f, 20.f }, { 0.f, 0.f, -20.f });
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(3, 5, parryContact, 2.f, event));
	assert(runtime.defenseLatched(3, 5, 1020000));

	runtime.resetSession();
	assert(!runtime.shield().active);
	assert(!runtime.defenderWeapon().active);
	assert(!runtime.defenseLatched(3, 5, 1020001));
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 0.f }, 1030000, contact));
}

} // namespace

int main() {
	testPublishedShieldFreshness();
	testInvalidShieldFailsClosed();
	testPublishedDefenderWeaponFreshnessAndVelocity();
	testDefenderWeaponTeleportFailsClosed();
	testIncomingWeaponBuildsRealVelocityAndBlocks();
	testSampleShieldBlockConveniencePath();
	testParryUsesPublishedPlayerWeapon();
	testParryTakesPrecedenceWhenShieldAlsoIntersects();
	testShieldFallbackWhenWeaponDoesNotIntersect();
	testSamplingNearMissDoesNotConsumeDefenseCooldown();
	testDefenseLatchSuppressesWholeWeaponStrike();
	testStaleDefenderWeaponCannotParry();
	testStaleShieldRejectsOtherwiseValidContact();
	testConveniencePathClearsRejectedEvent();
	testTimestampRegressionCreatesFreshBaseline();
	testLargeGapDoesNotFabricateSweep();
	testTeleportSpeedDoesNotFabricateSweep();
	testDuplicatePositionPreservesMeaningfulBaseline();
	testSourceAndActionHistoriesAreIndependent();
	testResetClearsAllPublishedDefenseState();
	return 0;
}
