# Hand orientation / menu anchor — v60, 2026-09-23

## Installed result

PICO `PA921CMGK6120092G`, package `com.arxvr.android`.
Installed with `adb install -r`; no data clear, uninstall, save deletion, or EZPico.

APK: `ArxVR-Android/app/build/outputs/apk/debug/app-debug-v60-handspace-fullbuild.apk`
SHA256: `0D5B82C68CC14672BC86D5BC3EBB447566E148DED02B095AF85C5D85256B3C59`.
The device's installed `base.apk` hash was checked and matches. All engine,
resources and other library entries match v58; only the OpenXR host library and
signing metadata changed. Original APKs are preserved.

## Confirmed cause and changes

1. `arxvrGetHandWorldPose` exports a rotation in Arx-local coordinates: +Z aim,
   -Y up. `Hand::setModel` corrects the FBX into OpenXR-local coordinates: -Z
   aim, +Y up. `applyGameHandPose` used the exported rotation without converting
   the mesh's local basis. Both hands therefore pointed backwards and upside
   down relative to their controller's gameplay ray. Inserted the missing
   (1,-1,-1) basis conversion before the existing FBX correction. Engine object
   poses, controls, asset corrections, curls and scale were not changed.
2. Panel placement used optional eye-gaze view data, potentially stale when
   eye tracking was inactive, or a guessed IPD correction. It now uses the fresh
   centre-head sample supplied to the engine. Returning focus while a 2D menu
   is active invalidates its anchor once and resets its pointer reference.
   Open menus remain world-fixed; gameplay focus return does not reset body
   orientation. Focus loss is remembered from the OpenXR event even when no
   unfocused frame is rendered.

## Local regression evidence

- `tools/test-arxvr-hand-space.ps1`: executes production GLM transform statements
  and the real `Hand::setModel` body with a non-rendering matrix sink. Loaded-FBX
  wrist/middle-finger bind landmarks are the fixtures. Before the fix, both
  hands failed four orientations: finger/aim dot = -0.997563, up dot = -1.
  After the fix, all eight cases pass: +0.997563 and +1 respectively. Position
  remains unchanged and the native OpenXR path still points along -Z.
- `tools/test-arxvr-menu-anchor.ps1`: executes the production anchor and head
  callbacks. Observed failures for stale centre, focus return and current eye
  height before the patch. Seven assertions pass afterward, including focus
  recovery without an intervening rendered frame, upright placement and no
  gameplay recenter on focus return.
- QA command parser executable: PASS.
- Existing reference-space source-wiring check: PASS. This is a textual wiring
  check, not proof of physical body/camera correctness.
- Full NDK host build and APK alignment/signature checks pass. There is no
  claim that the game's entire mechanics or all project tests were verified.

## Build failure caught on the device

The incremental v59 build recompiled application.cpp/openxr_program.cpp but
retained old graphicsplugin objects after the IApplication vtable changed.
It installed successfully yet repeatedly dispatched a render call to the wrong
virtual method: screenshot `47-v59-start.png` was entirely black and the focus
reset log repeated every frame. v59 is a FAILED build; do not reinstall it.

Forced all host translation units to rebuild with `ndk-build -B ... openxr_demos`,
then ran the default NDK target to install/strip the rebuilt .so into the lib
output consumed by packaging. v60 restores rendering. Future interface changes
must use a full host rebuild; incremental build success is not sufficient.

## Device observations (reviewed screenshots)

Screenshots and input/focus logs are in `diagnostics/human-agent-v57` despite the
directory name. The numbered files below belong to **v60**, not v57.

- `48`–`56`: both-eye intro, normal menu -> New Game -> Quick creation -> Done.
  `auto_play`, `auto_human`, and `skip_intro` remained 0. No forced level load.
- `57-v60-wait-intro`: visible jail gameplay and both hands after the scripted
  intro finished. No low-light overlay in this captured frame.
- `58-v60-extended-open`: right hand extended from z=-0.55 to -0.80 m. No held
  object; this is NOT evidence of item attachment.
- `59-v60-both-fists`: BLACK frame, not counted as visual success.
- `60-v60-both-fists-visible`: both hands visibly curl during a 2-second lower-
  grip pulse; fingers differ visibly from the open frames.
- `61-v60-forward`: normalized stick (0,+1), unchanged yaw, 0.4-second pulse.
  Player (8540,2935.14,8460) -> (8540.09,2921.96,8546.62); gate grows in view.
- `62-v60-backward`: (0,-1), same yaw, 0.4-second pulse.
  Player (8540.09,2926.23,8557.86) -> (8539.98,2916.77,8468.84); gate recedes.
  The vertical component changes on the uneven floor; this is not a crouch test.
- `63`: pause menu opens. `64`: aimed lower-grip click opens Load/Save submenu.
  No save slot was loaded or overwritten.
- `65`: changing synthetic head yaw to +20 degrees leaves the open panel fixed.
  Actual compositor views remained the runtime views, so this is not proof of
  real physical head motion quality.
- `66`: Android Home/return produced a real focus cycle and one re-anchor at
  the changed synthetic yaw. The system dashboard subsequently covered part of
  the panel; this is not an unobstructed gameplay pass.
- `67`: QA disabled, physical tracking restored log. System dashboard remains.
- App restarted with diagnostics off to restore an unobstructed application.
  `68-v60-final-no-qa`: both-eye intro visible, QA/force_menu empty, auto_human,
  auto_play and skip_intro 0. No pending input or recording remains armed.

`v60-hands-check.mp4` contains a 45-second compositor recording, 47,374,572 bytes.
Saved as evidence; no claim of exhaustive frame-by-frame review is made.

## Limits / remaining work

All remote poses were explicitly SYNTHETIC normal tracking/input, not physical
controller movement. Runtime logs still reported tracking_6dof_stopped=true.
Screenshots prove the rendered test poses, not controller-to-real-hand ergonomic
alignment. The full level, cell escape, bone/skull/chair attachment, goblin
combat, throws, physical crouch and physical recenter/backwards regression were
not validated by this pass. Performance logs around this scene show summed
legacy eye CPU time about 31–35 ms; no claim of 47–58 or 75 FPS is made.
