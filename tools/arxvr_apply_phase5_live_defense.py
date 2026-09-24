#!/usr/bin/env python3
"""Deterministically wire tracked shield and weapon defense into live Arx melee.

The runtime-neutral defense classifier stays testable without Arx/OpenXR types.
This adapter performs the unavoidable engine integrations:
  * publish the tracked off-hand shield pose once per gameplay frame;
  * publish the current physical player-weapon segment for parry qualification;
  * sample NPC weapon action-point motion at ARX_EQUIPMENT_Strike_Check;
  * evaluate defense only after Arx confirms the player is an actual strike target;
  * latch a successful defense across repeated action points/checks from that
    short NPC weapon strike, preventing damage leakage after a valid block/parry.

Equipment.cpp is patched byte-for-byte so its historical CRLF layout does not
turn a small semantic change into whole-file churn.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARX_GAME = ROOT / "ArxAndroid/src/core/ArxGame.cpp"
EQUIPMENT = ROOT / "ArxAndroid/src/game/Equipment.cpp"


def _eol_for(raw: bytes) -> bytes:
    return b"\r\n" if raw.count(b"\r\n") > raw.count(b"\n") // 2 else b"\n"


def replace_once(path: Path, old: str, new: str, marker: str) -> None:
    raw = path.read_bytes()
    if marker.encode("utf-8") in raw:
        return

    eol = _eol_for(raw)
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

    replace_once(
        ARX_GAME,
        "\tconst arxvr::VrWeaponSegment segment =\n"
        "\t\tg_vrWeaponSystem.buildContactSegment(profile, weaponPose);\n\n"
        "\tarxvr::VrImpactSample sample;",
        "\tconst arxvr::VrWeaponSegment segment =\n"
        "\t\tg_vrWeaponSystem.buildContactSegment(profile, weaponPose);\n\n"
        "\t// Keep the physical player weapon available to the incoming-melee\n"
        "\t// defense adapter even when this frame is not itself an outgoing hit.\n"
        "\tif(segment.valid && weaponPose.valid && allowHit) {\n"
        "\t\tarxvr::vrDefenseRuntime().publishDefenderWeapon(\n"
        "\t\t\ttracking.weaponToken, segment, timestampUs);\n"
        "\t} else {\n"
        "\t\tarxvr::vrDefenseRuntime().clearDefenderWeapon();\n"
        "\t}\n\n"
        "\tarxvr::VrImpactSample sample;",
        "publishDefenderWeapon(",
    )

    replace_once(
        ARX_GAME,
        "\tif(!equippedMeleeActive || rightDragging) {\n"
        "\t\tg_vrWeaponSystem.reset();\n"
        "\t}",
        "\tif(!equippedMeleeActive || rightDragging) {\n"
        "\t\tg_vrWeaponSystem.reset();\n"
        "\t\tarxvr::vrDefenseRuntime().clearDefenderWeapon();\n"
        "\t}",
        "arxvr::vrDefenseRuntime().clearDefenderWeapon();",
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

    old_live_sample = (
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
        "#endif"
    )
    new_sample_block = (
        "#if defined(ARXVR_ANDROID_BUILD)\n"
        "\t\tbool vrHaveIncomingWeapon = false;\n"
        "\t\tstd::uint64_t vrDefenseSourceToken = 0;\n"
        "\t\tstd::uint64_t vrDefenseStrikeToken = 0;\n"
        "\t\tstd::uint64_t vrDefenseActionToken = 0;\n"
        "\t\tstd::uint64_t vrDefenseTimestampUs = 0;\n"
        "\t\tarxvr::VrIncomingContact vrIncomingWeapon;\n"
        "\t\tif(io_source != entities.player()) {\n"
        "\t\t\tvrDefenseSourceToken = static_cast<std::uint64_t>(\n"
        "\t\t\t\treinterpret_cast<std::uintptr_t>(io_source));\n"
        "\t\t\tvrDefenseStrikeToken = static_cast<std::uint64_t>(\n"
        "\t\t\t\treinterpret_cast<std::uintptr_t>(io_weapon));\n"
        "\t\t\tvrDefenseActionToken = vrDefenseStrikeToken\n"
        "\t\t\t\t^ (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&action)) << 1u);\n"
        "\t\t\tvrDefenseTimestampUs = arxvr::vrDefenseNowMicros();\n"
        "\t\t\tvrHaveIncomingWeapon = arxvr::vrDefenseRuntime().sampleIncomingWeapon(\n"
        "\t\t\t\tvrDefenseSourceToken, vrDefenseActionToken,\n"
        "\t\t\t\t{ sphere.origin.x, sphere.origin.y, sphere.origin.z },\n"
        "\t\t\t\tvrDefenseTimestampUs, vrIncomingWeapon);\n"
        "\t\t}\n"
        "#endif"
    )

    raw_text = EQUIPMENT.read_text(encoding="utf-8", errors="strict")
    if "vrHaveIncomingWeapon = arxvr::vrDefenseRuntime().sampleIncomingWeapon" not in raw_text:
        if old_live_sample in raw_text:
            replace_once(
                EQUIPMENT,
                old_live_sample,
                new_sample_block,
                "vrHaveIncomingWeapon = arxvr::vrDefenseRuntime().sampleIncomingWeapon",
            )
        else:
            raise RuntimeError("Equipment.cpp no longer matches the known live shield adapter")

    old_target_block = (
        "#if defined(ARXVR_ANDROID_BUILD)\n"
        "\t\t\t\t\t// A physical shield block consumes only this NPC equipment strike.\n"
        "\t\t\t\t\t// Generic damage, spells and environmental sources continue through\n"
        "\t\t\t\t\t// their existing pipelines unchanged.\n"
        "\t\t\t\t\tif(target == entities.player() && vrShieldBlocked) {\n"
        "\t\t\t\t\t\tcontinue;\n"
        "\t\t\t\t\t}\n"
        "#endif"
    )
    new_target_block = (
        "#if defined(ARXVR_ANDROID_BUILD)\n"
        "\t\t\t\t\tif(target == entities.player() && io_source != entities.player()) {\n"
        "\t\t\t\t\t\tarxvr::VrDefenseRuntime & vrDefense = arxvr::vrDefenseRuntime();\n"
        "\t\t\t\t\t\tbool vrDefenseConsumed = vrDefense.defenseLatched(\n"
        "\t\t\t\t\t\t\tvrDefenseSourceToken, vrDefenseStrikeToken, vrDefenseTimestampUs);\n"
        "\t\t\t\t\t\tarxvr::VrDefenseEvent defenseEvent;\n"
        "\t\t\t\t\t\tbool vrFreshDefense = false;\n"
        "\t\t\t\t\t\tif(!vrDefenseConsumed && vrHaveIncomingWeapon) {\n"
        "\t\t\t\t\t\t\tvrDefenseConsumed = vrDefense.evaluatePlayerDefense(\n"
        "\t\t\t\t\t\t\t\tvrDefenseSourceToken, vrDefenseStrikeToken, vrIncomingWeapon,\n"
        "\t\t\t\t\t\t\t\tstd::max(rad, 1.f), defenseEvent);\n"
        "\t\t\t\t\t\t\tvrFreshDefense = vrDefenseConsumed;\n"
        "\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\tif(vrDefenseConsumed) {\n"
        "\t\t\t\t\t\t\tif(vrFreshDefense) {\n"
        "\t\t\t\t\t\t\t\tconst float strength = std::clamp(defenseEvent.relativeSpeed / 3000.f,\n"
        "\t\t\t\t\t\t\t\t                                  0.35f, 1.f);\n"
        "\t\t\t\t\t\t\t\tif(defenseEvent.type == arxvr::VrDefenseEventType::WeaponParry) {\n"
        "\t\t\t\t\t\t\t\t\tarxvrEmitHaptic(VrHapticHand::Right, VrHapticEvent::Parry, strength);\n"
        "\t\t\t\t\t\t\t\t\tarxvrEmitHaptic(VrHapticHand::Left, VrHapticEvent::Parry, strength);\n"
        "\t\t\t\t\t\t\t\t} else {\n"
        "\t\t\t\t\t\t\t\t\tarxvrEmitHaptic(VrHapticHand::Left, VrHapticEvent::Block, strength);\n"
        "\t\t\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t\t\tconst std::string_view attackerMaterial = io_weapon->weaponmaterial.empty()\n"
        "\t\t\t\t\t\t\t\t                                        ? std::string_view(\"metal\")\n"
        "\t\t\t\t\t\t\t\t                                        : std::string_view(io_weapon->weaponmaterial);\n"
        "\t\t\t\t\t\t\t\tconst Vec3f defensePosition(defenseEvent.position.x,\n"
        "\t\t\t\t\t\t\t\t                            defenseEvent.position.y,\n"
        "\t\t\t\t\t\t\t\t                            defenseEvent.position.z);\n"
        "\t\t\t\t\t\t\t\tARX_SOUND_PlayCollision(attackerMaterial, \"metal\", 1.f, 1.f,\n"
        "\t\t\t\t\t\t\t\t                        defensePosition, nullptr);\n"
        "\t\t\t\t\t\t\t}\n"
        "\t\t\t\t\t\t\tcontinue;\n"
        "\t\t\t\t\t\t}\n"
        "\t\t\t\t\t}\n"
        "#endif"
    )

    raw_text = EQUIPMENT.read_text(encoding="utf-8", errors="strict")
    if "vrDefense.evaluatePlayerDefense(" not in raw_text:
        if old_target_block in raw_text:
            replace_once(
                EQUIPMENT,
                old_target_block,
                new_target_block,
                "vrDefense.evaluatePlayerDefense(",
            )
        else:
            raise RuntimeError("Equipment.cpp no longer matches the known shield target gate")


def validate() -> None:
    game = ARX_GAME.read_text(encoding="utf-8", errors="strict")
    equipment = EQUIPMENT.read_text(encoding="utf-8", errors="strict")

    required_game = (
        '#include "vr/VrDefenseRuntime.h"',
        "arxvr::vrDefenseRuntime().resetSession();",
        "equippedShield && haveLeftHand && !leftDragging",
        "arxvr::vrDefenseRuntime().publishShield(",
        "arxvr::vrDefenseRuntime().publishDefenderWeapon(",
        "arxvr::vrDefenseRuntime().clearDefenderWeapon();",
    )
    required_equipment = (
        '#include "vr/VrDefenseRuntime.h"',
        "vrHaveIncomingWeapon = arxvr::vrDefenseRuntime().sampleIncomingWeapon",
        "vrDefense.defenseLatched(",
        "vrDefense.evaluatePlayerDefense(",
        "VrHapticEvent::Parry",
        "VrHapticEvent::Block",
        "ARX_SOUND_PlayCollision(attackerMaterial, \"metal\"",
    )
    forbidden_equipment = (
        "vrShieldBlocked = arxvr::vrDefenseRuntime().sampleShieldBlock",
        "if(target == entities.player() && vrShieldBlocked)",
    )
    missing = [item for item in required_game if item not in game]
    missing += [item for item in required_equipment if item not in equipment]
    forbidden = [item for item in forbidden_equipment if item in equipment]
    if missing or forbidden:
        raise RuntimeError(
            f"live defense integration validation failed: missing={missing}, forbidden={forbidden}"
        )


if __name__ == "__main__":
    patch_arx_game()
    patch_equipment()
    validate()
    print("ArxVR Phase 5 live shield/parry defense integration applied")
