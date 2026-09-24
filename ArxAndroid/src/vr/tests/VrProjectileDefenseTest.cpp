#include <cassert>
#include <cmath>
#include <limits>

#include "vr/VrProjectileDefense.h"

namespace {

using namespace arxvr;

bool near(float lhs, float rhs, float epsilon = 0.01f) {
	return std::abs(lhs - rhs) <= epsilon;
}

VrProjectileSample projectile(std::uint64_t token,
                              VrImpactVector3 start,
                              VrImpactVector3 end,
                              VrImpactVector3 velocity,
                              std::uint64_t timestampUs) {
	VrProjectileSample sample;
	sample.token = token;
	sample.start = start;
	sample.end = end;
	sample.velocity = velocity;
	sample.radius = 1.f;
	sample.timestampUs = timestampUs;
	return sample;
}

VrShieldPose frontShield() {
	VrShieldPose pose;
	pose.center = { 0.f, 0.f, 0.f };
	pose.normal = { 0.f, 0.f, 1.f };
	pose.up = { 0.f, 1.f, 0.f };
	pose.valid = true;
	return pose;
}

VrWeaponSegment verticalWeapon() {
	VrWeaponSegment segment;
	segment.start = { 0.f, -20.f, 0.f };
	segment.end = { 0.f, 20.f, 0.f };
	segment.radius = 1.5f;
	segment.valid = true;
	return segment;
}

void testShieldReflectsFrontalProjectile() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	const bool hit = system.evaluateShield(
		projectile(1, { 0.f, 0.f, 20.f }, { 0.f, 0.f, -20.f },
		           { 0.f, 0.f, -1000.f }, 1000),
		VrShieldProfile{}, frontShield(), result);
	assert(hit);
	assert(result.type == VrProjectileDeflectionType::Shield);
	assert(result.outgoingVelocity.z > 0.f);
	assert(near(result.incomingSpeed, 1000.f));
	assert(near(result.outgoingSpeed, 750.f));
	assert(result.position.z > 0.f);
	assert(result.timestampUs == 1000);
}

void testShieldRejectsRearAndFiniteMisses() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	assert(!system.evaluateShield(
		projectile(2, { 0.f, 0.f, -20.f }, { 0.f, 0.f, 20.f },
		           { 0.f, 0.f, 1000.f }, 2000),
		VrShieldProfile{}, frontShield(), result));
	assert(!system.evaluateShield(
		projectile(3, { 80.f, 0.f, 20.f }, { 80.f, 0.f, -20.f },
		           { 0.f, 0.f, -1000.f }, 2100),
		VrShieldProfile{}, frontShield(), result));
}

void testProjectileRadiusExpandsShieldContact() {
	VrProjectileDefenseSystem system;
	VrProjectileSample sample = projectile(
		4, { 24.5f, 0.f, 20.f }, { 24.5f, 0.f, -20.f },
		{ 0.f, 0.f, -500.f }, 3000);
	sample.radius = 1.f;
	VrProjectileDeflection result;
	assert(system.evaluateShield(sample, VrShieldProfile{}, frontShield(), result));
}

void testWeaponReflectsCrossingProjectile() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	const bool hit = system.evaluateWeapon(
		projectile(5, { -15.f, 0.f, 0.f }, { 15.f, 0.f, 0.f },
		           { 900.f, 0.f, 0.f }, 4000),
		verticalWeapon(), result);
	assert(hit);
	assert(result.type == VrProjectileDeflectionType::Weapon);
	assert(result.outgoingVelocity.x < 0.f);
	assert(near(result.incomingSpeed, 900.f));
	assert(near(result.outgoingSpeed, 765.f));
	assert(std::isfinite(result.normal.x));
	assert(std::isfinite(result.normal.y));
	assert(std::isfinite(result.normal.z));
}

void testWeaponRejectsParallelAndSeparatedPaths() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	assert(!system.evaluateWeapon(
		projectile(6, { 0.f, -15.f, 0.f }, { 0.f, 15.f, 0.f },
		           { 0.f, 900.f, 0.f }, 5000),
		verticalWeapon(), result));
	assert(!system.evaluateWeapon(
		projectile(7, { -15.f, 0.f, 30.f }, { 15.f, 0.f, 30.f },
		           { 900.f, 0.f, 0.f }, 5100),
		verticalWeapon(), result));
}

void testMinimumSpeedAndInvalidSamplesFailClosed() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	assert(!system.evaluateShield(
		projectile(8, { 0.f, 0.f, 5.f }, { 0.f, 0.f, -5.f },
		           { 0.f, 0.f, -20.f }, 6000),
		VrShieldProfile{}, frontShield(), result));

	VrProjectileSample invalid = projectile(
		9, { 0.f, 0.f, 5.f }, { 0.f, 0.f, -5.f },
		{ 0.f, 0.f, -500.f }, 6100);
	invalid.velocity.x = std::numeric_limits<float>::quiet_NaN();
	assert(!system.evaluateShield(invalid, VrShieldProfile{}, frontShield(), result));
	invalid = projectile(0, { 0.f, 0.f, 5.f }, { 0.f, 0.f, -5.f },
	                     { 0.f, 0.f, -500.f }, 6200);
	assert(!system.evaluateShield(invalid, VrShieldProfile{}, frontShield(), result));
}

void testContactLatchSuppressesImmediateRepeatedContact() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	assert(system.evaluateShield(
		projectile(10, { 0.f, 0.f, 8.f }, { 0.f, 0.f, -2.f },
		           { 0.f, 0.f, -500.f }, 7000),
		VrShieldProfile{}, frontShield(), result));
	// The same token remains in the contact neighbourhood on the next frame.
	assert(!system.evaluateShield(
		projectile(10, { 0.f, 0.f, 6.f }, { 0.f, 0.f, 2.f },
		           { 0.f, 0.f, 375.f }, 7100),
		VrShieldProfile{}, frontShield(), result));
	// Timestamp regressions also stay fail-closed while latched.
	assert(!system.evaluateShield(
		projectile(10, { 0.f, 0.f, 6.f }, { 0.f, 0.f, 2.f },
		           { 0.f, 0.f, 375.f }, 6999),
		VrShieldProfile{}, frontShield(), result));
}

void testLatchRearmsAfterPhysicalSeparation() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	assert(system.evaluateShield(
		projectile(11, { 0.f, 0.f, 10.f }, { 0.f, 0.f, -10.f },
		           { 0.f, 0.f, -500.f }, 8000),
		VrShieldProfile{}, frontShield(), result));
	// A later, physically separated return can be reflected again.
	assert(system.evaluateShield(
		projectile(11, { 0.f, 0.f, 30.f }, { 0.f, 0.f, -30.f },
		           { 0.f, 0.f, -500.f }, 9000),
		VrShieldProfile{}, frontShield(), result));
}

void testResetClearsContactLatch() {
	VrProjectileDefenseSystem system;
	VrProjectileDeflection result;
	const VrProjectileSample sample = projectile(
		12, { 0.f, 0.f, 8.f }, { 0.f, 0.f, -2.f },
		{ 0.f, 0.f, -500.f }, 10000);
	assert(system.evaluateShield(sample, VrShieldProfile{}, frontShield(), result));
	system.resetSession();
	VrProjectileSample repeated = sample;
	repeated.timestampUs = 10001;
	assert(system.evaluateShield(repeated, VrShieldProfile{}, frontShield(), result));
}

void testRestitutionNeverAddsEnergy() {
	VrProjectileDefenseConfig config;
	config.shieldRestitution = 1.f;
	config.weaponRestitution = 1.f;
	VrProjectileDefenseSystem system(config);
	VrProjectileDeflection result;
	assert(system.evaluateShield(
		projectile(13, { 0.f, 0.f, 10.f }, { 0.f, 0.f, -10.f },
		           { 120.f, 0.f, -800.f }, 11000),
		VrShieldProfile{}, frontShield(), result));
	assert(result.outgoingSpeed <= result.incomingSpeed + 0.001f);
}

} // namespace

int main() {
	testShieldReflectsFrontalProjectile();
	testShieldRejectsRearAndFiniteMisses();
	testProjectileRadiusExpandsShieldContact();
	testWeaponReflectsCrossingProjectile();
	testWeaponRejectsParallelAndSeparatedPaths();
	testMinimumSpeedAndInvalidSamplesFailClosed();
	testContactLatchSuppressesImmediateRepeatedContact();
	testLatchRearmsAfterPhysicalSeparation();
	testResetClearsContactLatch();
	testRestitutionNeverAddsEnergy();
	return 0;
}
