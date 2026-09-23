# v58 QA input attempt — 2026-09-23

Result: BLOCKED_ENVIRONMENT before playable traversal. The synthetic input bridge and normal menu click path worked; sustained VR gameplay, pickups, escape, and next-location traversal were not verified.

Device: PICO PA921CMGK6120092G only. Installed with `adb -s ... install -r` after the parent explicitly authorized the v58 QA update. APK SHA256 was checked before installation: `463820D7A318F0FC7EBACF6CDAA6F5984B4846242488E6703B7054616995F6BF`. This is a QA host build; these observations must not be attributed to the unmodified v57 APK.

## Control method

`qa-command.ps1` sends opt-in `debug.arxvr.qa_input` commands. The bridge replaces head and controller poses with synthetic poses and submits normalized stick/button input to the existing tracking bridge. This is not physical headset/controller testing. No route diagnostics, direct entity movement, forced story events, god mode, forced level load, or combat shortcuts were used.

`auto_play=0`, `auto_human=0`, and `skip_intro=0` stayed at their baseline values during the v58 attempt. `auto_traverse` remained unset. A normal new-game menu flow was used.

## Achieved and evidence

- `30-v58-neutral-review.jpg`: neutral QA command accepted; initial cinematic visible as a flat panel in both eyes. `ARXVR_QA enabled: SYNTHETIC head/controllers; no world-state shortcuts` logged.
- Commands 2–5 exercised grip, B, trigger, and A. Down/up logs confirm input changes and automatic release. For command 2, right grip changed to 1 at 16:26:47.363 and back to 0 at 16:26:47.627 without a separate release command. This validates button TTL, not movement TTL.
- `34-v58-intro-a-review.jpg`: main menu visible in both eyes. The preceding introductory video progressed between commands; its termination cannot be uniquely attributed to A from these snapshots alone.
- Command 6 aimed the synthetic hand at **Новая игра** and clicked with lower grip. `35-v58-new-game-review.jpg` shows the character creation book.
- Command 7 clicked **Быстрое создание**, command 8 clicked **Готово**. `36-v58-character-generated.png` and `37-v58-character-done.png` record this transition. `v58-app-events.txt` confirms level1 loading at 16:28:20.451 and completion at 16:28:24.622. No auto-play setup was used.
- The initial in-game frame was black with hands/UI (`37`); a later capture (`38`) was black. These are not a visual gameplay pass.
- At 16:28:56.907 OpenXR changed FOCUSED→VISIBLE. `39-v58-skip-cinematic-review.jpg` shows PICO's **Слишком темно** dialog in both eyes, covering the game completely.
- A single Android BACK and a single DPAD_CENTER attempt did not dismiss the dialog: `40-v58-back-lowlight-review.jpg` and `41-v58-lowlight-confirm-review.jpg` show the same warning. The **Выключить отслеживание** button was never selected. No guardian/security settings were changed.
- At 16:29:24.938–939 the OpenXR session changed VISIBLE→SYNCHRONIZED→STOPPING→IDLE.
- Commands 11/12 requested 0.4-second normalized forward/backward pulses while the scene was occluded. Neither appears in the bridge's accepted-command log, and there are no locomotion events. They were not consumed by the stopped render loop. `42-v58-forward-occluded.png` and visually inspected `43-v58-backward-occluded-review.jpg` still show only the low-light dialog. These attempts prove no gameplay movement.

## Criteria not completed

| Requested criterion | Result |
|---|---|
| Fixed-facing forward/backward movement | Blocked before accepted movement commands |
| Body facing / backwards regression | Not proven |
| Bone, skull, or stone grab/release | Not tested; no visible playable scene available |
| Exit jail cell | Not reached |
| Lever | Not reached |
| Goblin / chair interaction | Not reached |
| Next location | Not reached |
| Physical tracking and controller ergonomics | Not tested |

## Artifacts and cleanup

- `v58-app-events.txt`: accepted command history, button transitions, level load, OpenXR state changes.
- `v58-gameplay-attempt.mp4`: 33,202,147-byte compositor recording of the attempt, including the blocking state. It was saved; no claim of exhaustive video-frame inspection is made.
- The numbered `30`–`45` PNGs contain both original compositor eyes. `-review.jpg` copies are for visual inspection only.
- `45-v58-final-props.txt`: `qa_input=[]`, `force_menu=[]`, `auto_human=0`, `auto_play=0`, `skip_intro=0`. Empty means disabled; these two initially absent properties cannot be removed through setprop. The pending stick request was replaced with 0 and then empty before ending. Recording was stopped and pulled. No autopilot remains armed.
- No save/data deletion, uninstall, or changes to tracking/guardian/power settings. The current game process was left intact with the runtime stopped by the system warning.

## Next action

Restore real tracking conditions: sufficient room lighting, unobstructed cameras, and a person wearing/positioning the headset. Then resume this same manual QA input path, beginning with neutral → fixed-yaw short forward pulse → neutral → backward pulse, with fresh compositor images and position/yaw logs. After that, test actual hand contact with the bone/skull/stone and the cell escape path. The environment blocker does not justify a game-mechanics patch. The v58 bridge solves the lack of remote normal-input control, but it cannot establish physical VR quality by itself.
