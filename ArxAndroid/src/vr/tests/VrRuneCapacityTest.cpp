#include "vr/VrRuneSystem.h"

#include <cmath>
#include <iostream>

namespace {

int g_failures = 0;

void expect(bool condition, const char * message) {
	if(!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++g_failures;
	}
}

bool near(float a, float b, float epsilon = 0.02f) {
	return std::abs(a - b) <= epsilon;
}

arxvr::VrRunePlane plane() {
	arxvr::VrRunePlane result;
	result.origin = { 0.f, 0.f, 0.f };
	result.normal = { 0.f, 0.f, 1.f };
	result.up = { 0.f, 1.f, 0.f };
	result.valid = true;
	return result;
}

arxvr::VrRuneSample sample(std::uint64_t timestampUs, float x, float y,
                           bool pressed = true) {
	arxvr::VrRuneSample result;
	result.timestampUs = timestampUs;
	result.handPosition = { x, y, 0.f };
	result.trackingValid = true;
	result.paintPressed = pressed;
	return result;
}

void capacityEqualToMinimumStillQualifies() {
	arxvr::VrRuneConfig config;
	config.halfWidth = 100.f;
	config.halfHeight = 100.f;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 4;
	config.maximumPointCount = 4;
	config.maximumHandSpeed = 100000.f;
	arxvr::VrRuneSystem system(config);
	const auto drawingPlane = plane();

	system.update(sample(100000u, 0.f, 0.f), drawingPlane);
	system.update(sample(110000u, 10.f, 0.f), drawingPlane);
	system.update(sample(120000u, 20.f, 0.f), drawingPlane);
	system.update(sample(130000u, 20.f, 10.f), drawingPlane);
	system.update(sample(140000u, 20.f, 20.f), drawingPlane);
	system.update(sample(150000u, 30.f, 20.f), drawingPlane);

	expect(system.update(sample(160000u, 30.f, 20.f, false), drawingPlane)
	       == arxvr::VrRuneStatus::GestureReady,
	       "capacity pressure must not compact below the configured minimum recognizer point count");

	arxvr::VrRuneGesture gesture;
	expect(system.consumeGesture(gesture) && gesture.valid,
	       "capacity-limited gesture at minimumPointCount should remain consumable");
	expect(gesture.capacityLimited && gesture.points.size() == 4,
	       "bounded simplification should use the full configured recognizer budget");
	expect(near(gesture.points.front().x, 0.f) && near(gesture.points.front().y, 0.f),
	       "bounded simplification must preserve the original paint-down point");
	expect(near(gesture.points.back().x, 0.3f) && near(gesture.points.back().y, 0.2f),
	       "bounded simplification must preserve the latest physical endpoint");
	expect(near(gesture.pathLength, 50.f),
	       "recognizer simplification must not alter full physical path-length metrics");
	expect(gesture.bounds.valid && near(gesture.bounds.max.x, 0.3f)
	       && near(gesture.bounds.max.y, 0.2f),
	       "gesture bounds must cover all accepted samples after repeated simplification");
}

void twoPointBudgetPreservesEndpoints() {
	arxvr::VrRuneConfig config;
	config.halfWidth = 100.f;
	config.minimumPointDistance = 0.f;
	config.minimumPathLength = 1.f;
	config.minimumPointCount = 2;
	config.maximumPointCount = 2;
	config.maximumHandSpeed = 100000.f;
	arxvr::VrRuneSystem system(config);
	const auto drawingPlane = plane();

	system.update(sample(100000u, 0.f, 0.f), drawingPlane);
	system.update(sample(110000u, 10.f, 0.f), drawingPlane);
	system.update(sample(120000u, 20.f, 0.f), drawingPlane);
	system.update(sample(130000u, 30.f, 0.f), drawingPlane);
	expect(system.update(sample(140000u, 30.f, 0.f, false), drawingPlane)
	       == arxvr::VrRuneStatus::GestureReady,
	       "minimal legal point budget should remain valid after many accepted samples");

	arxvr::VrRuneGesture gesture;
	expect(system.consumeGesture(gesture) && gesture.points.size() == 2,
	       "two-point budget should stay bounded to exactly two retained points");
	expect(near(gesture.points.front().x, 0.f) && near(gesture.points.back().x, 0.3f),
	       "two-point budget must preserve start and newest endpoint rather than an early prefix");
}

} // namespace

int main() {
	capacityEqualToMinimumStillQualifies();
	twoPointBudgetPreservesEndpoints();
	if(g_failures != 0) {
		std::cerr << g_failures << " VrRuneCapacity test failure(s)\n";
		return 1;
	}
	std::cout << "VrRuneCapacity tests passed\n";
	return 0;
}
