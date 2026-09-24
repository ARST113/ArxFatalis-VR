/*
 * Runtime configuration for the standalone Android/OpenXR port.
 *
 * The first settings surface intentionally uses Android system properties so
 * it can be exercised on real headsets before the VR options menu exists.
 * The game-facing code consumes only this typed structure, so a persistent UI
 * can replace the property source without changing locomotion/crouch logic.
 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include <sys/system_properties.h>

enum class VrStanceMode {
	Standing,
	Seated
};

struct VrConfig {
	VrStanceMode stance = VrStanceMode::Standing;
	bool physicalCrouch = true;
	float snapTurnDegrees = 30.f;
	// Zero keeps automatic tallest-observed calibration. A positive value is a
	// floor-relative standing eye-height override in metres.
	float standingHeightOverrideMeters = 0.f;
	// Applied after tracked room-scale translation. Useful for accessibility
	// calibration without modifying Arx's canonical player dimensions.
	float eyeHeightOffsetMeters = 0.f;
};

inline std::string arxvrReadSystemProperty(const char * name) {
	char value[PROP_VALUE_MAX] = {};
	const int length = __system_property_get(name, value);
	return length > 0 ? std::string(value, size_t(length)) : std::string();
}

inline bool arxvrParseBoolProperty(const std::string & value, bool fallback) {
	if(value == "1" || value == "true" || value == "on" || value == "yes") {
		return true;
	}
	if(value == "0" || value == "false" || value == "off" || value == "no") {
		return false;
	}
	return fallback;
}

inline float arxvrParseFloatProperty(const std::string & value, float fallback,
                                     float minimum, float maximum) {
	if(value.empty()) {
		return fallback;
	}
	char * end = nullptr;
	const float parsed = std::strtof(value.c_str(), &end);
	if(end == value.c_str() || *end != '\0' || !std::isfinite(parsed)) {
		return fallback;
	}
	return std::clamp(parsed, minimum, maximum);
}

inline VrConfig arxvrLoadRuntimeConfig() {
	VrConfig config;

	const std::string stance = arxvrReadSystemProperty("debug.arxvr.stance");
	if(stance == "seated" || stance == "sit") {
		config.stance = VrStanceMode::Seated;
	} else if(stance == "standing" || stance == "stand") {
		config.stance = VrStanceMode::Standing;
	}

	config.physicalCrouch = arxvrParseBoolProperty(
		arxvrReadSystemProperty("debug.arxvr.physical_crouch"), true);
	config.snapTurnDegrees = arxvrParseFloatProperty(
		arxvrReadSystemProperty("debug.arxvr.snap_turn"), 30.f, 15.f, 90.f);
	config.standingHeightOverrideMeters = arxvrParseFloatProperty(
		arxvrReadSystemProperty("debug.arxvr.player_height"), 0.f, 0.f, 2.4f);
	// Treat very small non-zero heights as an accidental property value rather
	// than a real calibration. 0 means automatic calibration.
	if(config.standingHeightOverrideMeters > 0.f
	   && config.standingHeightOverrideMeters < 1.f) {
		config.standingHeightOverrideMeters = 1.f;
	}
	config.eyeHeightOffsetMeters = arxvrParseFloatProperty(
		arxvrReadSystemProperty("debug.arxvr.eye_offset"), 0.f, -0.4f, 0.4f);

	return config;
}
