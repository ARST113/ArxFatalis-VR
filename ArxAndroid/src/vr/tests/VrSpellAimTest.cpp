#include "vr/VrSpellAim.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace {

bool near(float a, float b, float epsilon = 1.0e-5f) {
	return std::fabs(a - b) <= epsilon;
}

void normalizesTrackedDirectionWithoutMovingOrigin() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.origin = { 12.f, -34.f, 56.f };
	sample.forward = { 0.f, 0.f, 5.f };
	sample.trackingValid = true;

	const arxvr::VrSpellAimRay ray = service.update(sample);
	assert(ray.valid);
	assert(near(ray.origin.x, 12.f));
	assert(near(ray.origin.y, -34.f));
	assert(near(ray.origin.z, 56.f));
	assert(near(ray.direction.x, 0.f));
	assert(near(ray.direction.y, 0.f));
	assert(near(ray.direction.z, 1.f));
}

void acceptsObliqueAndVerticalPhysicalAim() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.origin = { 1.f, 2.f, 3.f };
	sample.forward = { -2.f, 3.f, 6.f };
	sample.trackingValid = true;

	arxvr::VrSpellAimRay ray = service.update(sample);
	assert(ray.valid);
	assert(near(arxvr::vrSpellLengthSquared(ray.direction), 1.f));

	sample.forward = { 0.f, -7.f, 0.f };
	ray = service.update(sample);
	assert(ray.valid);
	assert(near(ray.direction.x, 0.f));
	assert(near(ray.direction.y, -1.f));
	assert(near(ray.direction.z, 0.f));
}

void trackingLossClearsAPreviouslyValidRay() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.origin = { 4.f, 5.f, 6.f };
	sample.forward = { 1.f, 0.f, 0.f };
	sample.trackingValid = true;
	assert(service.update(sample).valid);

	sample.trackingValid = false;
	assert(!service.update(sample).valid);
	assert(!service.ray().valid);
}

void rejectsDegenerateAndNonFiniteSamples() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.trackingValid = true;
	sample.origin = { 0.f, 0.f, 0.f };
	sample.forward = { 0.f, 0.f, 0.f };
	assert(!service.update(sample).valid);

	sample.forward = { 0.f, 0.f, 1.f };
	sample.origin.x = std::numeric_limits<float>::quiet_NaN();
	assert(!service.update(sample).valid);

	sample.origin = { 0.f, 0.f, 0.f };
	sample.forward.z = std::numeric_limits<float>::infinity();
	assert(!service.update(sample).valid);
}

void explicitClearInvalidatesPublishedAim() {
	arxvr::VrSpellAimService service;
	arxvr::VrSpellAimSample sample;
	sample.origin = { 3.f, 2.f, 1.f };
	sample.forward = { 1.f, 2.f, 3.f };
	sample.trackingValid = true;
	assert(service.update(sample).valid);
	service.clear();
	assert(!service.ray().valid);
}

} // namespace

int main() {
	normalizesTrackedDirectionWithoutMovingOrigin();
	acceptsObliqueAndVerticalPhysicalAim();
	trackingLossClearsAPreviouslyValidRay();
	rejectsDegenerateAndNonFiniteSamples();
	explicitClearInvalidatesPublishedAim();
	return 0;
}
