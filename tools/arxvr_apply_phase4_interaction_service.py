#!/usr/bin/env python3
"""Promote generated Phase 4 combat integration to VrInteractionSystem.

This deterministic second-stage transform runs after
arxvr_apply_phase4_live_integration_v2.py. It keeps the gameplay target/damage
pipeline unchanged while replacing the two ArxGame-owned VrHandState globals
with the reusable two-hand interaction service and semantic VrImpactEvent.
"""
from pathlib import Path

PATH = Path("ArxAndroid/src/core/ArxGame.cpp")
text = PATH.read_bytes().decode("utf-8")
original = text


def replace_exact(old: str, new: str, expected: int, label: str) -> None:
    global text
    count = text.count(old)
    if count != expected:
        raise RuntimeError(f"{label}: expected {expected} literal match(es), found {count}")
    text = text.replace(old, new)


replace_exact(
    '#include "vr/VrHandState.h"\n',
    '#include "vr/VrInteractionSystem.h"\n',
    1,
    "interaction service include",
)

replace_exact(
    "static arxvr::VrHandState g_vrRightHandImpact;\n"
    "static arxvr::VrHandState g_vrLeftHandImpact;\n",
    "static arxvr::VrInteractionSystem g_vrInteractions;\n",
    1,
    "per-hand globals",
)

replace_exact(
    "\tarxvr::VrHandState & state = rightHand ? g_vrRightHandImpact : g_vrLeftHandImpact;\n",
    "\tconst arxvr::VrHand hand = rightHand ? arxvr::VrHand::Right : arxvr::VrHand::Left;\n",
    2,
    "per-function hand routing",
)

replace_exact(
    "\tconst arxvr::VrImpactGateStatus status = state.update(sample);\n",
    "\tconst arxvr::VrImpactGateStatus status = g_vrInteractions.updateHand(hand, sample);\n",
    2,
    "interaction service update",
)

replace_exact(
    "\tarxvr::VrQualifiedImpact impact;\n"
    "\tif(!state.consumeQualifiedImpact(impact)) {\n",
    "\tarxvr::VrImpactEvent impact;\n"
    "\tif(!g_vrInteractions.consumeImpact(hand, impact)) {\n",
    2,
    "semantic impact consumption",
)

replace_exact(
    "\t\tg_vrRightHandImpact.resetSession();\n"
    "\t\tg_vrLeftHandImpact.resetSession();\n",
    "\t\tg_vrInteractions.resetSession();\n",
    1,
    "session reset routing",
)

for required in (
    '#include "vr/VrInteractionSystem.h"',
    "static arxvr::VrInteractionSystem g_vrInteractions;",
    "g_vrInteractions.updateHand(hand, sample)",
    "g_vrInteractions.consumeImpact(hand, impact)",
    "arxvr::VrImpactEvent impact;",
    "g_vrInteractions.resetSession();",
):
    if required not in text:
        raise RuntimeError(f"required interaction-service token missing: {required}")

for forbidden in (
    '#include "vr/VrHandState.h"',
    "g_vrRightHandImpact",
    "g_vrLeftHandImpact",
    "state.consumeQualifiedImpact",
    "state.update(sample)",
):
    if forbidden in text:
        raise RuntimeError(f"legacy per-hand integration token still present: {forbidden}")

if text == original:
    raise RuntimeError("interaction-service transform produced no changes")

PATH.write_bytes(text.encode("utf-8"))
print("Promoted Phase 4 live combat integration to VrInteractionSystem in", PATH)
