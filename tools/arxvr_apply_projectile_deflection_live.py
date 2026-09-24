#!/usr/bin/env python3
"""Apply the Phase 5 live projectile-defense adapter to Projectile.cpp.

The upstream file uses CRLF line endings. Work on an LF-normalized string, then
restore the original newline convention so the generated commit contains only
the semantic integration diff.
"""

from pathlib import Path
import sys

TARGET = Path("ArxAndroid/src/physics/Projectile.cpp")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


def main() -> int:
    raw = TARGET.read_bytes()
    if b"\x00" in raw:
        raise RuntimeError("Projectile.cpp unexpectedly contains NUL bytes")
    newline = "\r\n" if raw.count(b"\r\n") >= raw.count(b"\n") // 2 else "\n"
    text = raw.decode("utf-8").replace("\r\n", "\n")

    marker = "arxvrTryDeflectProjectile"
    if marker in text:
        print("live projectile defense already integrated; no changes needed")
        return 0

    text = replace_once(
        text,
        '#include <memory>\n#include <string_view>\n',
        '#include <cstdint>\n#include <memory>\n#include <string_view>\n',
        "standard includes",
    )

    text = replace_once(
        text,
        '#include "util/Flags.h"\n#include "util/Range.h"\n\n',
        '#include "util/Flags.h"\n#include "util/Range.h"\n\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        '#include "vr/VrHaptics.h"\n'
        '#include "vr/VrProjectileDefenseRuntime.h"\n'
        '#endif\n\n',
        "VR includes",
    )

    text = replace_once(
        text,
        '\tEntityHandle source;\n\tVertexId attach;\n\tProjectileFlags flags;\n\t\n\tProjectile() arx_noexcept_default\n',
        '\tEntityHandle source;\n\tVertexId attach;\n\tProjectileFlags flags;\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        '\t// Stable across std::vector moves/reallocations for the lifetime of this projectile.\n'
        '\tstd::uint64_t vrDefenseToken = 0;\n'
        '#endif\n'
        '\t\n\tProjectile() arx_noexcept_default\n',
        "projectile token field",
    )

    text = replace_once(
        text,
        'static std::vector<Projectile> g_projectiles;\n\n',
        'static std::vector<Projectile> g_projectiles;\n\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        'static arxvr::VrProjectileDefenseSystem g_vrProjectileDefense;\n'
        'static std::uint64_t g_vrNextProjectileDefenseToken = 1;\n\n'
        'static std::uint64_t arxvrNextProjectileDefenseToken() {\n'
        '\tconst std::uint64_t token = g_vrNextProjectileDefenseToken++;\n'
        '\tif(g_vrNextProjectileDefenseToken == 0) {\n'
        '\t\tg_vrNextProjectileDefenseToken = 1;\n'
        '\t}\n'
        '\treturn token == 0 ? g_vrNextProjectileDefenseToken++ : token;\n'
        '}\n'
        '#endif\n\n',
        "projectile defense globals",
    )

    text = replace_once(
        text,
        'void ARX_THROWN_OBJECT_KillAll() {\n\tg_projectiles.clear();\n}\n',
        'void ARX_THROWN_OBJECT_KillAll() {\n'
        '\tg_projectiles.clear();\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        '\tg_vrProjectileDefense.resetSession();\n'
        '#endif\n'
        '}\n',
        "projectile session reset",
    )

    text = replace_once(
        text,
        '\tProjectile & projectile = g_projectiles.emplace_back();\n\t\n\tprojectile.damages = damages;\n',
        '\tProjectile & projectile = g_projectiles.emplace_back();\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        '\tprojectile.vrDefenseToken = arxvrNextProjectileDefenseToken();\n'
        '#endif\n'
        '\t\n\tprojectile.damages = damages;\n',
        "projectile token assignment",
    )

    helper = r'''#if defined(ARXVR_ANDROID_BUILD)
static float arxvrProjectileContactRadius(const Projectile & projectile) {
	float radius = 1.f;
	for(const EERIE_ACTIONLIST & action : projectile.obj->actionlist) {
		const float hit = GetHitValue(action.name);
		if(std::isfinite(hit) && hit >= 0.f) {
			radius = std::max(radius, hit * 0.5f);
		}
	}
	// Malformed action metadata must not turn an arrow into a room-sized guard
	// sweep. The upper bound is intentionally generous for non-arrow throwables.
	return std::clamp(radius, 1.f, 16.f);
}

static bool arxvrTryDeflectProjectile(Projectile & projectile,
                                      const Vec3f & previousPosition) {
	if(projectile.source == EntityHandle_Player || projectile.vrDefenseToken == 0
	   || projectile.vector == Vec3f(0.f)) {
		return false;
	}

	arxvr::VrProjectileSample sample;
	sample.token = projectile.vrDefenseToken;
	sample.start = { previousPosition.x, previousPosition.y, previousPosition.z };
	sample.end = { projectile.position.x, projectile.position.y, projectile.position.z };
	// Projectile::vector is expressed in Arx world units per millisecond.
	sample.velocity = { projectile.vector.x * 1000.f,
	                    projectile.vector.y * 1000.f,
	                    projectile.vector.z * 1000.f };
	sample.radius = arxvrProjectileContactRadius(projectile);
	sample.timestampUs = arxvr::vrDefenseNowMicros();

	arxvr::VrProjectileDeflection deflection;
	if(!arxvr::vrEvaluateProjectileDefense(arxvr::vrDefenseRuntime(),
	                                      g_vrProjectileDefense,
	                                      sample, deflection)) {
		return false;
	}

	const Vec3f outgoing(deflection.outgoingVelocity.x,
	                     deflection.outgoingVelocity.y,
	                     deflection.outgoingVelocity.z);
	const float outgoingSpeed = glm::length(outgoing);
	if(!std::isfinite(outgoingSpeed) || outgoingSpeed <= 0.001f) {
		return false;
	}

	projectile.vector = outgoing * 0.001f;
	const Vec3f contact(deflection.position.x,
	                    deflection.position.y,
	                    deflection.position.z);
	const Vec3f direction = outgoing / outgoingSpeed;
	const float separation = std::max(sample.radius + 2.f, 4.f);
	projectile.position = contact + direction * separation;
	projectile.quat = getProjectileQuatFromVector(projectile.vector) * projectile.rotation;

	const float hapticStrength = std::clamp(deflection.incomingSpeed / 1200.f, 0.3f, 1.f);
	if(deflection.type == arxvr::VrProjectileDeflectionType::Shield) {
		arxvrEmitHaptic(VrHapticHand::Left, VrHapticEvent::Block, hapticStrength);
	} else if(deflection.type == arxvr::VrProjectileDeflectionType::Weapon) {
		arxvrEmitHaptic(VrHapticHand::Right, VrHapticEvent::Parry, hapticStrength);
	}
	ParticleSparkSpawn(contact, 12);
	ARX_SOUND_PlayCollision("dagger", "metal", 1.f, 1.f, contact, nullptr);
	return true;
}
#endif

'''

    text = replace_once(
        text,
        'void ARX_THROWN_OBJECT_Render() {\n',
        helper + 'void ARX_THROWN_OBJECT_Render() {\n',
        "live deflection helper",
    )

    text = replace_once(
        text,
        '\tVec3f original_pos = projectile.position;\n\tprojectile.position += projectile.vector * timeDeltaMs;\n\t\n'
        '\tif(projectile.gravity != 0.f) {\n',
        '\tVec3f original_pos = projectile.position;\n'
        '\tprojectile.position += projectile.vector * timeDeltaMs;\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        '\tarxvrTryDeflectProjectile(projectile, original_pos);\n'
        '#endif\n'
        '\t\n\tif(projectile.gravity != 0.f) {\n',
        "per-frame deflection route",
    )

    required = [
        '#include "vr/VrProjectileDefenseRuntime.h"',
        'std::uint64_t vrDefenseToken = 0;',
        'g_vrProjectileDefense.resetSession();',
        'projectile.vrDefenseToken = arxvrNextProjectileDefenseToken();',
        'static bool arxvrTryDeflectProjectile(',
        'vrEvaluateProjectileDefense(arxvr::vrDefenseRuntime()',
        'arxvrTryDeflectProjectile(projectile, original_pos);',
    ]
    for needle in required:
        if needle not in text:
            raise RuntimeError(f"postcondition missing: {needle}")

    if text.count('arxvrTryDeflectProjectile(projectile, original_pos);') != 1:
        raise RuntimeError("live deflection call must occur exactly once")

    output = text if newline == "\n" else text.replace("\n", "\r\n")
    TARGET.write_bytes(output.encode("utf-8"))
    print(f"integrated live projectile defense into {TARGET} using {repr(newline)} newlines")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise
