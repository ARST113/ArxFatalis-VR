#include <cassert>
#include <cmath>
#include <cstdint>

#include "vr/VrDefenseRuntime.h"

namespace {

using arxvr::VrDefenseEvent;
using arxvr::VrDefenseEventType;
using arxvr::VrDefenseRuntime;
using arxvr::VrImpactVector3;
using arxvr::VrIncomingContact;
using arxvr::VrShieldPose;
using arxvr::VrShieldProfile;
using arxvr::VrWeaponSegment;

VrShieldPose makeShieldPose() {
	VrShieldPose pose;
	pose.center = { 0.f, 0.f, 0.f };
	pose.normal = { 0.f, 0.f, 1.f };
	pose.up = { 0.f, 1.f, 0.f };
	pose.valid = true;
	return pose;
}

VrWeaponSegment makeDefenderWeapon(float y = 0.f) {
	VrWeaponSegment segment;
	segment.start = { -20.f, y, 0.f };
	segment.end = { 20.f, y, 0.f };
	segment.radius = 2.f;
	segment.valid = true;
	return segment;
}

VrIncomingContact makeIncomingSweep(VrDefenseRuntime & runtime,
                                    std::uint64_t sourceToken,
                                    std::uint64_t actionToken,
                                    std::uint64_t firstTime,
                                    std::uint64_t secondTime,
                                    VrImpactVector3 first = { 0.f, 0.f, 20.f },
                                    VrImpactVector3 second = { 0.f, 0.f, -20.f }) {
	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(sourceToken, actionToken,
	                                     first, firstTime, contact));
	assert(runtime.sampleIncomingWeapon(sourceToken, actionToken,
	                                    second, secondTime, contact));
	return contact;
}

void testNearMissSamplingDoesNotArmDefense() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 1000000;
	const std::uint64_t secondTime = 1010000;
	runtime.publishShield(100, VrShieldProfile{}, makeShieldPose(), secondTime);

	// This mirrors the live Equipment.cpp contract: NPC weapon motion is sampled
	// before the sphere query is inspected, but defense is evaluated only when
	// Arx actually selects the player as a target.
	const VrIncomingContact nearMiss = makeIncomingSweep(
		runtime, 200, 300, firstTime, secondTime);
	(void)nearMiss;
	assert(!runtime.defenseLatched(200, 400, secondTime));

	const VrIncomingContact playerHit = makeIncomingSweep(
		runtime, 201, 301, secondTime + 1000, secondTime + 11000);
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(201, 401, playerHit, 2.f, event));
	assert(event.type == VrDefenseEventType::ShieldBlock);
	assert(runtime.defenseLatched(201, 401, secondTime + 12000));
}

void testParryTakesPriorityOverShieldInLiveRoute() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 2000000;
	const std::uint64_t secondTime = 2010000;
	runtime.publishShield(500, VrShieldProfile{}, makeShieldPose(), secondTime);
	runtime.publishDefenderWeapon(600, makeDefenderWeapon(), secondTime);

	const VrIncomingContact playerHit = makeIncomingSweep(
		runtime, 700, 800, firstTime, secondTime);
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(700, 900, playerHit, 2.f, event));
	assert(event.type == VrDefenseEventType::WeaponParry);
	assert(event.relativeSpeed > 3999.f && event.relativeSpeed < 4001.f);
	assert(runtime.defenseLatched(700, 900, secondTime + 1));
}

void testStaleWeaponFallsBackToFreshShield() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 3000000;
	const std::uint64_t secondTime = 3010000;
	runtime.publishDefenderWeapon(1000, makeDefenderWeapon(), secondTime - 130000);
	runtime.publishShield(1001, VrShieldProfile{}, makeShieldPose(), secondTime);

	const VrIncomingContact playerHit = makeIncomingSweep(
		runtime, 1100, 1200, firstTime, secondTime);
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(1100, 1300, playerHit, 2.f, event));
	assert(event.type == VrDefenseEventType::ShieldBlock);
}

void testStrikeLatchOnlySuppressesMatchingStrike() {
	VrDefenseRuntime runtime;
	const std::uint64_t firstTime = 4000000;
	const std::uint64_t secondTime = 4010000;
	runtime.publishShield(1400, VrShieldProfile{}, makeShieldPose(), secondTime);

	const VrIncomingContact playerHit = makeIncomingSweep(
		runtime, 1500, 1600, firstTime, secondTime);
	VrDefenseEvent event;
	assert(runtime.evaluatePlayerDefense(1500, 1700, playerHit, 2.f, event));
	assert(runtime.defenseLatched(1500, 1700, secondTime + 1000));

	// Repeated action points for the same weapon strike are suppressed, while a
	// different weapon/source token is not accidentally swallowed.
	assert(!runtime.defenseLatched(1500, 1701, secondTime + 1000));
	assert(!runtime.defenseLatched(1501, 1700, secondTime + 1000));
	assert(!runtime.defenseLatched(1500, 1700, secondTime + 300001));
}

void testTrackingDiscontinuityCannotFabricateDefense() {
	arxvr::VrDefenseRuntimeConfig config;
	config.maxIncomingSpeed = 1000.f;
	VrDefenseRuntime runtime(config);
	const std::uint64_t firstTime = 5000000;
	const std::uint64_t secondTime = 5010000;
	runtime.publishShield(1800, VrShieldProfile{}, makeShieldPose(), secondTime);

	VrIncomingContact contact;
	assert(!runtime.sampleIncomingWeapon(1900, 2000,
	                                     { 0.f, 0.f, 100.f }, firstTime, contact));
	assert(!runtime.sampleIncomingWeapon(1900, 2000,
	                                     { 0.f, 0.f, -100.f }, secondTime, contact));
	VrDefenseEvent event;
	assert(!runtime.evaluatePlayerDefense(1900, 2100, contact, 2.f, event));
	assert(event.type == VrDefenseEventType::None);
	assert(!runtime.defenseLatched(1900, 2100, secondTime));
}

} // namespace

int main() {
	testNearMissSamplingDoesNotArmDefense();
	testParryTakesPriorityOverShieldInLiveRoute();
	testStaleWeaponFallsBackToFreshShield();
	testStrikeLatchOnlySuppressesMatchingStrike();
	testTrackingDiscontinuityCannotFabricateDefense();
	return 0;
}
