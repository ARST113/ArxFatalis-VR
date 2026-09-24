#include "vr/VrStrikeClassifier.h"

#include <cmath>
#include <cstdint>
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

arxvr::VrMotionSample sample(float x, float y, std::uint64_t timeUs) {
	return { x, y, 0.f, timeUs };
}

void feedPunch(arxvr::VrStrikeClassifier & classifier, std::uint64_t startUs,
               float offsetX, const arxvr::VrStrikeProfile & profile) {
	classifier.update(sample(offsetX + 0.f, 0.f, startUs + 0), profile);
	classifier.update(sample(offsetX + 2.f, 0.2f, startUs + 20000), profile);
	classifier.update(sample(offsetX + 5.f, 0.6f, startUs + 40000), profile);
	classifier.update(sample(offsetX + 9.f, 1.2f, startUs + 60000), profile);
	classifier.update(sample(offsetX + 14.f, 2.f, startUs + 80000), profile);
}

void deliberatePunchQualifies() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrFistStrikeProfile();
	feedPunch(classifier, 0, 0.f, profile);
	const auto & metrics = classifier.metrics();
	expect(classifier.canStrike(profile), "deliberate punch should qualify");
	expect(metrics.directionalConsistency > 0.95f,
	       "deliberate punch should have strong directional consistency");
	expect(metrics.peakSpeed > profile.minPeakSpeed,
	       "deliberate punch should exceed peak-speed threshold");
	expect(classifier.consumeStrike(profile), "qualifying punch should be consumable");
	expect(!classifier.canStrike(profile), "consumed strike must disarm classifier");
}

void waggleIsRejected() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrFistStrikeProfile();
	classifier.update(sample(0.f, 0.f, 0), profile);
	classifier.update(sample(4.f, 0.f, 20000), profile);
	classifier.update(sample(0.f, 0.f, 40000), profile);
	classifier.update(sample(4.f, 0.f, 60000), profile);
	classifier.update(sample(0.f, 0.f, 80000), profile);
	const auto & metrics = classifier.metrics();
	expect(metrics.peakSpeed > profile.minPeakSpeed,
	       "waggle fixture must be fast enough to challenge the filter");
	expect(metrics.energy > profile.minEnergy,
	       "waggle fixture must have enough energy to challenge the filter");
	expect(metrics.directionalConsistency < profile.minDirectionalConsistency,
	       "back-and-forth waggle should have poor directional consistency");
	expect(!classifier.canStrike(profile), "back-and-forth waggle must not qualify");
}

void slowMotionIsRejected() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrFistStrikeProfile();
	classifier.update(sample(0.f, 0.f, 0), profile);
	classifier.update(sample(0.5f, 0.f, 30000), profile);
	classifier.update(sample(1.f, 0.f, 60000), profile);
	classifier.update(sample(1.5f, 0.f, 90000), profile);
	classifier.update(sample(2.f, 0.f, 120000), profile);
	expect(!classifier.canStrike(profile), "slow reach must not qualify as a strike");
	expect(classifier.metrics().peakSpeed < profile.minPeakSpeed,
	       "slow fixture should stay below peak-speed threshold");
}

void trackingJumpResetsHistory() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrFistStrikeProfile();
	classifier.update(sample(0.f, 0.f, 0), profile);
	const auto status = classifier.update(sample(100.f, 0.f, 10000), profile);
	expect(status == arxvr::VrMotionSampleStatus::TrackingReset,
	       "implausible controller jump must reset tracking history");
	expect(classifier.metrics().sampleCount == 1,
	       "tracking reset should retain only the recovery sample");
	expect(!classifier.armed(), "tracking reset must temporarily disarm strikes");
}

void cooldownRequiresSlowRearm() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrFistStrikeProfile();
	feedPunch(classifier, 0, 0.f, profile);
	expect(classifier.consumeStrike(profile), "first strike should be consumed");

	classifier.update(sample(14.2f, 2.f, 180000), profile);
	classifier.update(sample(14.3f, 2.f, 280000), profile);
	classifier.update(sample(14.4f, 2.f, 380000), profile);
	expect(!classifier.armed(), "classifier must remain disarmed during cooldown");
	classifier.update(sample(14.5f, 2.f, 440000), profile);
	expect(classifier.armed(), "slow hand after cooldown should rearm classifier");

	feedPunch(classifier, 460000, 14.5f, profile);
	expect(classifier.canStrike(profile), "second deliberate punch should qualify after rearm");
	expect(classifier.consumeStrike(profile), "second strike should be consumable");
}

void curvedSwingQualifies() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrHeldObjectStrikeProfile();
	classifier.update(sample(0.f, 0.f, 0), profile);
	classifier.update(sample(2.f, 0.6f, 20000), profile);
	classifier.update(sample(5.f, 1.7f, 40000), profile);
	classifier.update(sample(9.f, 3.3f, 60000), profile);
	classifier.update(sample(14.f, 5.5f, 80000), profile);
	expect(classifier.metrics().directionalConsistency > 0.9f,
	       "curved swing should retain net directional progress");
	expect(classifier.canStrike(profile), "natural curved held-object swing should qualify");
}

void invalidAndOutOfOrderSamplesFailSafe() {
	arxvr::VrStrikeClassifier classifier;
	const auto profile = arxvr::vrFistStrikeProfile();
	classifier.update(sample(0.f, 0.f, 100000), profile);
	const auto outOfOrder = classifier.update(sample(1.f, 0.f, 90000), profile);
	expect(outOfOrder == arxvr::VrMotionSampleStatus::TrackingReset,
	       "timestamp regression should reset motion history");
	expect(!classifier.armed(), "timestamp regression should disarm classifier");

	auto invalid = sample(0.f, 0.f, 100000);
	invalid.x = std::numeric_limits<float>::quiet_NaN();
	const auto invalidStatus = classifier.update(invalid, profile);
	expect(invalidStatus == arxvr::VrMotionSampleStatus::Invalid,
	       "non-finite tracking sample should be rejected");
	expect(classifier.metrics().sampleCount == 0,
	       "invalid sample should clear potentially corrupted history");
	expect(!classifier.armed(),
	       "invalid tracking sample must fail closed until a slow rearm");
}

} // namespace

int main() {
	deliberatePunchQualifies();
	waggleIsRejected();
	slowMotionIsRejected();
	trackingJumpResetsHistory();
	cooldownRequiresSlowRearm();
	curvedSwingQualifies();
	invalidAndOutOfOrderSamplesFailSafe();

	if(g_failures != 0) {
		std::cerr << g_failures << " strike-classifier test(s) failed\n";
		return 1;
	}
	std::cout << "VrStrikeClassifier: all deterministic motion tests passed\n";
	return 0;
}
