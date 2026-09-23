# Independent v57 observation — 2026-09-23

Result: LIMITED SYNTHETIC SMOKE, NOT HUMAN GAMEPLAY PASS. No exit from the starting jail, object pickup, lever, combat, or next-location transition was achieved.

Device: PICO serial PA921CMGK6120092G, package com.arxvr.android. Installed APK SHA256 before v58 installation: `91b0bdcb32dca878b59e35103cbed10402d5a7d7a12330edce65abb8236169f8`. Package metadata is versionCode 1 / versionName 0.1.0-dev, so the v57 label is the build label supplied by the main task, not an Android version code.

## What was actually observed

- `00-initial.png` / `02-wake-input.png`: both compositor eyes show the Arx main menu, no system dialog. The device logs already report `tracking_6dof_stopped: true`, camera brightness around 3.8. This does not establish working physical 6DOF.
- `01-adb-input.png`: fully black after headset sleep, and Android reports no focused window. A wake key (224) restores the menu.
- ADB BUTTON_A (96) and W (51) reach `main.cpp:101` as down/up events, but the handler only logs them and returns 0. They do not drive the OpenXR action set. `03-available-menu-events.txt` includes the input logs. The main menu remains unchanged.
- `04-menu-button.png`: the dedicated `force_menu` property generates RIGHT_B down/up (`buttons=4096` then 0), but there is no loaded game to resume from this menu. The property was restored to empty afterward.
- One explicitly synthetic canned run used only `auto_play`, `auto_human`, and `skip_intro`, never `auto_traverse`. It starts a diagnostic new game and supplies a 180-tracking-frame forward stick plus scripted head/hand poses.
- `canned-app-events.txt`: level1 loaded; forward stick `(0,1)` moved the player from `(8540,2935.14,8460)` to `(8790.4,2935.14,8897.66)`, displacement `504.229`. The yaw sweep overlaps the movement, so this does not isolate forward/body-facing correctness.
- Synthetic head-height changes produced crouch active/inactive logs. The log phrase `physical crouch` describes the game input handler; no person physically crouched during this test.
- Scripted squeeze/trigger events were emitted. There is no successful physical-grip object event and no evidence that a bone, skull, or stone was picked up.

## Visually reviewed evidence

All listed screenshots are original 4320×2160 compositor captures containing both eyes. Smaller `-review.jpg` files were generated solely for inspection after the image reader failed to decode several large PNG transfers; the originals remain available.

| Screenshot | Visual interpretation |
|---|---|
| `10-canned-settle-review.jpg` | Full VR jail scene, barred doorway, stone floor/walls in both eyes. Right eye visibly darker in this one frame; no conclusion about cause. |
| `11-canned-yaw90-review.jpg` | Full VR wall close-up with both synthetic hands. Filename follows an observed log marker; it is not proof of the exact yaw at screencap time. |
| `12-canned-yaw180.png`, `13-canned-yaw270-review.jpg`, `14-canned-yaw360-review.jpg` | The actual captured view is a small flat panel with the opening prisoner dialogue, not full stereo yaw coverage. Sequential screencap/log collection ran behind the one-second yaw checkpoints. These images must not be counted as a 360-degree VR pass. |
| `15-canned-head6dof-review.jpg` | Full VR view of bars/ceiling/prisoner, synthetic hands raised in view; no system overlay in this fresh run. |
| `16-canned-hands-review.jpg` / `17-canned-complete-review.jpg` | Full VR jail, animated hand pose changes; no held object. Script completion only. |
| `19-restored-review.jpg` | Restart loading rings immediately after diagnostics were disabled; not proof of a finished menu transition. |

`canned-observation.mp4` (89,864,062 bytes) records the compositor run. Video was saved; conclusions above rely on the inspected stills and logs, not a claim that every video frame was inspected.

## Remaining criteria

| Criterion | Result |
|---|---|
| Physical head/controller tracking | Not verified; runtime reports stopped 6DOF |
| Independent forward versus backward at fixed facing | Not tested on v57; forward-only canned motion overlaps yaw |
| Backwards/body-facing regression fixed | NOT PROVEN; a handled reference-space event is insufficient |
| Bone / skull / stone pickup and release | Not achieved |
| Exit cell / jail lever | Not achieved |
| Goblin / chair | Not reached |
| Next location | Not reached |

## Cleanup and recommendation

Baseline properties were `auto_human=0`, `auto_play=0`, `skip_intro=0`; they were restored and the app restarted to discard latched canned controls. `force_menu` was initially absent and was left empty (Android keeps an empty property entry). No system tracking/guardian settings, saves, or game data were deleted or cleared. v57 was not reinstalled during its baseline. Later the parent explicitly authorized installing v58 QA; that separate result is in `report-v58-qa.md`.

The useful next test is an explicit normal-range input bridge followed by a fixed-facing forward/backward comparison and object interactions while the headset compositor remains visible. The existing route's forced script/combat/collision shortcuts cannot serve as human gameplay evidence; see the main agent's `verification-audit.md`.
