#!/usr/bin/env python3
"""Deterministically wire tracked shields into live Arx NPC melee.

The gameplay-facing defense classifier stays header-only and deterministic. This
adapter performs the two unavoidable source integrations:
  * publish the tracked off-hand shield pose once per gameplay frame;
  * sample NPC weapon action-point motion at ARX_EQUIPMENT_Strike_Check and
    suppress only the player weapon hit that was physically intercepted.

The Equipment.cpp patch is byte-preserving so its historical CRLF layout does
not turn a small semantic change into whole-file churn.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARX_GAME = ROOT / "ArxAndroid/src/core/ArxGame.cpp"
EQUIPMENT = ROOT / "ArxAndroid/src/game/Equipment.cpp"


def replace_once(path: Path, old: str, new: str, marker: str) -> None:
    raw = path.read_bytes()
    if marker.encode("utf-8") in raw:
        return

    eol = b"\r\n" if raw.count(b"\r\n") > raw.count(b"\n") // 2 else b"\n"
    old_bytes = old.encode("utf-8").replace(b"\n", eol)
    new_bytes = new.encode("utf-8").replace(b"\n", eol)
    count = raw.count(old_bytes)
    if count != 1:
        raise RuntimeError(
            f"{path}: expected exactly one anchor for {marker!r}, found {count}"
        )
    path.write_bytes(raw.replace(old_bytes, new_bytes, 1))


def patch_arx_game() -> None:
    replace_once(
        ARX_GAME,
        '#include "vr/VrHaptics.h"\n#include "vr/VrInteractionSystem.h"',
        '#include "vr/VrDefenseRuntime.h"\n#include "vr/VrHaptics.h"\n#include "vr/VrInteractionSystem.h"',
        '#include "vr/VrDefenseRuntime.h"',
    )

    replace_once(
        ARX_GAME,
        "\t\tg_vrInteractions.resetSession();\n\t\tg_vrWeaponSystem.reset();\n\t\tg_vrInteractionTarget = EntityHandle();",
        "\t\tg_vrInteractions.resetSession();\n\t\tg_vrWeaponSystem.reset();\n\t\tarxvr::vrDefenseRuntime().resetSession();\n\t\tg_vrInteractionTarget = EntityHandle();",
        "arxvr::vrDefenseRuntime().resetSession();",
    )

    replace_once(
        ARX_GAME,
        "\tconst bool physicalDragActive = isVrPhysicalDragActive();\n"
        "\tconst bool rightDragging = physicalDragActive && g_vrPhysicalDragUsesRightHand;\n"
        "\tconst bool leftDragging = physicalDragActive && !g_vrPhysicalDragUsesRightHand;\n\n"
        "\t// Each physical hand owns exactly one semantic impact source per frame.",
        "\tconst bool physicalDragActive = isVrPhysicalDragActive();\n"
        "\tconst bool rightDragging = physicalDragActive && g_vrPhysicalDragUsesRightHand;\n"
        "\tconst bool leftDragging = physicalDragActive && !g_vrPhysicalDragUsesRightHand;\n\n"
        "\t// Publish the actual off-hand controller pose only while a shield is\n"
        "\t// equipped and the hand is available for defense. The runtime applies a\n"
        "\t// short freshness window, so tracking loss fails closed without leaving\n"
        "\t// a frozen shield collider active in front of the player.\n"
        "\tEntity * equippedShield = entities.get(player.equiped[EQUIP_SLOT_SHIELD]);\n"
        "\tif(equippedShield && haveLeftHand && !leftDragging && !BLOCK_PLAYER_CONTROLS) {\n"
        "\t\tarxvr::VrShieldPose shieldPose;\n"
        "\t\tshieldPose.center = vrImpactVector(leftHandPosition);\n"
        "\t\tshieldPose.normal = vrImpactVector(leftHandDirection);\n"
        "\t\tshieldPose.up = vrImpactVector(leftHandOrientation * Vec3f(0.f, 1.f, 0.f));\n"
        "\t\tshieldPose.valid = true;\n"
        "\t\tarxvr::vrDefenseRuntime().publishShield(\n"
        "\t\t\tstatic_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(equippedShield)),\n"
        "\t\t\tarxvr::VrShieldProfile{}, shieldPose, impactTimestampUs);\n"
        "\t} else {\n"
        "\t\tarxvr::vrDefenseRuntime().clearShield();\n"
        "\t}\n\n"
        "\t// Each physical hand owns exactly one semantic impact source per frame.",
        "equippedShield && haveLeftHand && !leftDragging",
    )


def patch_equipment() -> None:
    replace_once(
        EQUIPMENT,
        "#include <cstdlib>\n#include <cstring>",
        "#include <cstdlib>\n#include <cstdint>\n#include <cstring>",
        "#include <cstdint>",
    )

    replace_once(
        EQUIPMENT,
        '#include "util/Number.h"\n#include "util/String.h"',
        '#include "util/Number.h"\n#include "util/String.h"\n\n'
        '#if defined(ARXVR_ANDROID_BUILD)\n'
        '#include "vr/VrDefenseRuntime.h"\n'
        '#include "vr/VrHaptics.h"\n'
        '#endif',
        '#include "vr/VrDefenseRuntime.h"',
    )

    replace_once(
        EQUIPMENT,
        "\t\tif(io_source != entities.player()) {\n"
        "\t\t\tsphere.radius += 15.f;\n"
        "\t\t}\n\t\t\n"
        "\t\tstd::vector<Entity *> sphereContent;",
        "\t\tif(io_source != entities.player()) {\n"
        "\t\t\tsphere.radius += 15.f;\n"
        "\t\t}\n\n"
        "#if defined(ARXVR_ANDROID_BUILD)\n"
        "\t\tbool vrShieldBlocked = false;\n"
        "\t\tif(io_source != entities.player()) {\n"
        "\t\t\tconst std::uint64_t sourceToken = static_cast<std::uint64_t>(\n"
        "\t\t\t\treinterpret_cast<std::uintptr_t>(io_source));\n"
        "\t\t\tconst std::uint64_t actionToken = static_cast<std::uint64_t>(\n"
        "\t\t\t\treinterpret_cast<std::uintptr_t>(io_weapon))\n"
        "\t\t\t\t^ (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&action)) << 1u);\n"
        "\t\t\tarxvr::VrDefenseEvent defenseEvent;\n"
        "\t\t\tvrShieldBlocked = arxvr::vrDefenseRuntime().sampleShieldBlock(\n"
        "\t\t\t\tsourceToken, actionToken,\n"
        "\t\t\t\t{ sphere.origin.x, sphere.origin.y, sphere.origin.z },\n"
        "\t\t\t\tarxvr::vrDefenseNowMicros(), defenseEvent);\n"
        "\t\t\tif(vrShieldBlocked) {\n"
        "\t\t\t\tconst float strength = std::clamp(defenseEvent.relativeSpeed / 3000.f,\n"
        "\t\t\t\t                                  0.35f, 1.f);\n"
        "\t\t\t\tarxvrEmitHaptic(VrHapticHand::Left, VrHapticEvent::Block, strength);\n"
        "\t\t\t}\n"
        "\t\t}\n"
        "#endif\n\n"
        "\t\tstd::vector<Entity *> sphereContent;",
        "vrShieldBlocked = arxvr::vrDefenseRuntime().sampleShieldBlock",
    )

    replace_once(
        EQUIPMENT,
        "\t\t\t\t\tColor color = (target->ioflags & IO_NPC) ? target->_npcdata->blood_color : Color::white;\n"
        "\t\t\t\t\tVec3f pos = target->obj->vertexWorldPositions[hitpoint].v;\n\t\t\t\t\t\n"
        "\t\t\t\t\tfloat dmgs = 0.f;",
        "\t\t\t\t\tColor color = (target->ioflags & IO_NPC) ? target->_npcdata->blood_color : Color::white;\n"
        "\t\t\t\t\tVec3f pos = target->obj->vertexWorldPositions[hitpoint].v;\n\n"
        "#if defined(ARXVR_ANDROID_BUILD)\n"
        "\t\t\t\t\t// A physical shield block consumes only this NPC equipment strike.\n"
        "\t\t\t\t\t// Generic damage, spells and environmental sources continue through\n"
        "\t\t\t\t\t// their existing pipelines unchanged.\n"
        "\t\t\t\t\tif(target == entities.player() && vrShieldBlocked) {\n"
        "\t\t\t\t\t\tcontinue;\n"
        "\t\t\t\t\t}\n"
        "#endif\n\n"
        "\t\t\t\t\tfloat dmgs = 0.f;",
        "if(target == entities.player() && vrShieldBlocked)",
    )


def validate() -> None:
    game = ARX_GAME.read_text(encoding="utf-8", errors="strict")
    equipment = EQUIPMENT.read_text(encoding="utf-8", errors="strict")

    required_game = (
        '#include "vr/VrDefenseRuntime.h"',
        "arxvr::vrDefenseRuntime().resetSession();",
        "equippedShield && haveLeftHand && !leftDragging",
        "arxvr::vrDefenseRuntime().publishShield(",
    )
    required_equipment = (
        '#include "vr/VrDefenseRuntime.h"',
        "vrShieldBlocked = arxvr::vrDefenseRuntime().sampleShieldBlock",
        "arxvrEmitHaptic(VrHapticHand::Left, VrHapticEvent::Block, strength);",
        "if(target == entities.player() && vrShieldBlocked)",
    )
    missing = [item for item in required_game if item not in game]
    missing += [item for item in required_equipment if item not in equipment]
    if missing:
        raise RuntimeError(f"live defense integration validation failed: {missing}")


if __name__ == "__main__":
    patch_arx_game()
    patch_equipment()
    validate()
    print("ArxVR Phase 5 live shield defense integration applied")
