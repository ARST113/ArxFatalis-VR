#include <cassert>

#include "vr/VrProjectileDefenseRuntime.h"

namespace {

using namespace arxvr;

VrShieldPose shieldPose() {
	VrShieldPose pose;
	pose.center = { 0.f, 0.f, 0.f };
	pose.normal = { 0.f, 0.f, 1.f };
	pose.up = { 0.f, 1.f, 0.f };
	pose.valid = true;
	return pose;
}

VrWeaponSegment weaponSegment() {
	VrWeaponSegment segment;
	segment.start = { 0.f, -20.f, 0.f };
	segment.end = { 0.f, 20.f, 0.f };
	segment.radius = 1.5f;
	segment.valid = true;
	return segment;
}

VrProjectileSample projectile(std::uint64_t token, std::uint64_t timestampUs,
                              VrImpactVector3 start = { 0.f, 0.f, 20.f },
                              VrImpactVector3 end = { 0.f, 0.f, -20.f },
                              VrImpactVector3 velocity = { 0.f, 0.f, -900.f }) {
	VrProjectileSample sample;
	sample.token = token;
	sample.start = start;
	sample.end = end;
	sample.velocity = velocity;
	sample.radius = 1.f;
	sample.timestampUs = timestampUs;
	return sample;
}

void publishShield(VrDefenseRuntime & runtime, std::uint64_t timestampUs) {
	runtime.publishShield(10, VrShieldProfile{}, shieldPose(), timestampUs);
}

void publishWeapon(VrDefenseRuntime & runtime, std::uint64_t timestampUs) {
	runtime.publishDefenderWeapon(20, weaponSegment(), timestampUs);
}

void testShieldGetsFirstRefusal() {
	VrDefenseRuntime runtime;
	VrProjectileDefenseSystem system;
	publishShield(runtime, 1000);
	publishWeapon(runtime, 1000);

	VrProjectileDeflection result;
	assert(vrEvaluateProjectileDefense(runtime, system, projectile(1, 1100), result));
	assert(result.type == VrProjectileDeflectionType::Shield);
}

void testWeaponHandlesProjectileWithoutFreshShield() {
	VrDefenseRuntime runtime;
	VrProjectileDefenseSystem system;
	publishShield(runtime, 1000);
	publishWeapon(runtime, 200000);

	VrProjectileDeflection result;
	const VrProjectileSample crossing = projectile(
		2, 200100, { -15.f, 0.f, 0.f }, { 15.f, 0.f, 0.f }, { 900.f, 0.f, 0.f });
	assert(vrEvaluateProjectileDefense(runtime, system, crossing, result));
	assert(result.type == VrProjectileDeflectionType::Weapon);
}

void testStalePublishedGeometryFailsClosed() {
	VrDefenseRuntime runtime;
	VrProjectileDefenseSystem system;
	publishShield(runtime, 1000);
	publishWeapon(runtime, 1000);

	VrProjectileDeflection result;
	assert(!vrEvaluateProjectileDefense(runtime, system, projectile(3, 200000), result));
	assert(result.type == VrProjectileDeflectionType::None);
}

void testInvalidProjectileFailsBeforeRouting() {
	VrDefenseRuntime runtime;
	VrProjectileDefenseSystem system;
	publishShield(runtime, 1000);

	VrProjectileSample invalid = projectile(0, 1100);
	VrProjectileDeflection result;
	assert(!vrEvaluateProjectileDefense(runtime, system, invalid, result));
	assert(result.type == VrProjectileDeflectionType::None);
}

void testClassifierLatchSurvivesRuntimeRouteChanges() {
	VrDefenseRuntime runtime;
	VrProjectileDefenseSystem system;
	publishShield(runtime, 1000);

	VrProjectileDeflection result;
	assert(vrEvaluateProjectileDefense(runtime, system, projectile(4, 1100), result));
	assert(result.type == VrProjectileDeflectionType::Shield);

	// Removing the shield and publishing a weapon must not let the same token
	// immediately generate a second defensive contact inside the shared contact
	// neighbourhood. The classifier, not the routing adapter, owns that latch.
	runtime.clearShield();
	publishWeapon(runtime, 1150);
	const VrProjectileSample immediate = projectile(
		4, 1200, { -5.f, 0.f, 0.f }, { 5.f, 0.f, 0.f }, { 900.f, 0.f, 0.f });
	assert(!vrEvaluateProjectileDefense(runtime, system, immediate, result));
	assert(result.type == VrProjectileDeflectionType::None);
}

void testRuntimeResetAndClassifierResetAreExplicit() {
	VrDefenseRuntime runtime;
	VrProjectileDefenseSystem system;
	publishShield(runtime, 1000);
	VrProjectileDeflection result;
	assert(vrEvaluateProjectileDefense(runtime, system, projectile(5, 1100), result));

	runtime.resetSession();
	system.resetSession();
	publishShield(runtime, 1200);
	assert(vrEvaluateProjectileDefense(runtime, system, projectile(5, 1300), result));
	assert(result.type == VrProjectileDeflectionType::Shield);
}

} // namespace

int main() {
	testShieldGetsFirstRefusal();
	testWeaponHandlesProjectileWithoutFreshShield();
	testStalePublishedGeometryFailsClosed();
	testInvalidProjectileFailsBeforeRouting();
	testClassifierLatchSurvivesRuntimeRouteChanges();
	testRuntimeResetAndClassifierResetAreExplicit();
	return 0;
}
