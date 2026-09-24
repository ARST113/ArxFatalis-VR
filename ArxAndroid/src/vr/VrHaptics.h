/*
 * Semantic VR haptics for the Android/OpenXR build.
 *
 * Gameplay code emits meaning (grab, impact, lever, ...). The Android bridge
 * serialises that into a tiny ABI request that the OpenXR host drains and
 * submits through xrApplyHapticFeedback.
 */
#pragma once

#include <cstdint>

enum class VrHapticHand : std::uint32_t {
	Left = 0,
	Right = 1
};

enum class VrHapticEvent : std::uint32_t {
	Grab = 1,
	Release,
	UiTick,
	UiConfirm,
	ImpactLight,
	ImpactHeavy,
	Block,
	Parry,
	RuneAccepted,
	RuneRejected,
	SpellCast,
	BowString,
	Lever
};

void arxvrEmitHaptic(VrHapticHand hand, VrHapticEvent event, float strength = 1.f);
