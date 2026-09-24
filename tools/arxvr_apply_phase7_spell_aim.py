#!/usr/bin/env python3
"""Guarded Phase 7 casting-hand aim integration.

Publishes a fresh right-hand world-space spell ray from ArxGame and consumes it
only for player Magic Missile launch. NPC casting and all legacy spell
semantics remain untouched. The patch preserves each source file's existing
line endings and refuses ambiguous anchors.
"""

from pathlib import Path

GAME = Path("ArxAndroid/src/core/ArxGame.cpp")
SPELL = Path("ArxAndroid/src/game/magic/spells/SpellsLvl01.cpp")


def eol_for(data: bytes) -> bytes:
    return b"\r\n" if b"\r\n" in data else b"\n"


def replace_once(data: bytes, old_lf: str, new_lf: str, label: str) -> bytes:
    eol = eol_for(data)
    old = old_lf.replace("\n", eol.decode()).encode()
    new = new_lf.replace("\n", eol.decode()).encode()
    count = data.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return data.replace(old, new, 1)


def patch_game() -> bool:
    data = GAME.read_bytes()
    if b'#include "vr/VrSpellAim.h"' in data:
        required = [b"vrSpellAimService().update", b"VrSpellAimSample"]
        if not all(marker in data for marker in required):
            raise SystemExit("ArxGame has partial spell-aim integration")
        return False

    data = replace_once(
        data,
        '#include "vr/VrRuneRuntime.h"\n#include "game/magic/SpellRecognition.h"',
        '#include "vr/VrRuneRuntime.h"\n#include "vr/VrSpellAim.h"\n#include "game/magic/SpellRecognition.h"',
        "spell aim include",
    )
    data = replace_once(
        data,
        '\t// Checks Magic Flares Drawing\n\tif(!player.m_paralysed) {',
        '''\t// Keep directional spell launches frame-local. Tracking loss or a
\t// paralysed player must never leave a stale controller ray available.
\tarxvr::vrSpellAimService().clear();

\t// Checks Magic Flares Drawing
\tif(!player.m_paralysed) {''',
        "frame-local spell aim clear",
    )
    data = replace_once(
        data,
        '''\t\t(void)vrRuneHandDirection;
\t\t(void)vrRuneHandOrientation;

\t\tarxvr::VrRuneSample vrRuneSample;''',
        '''\t\tarxvr::VrSpellAimSample vrSpellAimSample;
\t\tvrSpellAimSample.origin = {
\t\t\tvrRuneHandPosition.x, vrRuneHandPosition.y, vrRuneHandPosition.z
\t\t};
\t\tvrSpellAimSample.forward = {
\t\t\tvrRuneHandDirection.x, vrRuneHandDirection.y, vrRuneHandDirection.z
\t\t};
\t\tvrSpellAimSample.trackingValid = vrRuneTracking;
\t\tarxvr::vrSpellAimService().update(vrSpellAimSample);
\t\t(void)vrRuneHandOrientation;

\t\tarxvr::VrRuneSample vrRuneSample;''',
        "publish casting hand ray",
    )
    GAME.write_bytes(data)
    return True


def patch_spell() -> bool:
    data = SPELL.read_bytes()
    if b'#include "vr/VrSpellAim.h"' in data:
        required = [b"vrSpellAimService().ray()", b"vrPhysicalLaunchOffset"]
        if not all(marker in data for marker in required):
            raise SystemExit("SpellsLvl01 has partial spell-aim integration")
        return False

    data = replace_once(
        data,
        '#include "util/Range.h"\n',
        '''#include "util/Range.h"

#if defined(ARXVR_ANDROID_BUILD)
#include "vr/VrSpellAim.h"
#endif
''',
        "spell aim include in level-one spells",
    )

    old_launch = '''\tVec3f startPos = m_hand_pos;
\tfloat pitch, yaw;
\tif(m_caster == EntityHandle_Player) {
\t\tpitch = player.angle.getPitch();
\t\tyaw = player.angle.getYaw();
\t\tif(!m_hand_group) {
\t\t\tstartPos = player.pos + angleToVectorXZ(yaw);
\t\t}
\t} else {
\t\tpitch = 0.f;
\t\tyaw = entities[m_caster]->angle.getYaw();
\t\tif(!m_hand_group) {
\t\t\tstartPos = entities[m_caster]->pos;
\t\t}
\t}
\t
\tstartPos += angleToVector(Anglef(pitch, yaw, 0.f)) * 60.f;
'''
    new_launch = '''\tVec3f startPos = m_hand_pos;
\tfloat pitch, yaw;
#if defined(ARXVR_ANDROID_BUILD)
\tbool vrHandAimed = false;
\tVec3f vrHandDirection(0.f);
\tconstexpr float vrPhysicalLaunchOffset = 18.f;
#endif
\tif(m_caster == EntityHandle_Player) {
\t\tpitch = player.angle.getPitch();
\t\tyaw = player.angle.getYaw();
\t\tif(!m_hand_group) {
\t\t\tstartPos = player.pos + angleToVectorXZ(yaw);
\t\t}
#if defined(ARXVR_ANDROID_BUILD)
\t\tconst arxvr::VrSpellAimRay vrAim = arxvr::vrSpellAimService().ray();
\t\tif(vrAim.valid) {
\t\t\tstartPos = Vec3f(vrAim.origin.x, vrAim.origin.y, vrAim.origin.z);
\t\t\tvrHandDirection = Vec3f(vrAim.direction.x, vrAim.direction.y,
\t\t\t                            vrAim.direction.z);
\t\t\tconst Anglef handAngles = unitVectorToAngle(vrHandDirection);
\t\t\tpitch = handAngles.getPitch();
\t\t\tyaw = handAngles.getYaw();
\t\t\tvrHandAimed = true;
\t\t}
#endif
\t} else {
\t\tpitch = 0.f;
\t\tyaw = entities[m_caster]->angle.getYaw();
\t\tif(!m_hand_group) {
\t\t\tstartPos = entities[m_caster]->pos;
\t\t}
\t}
\t
#if defined(ARXVR_ANDROID_BUILD)
\tif(vrHandAimed) {
\t\tstartPos += vrHandDirection * vrPhysicalLaunchOffset;
\t} else
#endif
\t{
\t\tstartPos += angleToVector(Anglef(pitch, yaw, 0.f)) * 60.f;
\t}
'''
    data = replace_once(data, old_launch, new_launch, "Magic Missile physical launch ray")
    SPELL.write_bytes(data)
    return True


def main() -> None:
    game_changed = patch_game()
    spell_changed = patch_spell()
    print(
        "Applied Phase 7 casting-hand aim integration: "
        f"ArxGame={'changed' if game_changed else 'current'}, "
        f"SpellsLvl01={'changed' if spell_changed else 'current'}"
    )


if __name__ == "__main__":
    main()
