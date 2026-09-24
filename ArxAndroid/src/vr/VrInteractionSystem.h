#pragma once

#include <array>
#include <cstddef>

#include "vr/VrHandState.h"

namespace arxvr {

// Reusable owner for the two physical-hand interaction states. Gameplay feeds
// exactly one semantic source per hand each frame and consumes qualified impact
// events from this service. Target selection and Arx damage semantics remain a
// downstream concern, which keeps OpenXR/controller transport out of combat.
class VrInteractionSystem {
public:
	VrImpactGateStatus updateHand(VrHand hand, const VrImpactSample & sample) {
		if(!validHand(hand)) {
			return VrImpactGateStatus::Invalid;
		}
		return state(hand).update(sample);
	}

	bool canImpact(VrHand hand) const {
		return validHand(hand) && state(hand).canImpact();
	}

	bool consumeImpact(VrHand hand, VrImpactEvent & impact) {
		if(!validHand(hand)) {
			return false;
		}
		VrImpactEvent candidate;
		if(!state(hand).consumeQualifiedImpact(candidate)) {
			return false;
		}
		candidate.hand = hand;
		impact = candidate;
		return true;
	}

	void resetGesture(VrHand hand) {
		if(validHand(hand)) {
			state(hand).resetGesture();
		}
	}

	void resetSession() {
		for(VrHandState & hand : m_hands) {
			hand.resetSession();
		}
	}

	VrHandState & handState(VrHand hand) {
		return state(hand);
	}

	const VrHandState & handState(VrHand hand) const {
		return state(hand);
	}

private:
	static constexpr bool validHand(VrHand hand) {
		return hand == VrHand::Left || hand == VrHand::Right;
	}

	static constexpr std::size_t handIndex(VrHand hand) {
		return hand == VrHand::Left ? 0u : 1u;
	}

	VrHandState & state(VrHand hand) {
		return m_hands[handIndex(hand)];
	}

	const VrHandState & state(VrHand hand) const {
		return m_hands[handIndex(hand)];
	}

	std::array<VrHandState, 2> m_hands{};
};

} // namespace arxvr
