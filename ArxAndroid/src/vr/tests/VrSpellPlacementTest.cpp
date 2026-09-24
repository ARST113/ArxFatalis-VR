#include "vr/VrSpellAim.h"
#include "vr/VrSpellPlacement.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

bool near(float a, float b, float epsilon = 1.0e-4f) {
	return std::fabs(a - b) <= epsilon;
}

arxvr::VrSpellAimRay makeRay(arxvr::VrSpellVector3 origin,
                             arxvr::VrSpellVector3 forward) {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.trackingValid = true;
	sample.origin = origin;
	sample.forward = forward;
	return service.update(sample);
}

void placesGroundSpellFromHandXZAtLegacyHeight() {
	const arxvr::VrSpellAimRay ray = makeRay({ 10.f, 999.f, 20.f }, { 3.f, 4.f, 4.f });
	assert(ray.valid);

	arxvr::VrSpellPlacement placement;
	assert(arxvr::vrSpellHorizontalPlacement(ray, 77.f, 250.f, placement));
	assert(placement.valid);
	assert(near(placement.direction.x, 0.6f));
	assert(near(placement.direction.y, 0.f));
	assert(near(placement.direction.z, 0.8f));
	assert(near(placement.position.x, 160.f));
	assert(near(placement.position.y, 77.f));
	assert(near(placement.position.z, 220.f));
}

void computesYawUsingArxHorizontalConvention() {
	arxvr::VrSpellPlacement placement;

	assert(arxvr::vrSpellHorizontalPlacement(
		makeRay({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f }), 0.f, 1.f, placement));
	assert(near(placement.yawDegrees, 0.f));

	assert(arxvr::vrSpellHorizontalPlacement(
		makeRay({ 0.f, 0.f, 0.f }, { -1.f, 0.f, 0.f }), 0.f, 1.f, placement));
	assert(near(placement.yawDegrees, 90.f));

	assert(arxvr::vrSpellHorizontalPlacement(
		makeRay({ 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }), 0.f, 1.f, placement));
	assert(near(placement.yawDegrees, -90.f));
}

void preservesHandOriginWhenDistanceIsZero() {
	const arxvr::VrSpellAimRay ray = makeRay({ 5.f, 6.f, 7.f }, { 0.f, -1.f, 2.f });
	arxvr::VrSpellPlacement placement;
	assert(arxvr::vrSpellHorizontalPlacement(ray, 42.f, 0.f, placement));
	assert(near(placement.position.x, 5.f));
	assert(near(placement.position.y, 42.f));
	assert(near(placement.position.z, 7.f));
}

void rejectsVerticalInvalidAndNonFinitePlacementInputs() {
	arxvr::VrSpellPlacement placement;

	assert(!arxvr::vrSpellHorizontalPlacement(
		makeRay({ 0.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }), 0.f, 250.f, placement));
	assert(!placement.valid);

	arxvr::VrSpellAimRay invalid;
	assert(!arxvr::vrSpellHorizontalPlacement(invalid, 0.f, 250.f, placement));

	const float nan = std::numeric_limits<float>::quiet_NaN();
	const arxvr::VrSpellAimRay ray = makeRay({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 1.f });
	assert(!arxvr::vrSpellHorizontalPlacement(ray, nan, 250.f, placement));
	assert(!arxvr::vrSpellHorizontalPlacement(ray, 0.f, nan, placement));
	assert(!arxvr::vrSpellHorizontalPlacement(ray, 0.f, -1.f, placement));
}

} // namespace

int main() {
	placesGroundSpellFromHandXZAtLegacyHeight();
	computesYawUsingArxHorizontalConvention();
	preservesHandOriginWhenDistanceIsZero();
	rejectsVerticalInvalidAndNonFinitePlacementInputs();
	return 0;
}
