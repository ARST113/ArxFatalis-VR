#pragma once

#include "vr/VrDefenseRuntime.h"
#include "vr/VrProjectileDefense.h"

namespace arxvr {

// Route a physical projectile sweep through the defensive geometry published by
// the live VR frame. The standalone classifier owns contact/rearm state while
// VrDefenseRuntime owns freshness of tracked shield/weapon poses. Keeping this
// adapter runtime-neutral lets deterministic tests exercise the exact routing
// policy used by gameplay without depending on OpenXR or Entity internals.
inline bool vrEvaluateProjectileDefense(VrDefenseRuntime & runtime,
                                        VrProjectileDefenseSystem & system,
                                        const VrProjectileSample & sample,
                                        VrProjectileDeflection & result) {
	result = VrProjectileDeflection{};
	if(!vrProjectileSampleValid(sample)) {
		return false;
	}

	// The shield gets first refusal because its finite defensive volume is the
	// explicit off-hand guard surface. If no fresh shield intersects the sweep,
	// the currently published physical weapon can deflect it instead.
	if(runtime.shieldActiveAt(sample.timestampUs)) {
		const VrPublishedShield & shield = runtime.shield();
		if(system.evaluateShield(sample, shield.profile, shield.pose, result)) {
			return true;
		}
	}

	if(runtime.defenderWeaponActiveAt(sample.timestampUs)) {
		const VrPublishedDefenderWeapon & weapon = runtime.defenderWeapon();
		if(system.evaluateWeapon(sample, weapon.segment, result)) {
			return true;
		}
	}

	result = VrProjectileDeflection{};
	return false;
}

} // namespace arxvr
