#include "vr/VrWeaponContact.h"

#include <cmath>
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

void segmentUsesNormalizedTrackedDirectionAndProfileReach() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	const arxvr::VrWeaponSegment segment = arxvr::vrBuildWeaponSegment(
		profile, { 10.f, 20.f, 30.f }, { 5.f, 0.f, 0.f });
	expect(segment.valid, "one-handed melee profile should produce a valid segment");
	expect(std::abs(segment.end.x - (10.f + profile.reach)) < 0.0001f,
	       "weapon segment should normalize controller direction before applying reach");
	expect(segment.end.y == 20.f && segment.end.z == 30.f,
	       "normalized horizontal direction should not introduce other-axis drift");
	expect(segment.radius == profile.contactRadius,
	       "contact radius should come from the weapon profile");
}

void unsupportedOrMalformedWeaponsFailClosed() {
	const auto bow = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Bow);
	const auto unknown = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Unknown);
	expect(!arxvr::vrBuildWeaponSegment(bow, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }).valid,
	       "bow must not accidentally acquire melee contact geometry");
	expect(!arxvr::vrBuildWeaponSegment(unknown, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }).valid,
	       "unknown equipment must fail closed");

	auto melee = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Dagger);
	expect(!arxvr::vrBuildWeaponSegment(melee, { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }).valid,
	       "zero-length tracked direction must not generate a weapon segment");
	const float nan = std::numeric_limits<float>::quiet_NaN();
	expect(!arxvr::vrBuildWeaponSegment(melee, { nan, 0.f, 0.f }, { 1.f, 0.f, 0.f }).valid,
	       "non-finite tracked position must fail closed");
}

void contactRadiusExpandsNpcBounds() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::OneHanded);
	const arxvr::VrWeaponSegment segment = arxvr::vrBuildWeaponSegment(
		profile, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f });
	float entryT = -1.f;
	expect(arxvr::vrWeaponSegmentIntersectsAabb(
	           segment, { 45.f, 8.f, -2.f }, { 55.f, 18.f, 2.f }, entryT),
	       "weapon radius should qualify a near-miss that enters the expanded target bounds");
	expect(entryT >= 0.f && entryT <= 1.f,
	       "contact entry parameter should remain on the finite weapon segment");

	float missT = -1.f;
	expect(!arxvr::vrWeaponSegmentIntersectsAabb(
	           segment, { 45.f, 40.f, -2.f }, { 55.f, 50.f, 2.f }, missT),
	       "target outside weapon reach radius should remain a miss");
}

void nearestContactPointComesFromEntryParameter() {
	const auto profile = arxvr::vrDefaultWeaponProfile(arxvr::VrWeaponClass::Dagger);
	const arxvr::VrWeaponSegment segment = arxvr::vrBuildWeaponSegment(
		profile, { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f });
	float entryT = 0.f;
	expect(arxvr::vrWeaponSegmentIntersectsAabb(
	           segment, { 30.f, -2.f, -2.f }, { 35.f, 2.f, 2.f }, entryT),
	       "dagger should intersect the test target");
	const arxvr::VrImpactVector3 contact = arxvr::vrWeaponSegmentPoint(segment, entryT);
	expect(contact.x >= 0.f && contact.x <= profile.reach,
	       "contact estimate should stay between grip and weapon tip");
	expect(std::abs(contact.y) < 0.0001f && std::abs(contact.z) < 0.0001f,
	       "contact estimate should remain on the tracked weapon axis");
}

} // namespace

int main() {
	segmentUsesNormalizedTrackedDirectionAndProfileReach();
	unsupportedOrMalformedWeaponsFailClosed();
	contactRadiusExpandsNpcBounds();
	nearestContactPointComesFromEntryParameter();

	if(g_failures != 0) {
		std::cerr << g_failures << " weapon-contact test(s) failed\n";
		return 1;
	}
	std::cout << "VrWeaponContact: segment and expanded-AABB tests passed\n";
	return 0;
}
