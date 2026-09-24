#include "vr/VrHaptics.h"

#include <algorithm>

#include "vr/AndroidVrBridge.h"
#include "vr/VrConfig.h"

namespace {

struct HapticPreset {
	float amplitude;
	float durationSeconds;
	float frequencyHz;
};

HapticPreset presetFor(VrHapticEvent event) {
	switch(event) {
		case VrHapticEvent::Grab:         return { 0.34f, 0.035f, 0.f };
		case VrHapticEvent::Release:      return { 0.18f, 0.020f, 0.f };
		case VrHapticEvent::UiTick:       return { 0.14f, 0.014f, 0.f };
		case VrHapticEvent::UiConfirm:    return { 0.28f, 0.028f, 0.f };
		case VrHapticEvent::ImpactLight:  return { 0.42f, 0.035f, 0.f };
		case VrHapticEvent::ImpactHeavy:  return { 0.76f, 0.065f, 0.f };
		case VrHapticEvent::Block:        return { 0.62f, 0.050f, 0.f };
		case VrHapticEvent::Parry:        return { 0.90f, 0.060f, 0.f };
		case VrHapticEvent::RuneAccepted: return { 0.30f, 0.035f, 0.f };
		case VrHapticEvent::RuneRejected: return { 0.22f, 0.070f, 0.f };
		case VrHapticEvent::SpellCast:    return { 0.55f, 0.060f, 0.f };
		case VrHapticEvent::BowString:    return { 0.12f, 0.020f, 0.f };
		case VrHapticEvent::Lever:        return { 0.46f, 0.045f, 0.f };
	}
	return { 0.f, 0.f, 0.f };
}

} // namespace

void arxvrEmitHaptic(VrHapticHand hand, VrHapticEvent event, float strength) {
	const HapticPreset preset = presetFor(event);
	const float eventStrength = std::clamp(strength, 0.f, 1.f);
	if(preset.amplitude <= 0.f || preset.durationSeconds <= 0.f || eventStrength <= 0.f) {
		return;
	}

	// System properties are the temporary settings surface for the Android port.
	// Reading the typed config here keeps haptic strength live-reloadable without
	// coupling gameplay call sites to Android or OpenXR details.
	const float configuredStrength = arxvrLoadRuntimeConfig().hapticStrength;
	const float effectiveStrength = eventStrength * configuredStrength;
	if(effectiveStrength <= 0.f) {
		return;
	}

	ArxVrHapticRequest request = {};
	request.version = ARXVR_HAPTIC_REQUEST_VERSION;
	request.hand = static_cast<std::uint32_t>(hand);
	request.event = static_cast<std::uint32_t>(event);
	request.amplitude = std::clamp(preset.amplitude * effectiveStrength, 0.f, 1.f);
	request.durationSeconds = preset.durationSeconds;
	request.frequencyHz = preset.frequencyHz;
	arxvrQueueHapticRequest(request);
}
