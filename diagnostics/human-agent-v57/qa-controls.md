# v58 QA input rehearsal

Purpose: let a tester choose ordinary controller inputs after observing the
rendered game. This is synthetic input, not physical headset testing.

APK: `ArxVR-Android/app/build/outputs/apk/debug/app-debug-v58-qa.apk`

SHA-256: `463820D7A318F0FC7EBACF6CDAA6F5984B4846242488E6703B7054616995F6BF`

The packaging check hashes all entries: the engine (`libarx.so`) and resources
are unchanged from v57. Only the OpenXR host library and signing metadata differ.
The normal app path is unchanged when `debug.arxvr.qa_input` is absent or `0`.

## Protocol

The space-separated Android property `debug.arxvr.qa_input` contains:

```
sequence duration moveX moveY yaw pitch height handX handY handZ handYaw handPitch buttons
```

- Sequence: positive, changed for each new pulse.
- Duration: 0 to 2 seconds. Movement and buttons expire automatically.
- Move axes: -1 to 1, matching a physical stick's range.
- Head yaw: -180 to 180 degrees; positive turns left. Pitch positive looks up.
- Height: -1 to 0.1 metres relative to the captured base head position.
- Right hand: metres relative to head position and upright head yaw;
  +X right, +Y up, -Z forward. Distance from head is limited to 1 metre.
- Hand angles: relative to upright head yaw; positive pitch points up.
- Button mask: right trigger 256, right grip 512, A 2048, B 4096,
  left stick click/run 4, left Y/magic mode 16.

The pose remains steady when a pulse expires. Set the property to `0` at the
end of a test; physical tracking is restored with a head-reference reset.
Malformed/out-of-range commands are rejected. No world coordinates, entity IDs,
damage, script events, inventory operations or level loading are in the protocol.

Examples (must be quoted as a single value on the Android shell):

```
1 0 0 0 0 0 0 .2 -.2 -.4 0 0 0
2 .4 0 1 0 0 0 .2 -.2 -.4 0 0 0
3 .15 0 0 0 0 0 .2 -.2 -.4 0 0 512
```

These mean neutral pose, 0.4 seconds forward, and a short right-grip press.
`auto_human` and `auto_traverse` must be disabled while using the bridge.
`auto_play` bypasses new-game setup and injects a brief initial walk; if used,
that setup must be explicitly excluded from claims about menu usability.

## Verification performed before handoff to tester

- Host native build completed successfully.
- C++ parser test passed: valid grip input; rejection of prolonged movement,
  nonphysical stick amplitude, NaN, unreachable pose, unknown buttons, malformed
  text; invalid commands leave the output unchanged.
- APK zip alignment and Android signature verification passed.
- Certificate matches the installed v57 certificate.
- Archive comparison confirmed unchanged game engine and resources.

Remaining runtime checks belong in the tester's report. None of the checks above
asserts that gameplay, physical tracking, grabbing or level completion works.
