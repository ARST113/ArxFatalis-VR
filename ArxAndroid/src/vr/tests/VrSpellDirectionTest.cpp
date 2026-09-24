#include "vr/VrSpellAim.h"
#include "vr/VrSpellDirection.h"

#include <cassert>
#include <cmath>

namespace {

bool near(float a, float b, float epsilon = 1.0e-5f) {
	return std::fabs(a - b) <= epsilon;
}

void normalizesReusableSpellDirections() {
	arxvr::VrSpellVector3 normalized;
	assert(arxvr::vrSpellNormalizeDirection({ 3.f, 4.f, 0.f }, normalized));
	assert(near(normalized.x, 0.6f));
	assert(near(normalized.y, 0.8f));
	assert(near(normalized.z, 0.f));

	assert(!arxvr::vrSpellNormalizeDirection({ 0.f, 0.f, 0.f }, normalized));
}

void horizontalProjectionPreservesYaw() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.trackingValid = true;
	sample.origin = { 4.f, 5.f, 6.f };
	sample.forward = { 3.f, 12.f, -4.f };
	const arxvr::VrSpellAimRay ray = service.update(sample);
	assert(ray.valid);

	arxvr::VrSpellVector3 horizontal;
	assert(arxvr::vrSpellHorizontalDirection(ray, horizontal));
	assert(near(horizontal.x, 0.6f));
	assert(near(horizontal.y, 0.f));
	assert(near(horizontal.z, -0.8f));
	assert(near(arxvr::vrSpellLengthSquared(horizontal), 1.f));
}

void verticalAndInvalidAimFailClosedForGroundSpells() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.trackingValid = true;
	sample.origin = { 0.f, 0.f, 0.f };
	sample.forward = { 0.f, -9.f, 0.f };
	const arxvr::VrSpellAimRay vertical = service.update(sample);
	assert(vertical.valid);

	arxvr::VrSpellVector3 horizontal;
	assert(!arxvr::vrSpellHorizontalDirection(vertical, horizontal));

	arxvr::VrSpellAimRay invalid;
	assert(!arxvr::vrSpellHorizontalDirection(invalid, horizontal));
}

} // namespace

int main() {
	normalizesReusableSpellDirections();
	horizontalProjectionPreservesYaw();
	verticalAndInvalidAimFailClosedForGroundSpells();
	return 0;
}
