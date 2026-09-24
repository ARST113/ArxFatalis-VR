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

VrShieldPose frontShield() {
	VrShieldPose pose;
	pose.center = { 0.f, 0.f, 0.f };
	pose.normal = { 0.f, 0.f, 1.f };
	pose.up = { 0.f, 1.f, 0.f };
	pose.valid = true;
	return pose;
}

VrIncomingContact makeIncoming(VrDefenseRuntime & runtime,
                               std::uint64_t source,
                               std::uint64_t action,
                               std::uint64_t firstTime,
                               std::uint64_t secondTime) {
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(source, action, { 0.f, 0.f, 20.f },
	                                    firstTime, contact));
	assert(runtime.sampleIncomingWeapon(source, action, { 0.f, 0.f, -10.f },
	                                   secondTime, contact));
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

void testStaleShieldRejectsOtherwiseValidContact() {
	VrDefenseRuntime runtime;
	runtime.publishShield(21, VrShieldProfile{}, frontShield(), 500000);
	VrIncomingContact contact = makeIncoming(runtime, 31, 41, 1000000, 1010000);
	VrDefenseEvent event;
	assert(!runtime.evaluateShieldBlock(contact, event));
}

void testConveniencePathClearsRejectedEvent() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 3000000;
	const std::uint64_t secondTime = 3010000;
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

void testResetClearsShieldAndMotionHistory() {
	VrDefenseRuntime runtime;
	VrIncomingContact contact;
	runtime.publishShield(7, VrShieldProfile{}, frontShield(), 1000000);
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 20.f }, 1000000, contact));
	runtime.resetSession();
	assert(!runtime.shield().active);
	assert(!runtime.sampleIncomingWeapon(1, 2, { 0.f, 0.f, 0.f }, 1010000, contact));
}

} // namespace

int main() {
	testPublishedShieldFreshness();
	testInvalidShieldFailsClosed();
	testIncomingWeaponBuildsRealVelocityAndBlocks();
	testSampleShieldBlockConveniencePath();
	testStaleShieldRejectsOtherwiseValidContact();
	testConveniencePathClearsRejectedEvent();
	testTimestampRegressionCreatesFreshBaseline();
	testLargeGapDoesNotFabricateSweep();
	testTeleportSpeedDoesNotFabricateSweep();
	testDuplicatePositionPreservesMeaningfulBaseline();
	testSourceAndActionHistoriesAreIndependent();
	testResetClearsShieldAndMotionHistory();
	return 0;
}
