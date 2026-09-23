# Audit of existing ArxVR v57 verification

This report audits the evidence and scripts. It is not a new gameplay pass.

## Earlier v57 verdict is not supported in full

`orientation-v57-green/summary.json` records `Completed=true` and `DarkEyes=0`.
The images show that these fields do not establish a usable VR session:

- `head-6dof.png`, `physical-crouch.png` and `pitch-down-55.png` contain a PICO
  insufficient-light tracking dialog on a black background. Their per-eye
  visible-pixel ratios are about 3.3%. The checker only rejects ratios below 2%.
- `yaw-180.png` and `yaw-270.png` are obstructed by PICO system settings.
- `yaw-090.png` does show the rendered jail in both eyes. This one observation
  cannot prove the rest of the sequence or the absence of black frames.
- The log confirms an OpenXR reference-space callback was scheduled and applied.
  It does not by itself prove correct player-facing direction after recenter.
- Body-yaw progression is measured during synthetic head input, not a physical
  controller/headset session.

## Old traversal script bypasses player actions

`ArxAndroid/src/core/ArxGame.cpp` contains these diagnostic shortcuts:

| Location | Shortcut | What it cannot prove |
| --- | --- | --- |
| `driveVrPlayerTo` | Writes player yaw directly and injects stick magnitude 2.75 | Ordinary stick range and real head/body alignment |
| Grip-only target selection | Teleports bone/skull/chair to the camera | Finding and reaching the object in its real location |
| Chair-combat diagnostic | Teleports the goblin into the swing path | Approaching and fighting a naturally positioned enemy |
| `UseBars` | Sends the `STONE` story event directly | The real stone action opens the bars |
| `UseJailLever` | Sends `SM_ACTION` and gate `open` directly | A controller can target and activate the lever |
| `DefeatGuard` | Calls `ARX_DAMAGES_ForceDeath` | Fists or held objects can defeat the goblin |
| `UseLever` | Sends `SM_ACTION` directly | The second lever is usable through normal input |
| `BreakTrapdoor` | Sends `SM_HIT` directly | Player attacks can break the trapdoor |
| `FallThroughTrapdoor` | Disables collision and forces downward velocity | Natural physics lets the player enter the next area |

The old script can still be useful as an assisted integration test, but it must
not be reported as human-like level completion.

## Evidence required for the delegated check

1. Confirm PICO tracking and unobstructed game output before accepting actions.
2. Label synthetic pose/stick input explicitly; do not call it physical testing.
3. Record ordinary input, the visible response, and the resulting game state.
4. Do not use forced deaths, story events, object teleports or collision changes
   to claim a successful route.
5. Inspect both-eye images. System overlays and unreadable frames are blocked
   evidence, regardless of average brightness.
6. Claim a next-location pass only after the normal route reaches it and the
   new location is visibly loaded. Otherwise state the last verified step.

The delegated tester owns headset controls during this run. The main agent is
auditing local scripts and evidence only, to avoid simultaneous control.
