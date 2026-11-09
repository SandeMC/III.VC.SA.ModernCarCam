# III.VC.SA.ModernCarCam

Universal vehicle camera ASI for GTA III, Vice City and San Andreas. The camera
engine is selected by `Profile` in `III.VC.SA.ModernCarCam.ini`; every extra
behaviour is an ini option layered on top of the authentic III / VC / SA camera.

## Build and verify

- Open `ModernCarCam.sln`, build `Release` / `Win32`. Output:
  `ModernCarCam/Release/III.VC.SA.ModernCarCam.asi` (the shipped copy is in
  `Release/` next to `III.VC.SA.ModernCarCam.ini`).
- There is no automated test suite. Verification is in-game on GTA III 1.0,
  Vice City 1.0 and San Andreas 1.0 US (the versions the addresses target), with
  the relevant profile and features enabled.
- The game executables are not in the repository. `gta-sa.exe` at the repo root
  and the old worktrees under `.kilo/worktrees/` are reverse-engineering inputs
  only; IDA working files are ignored by git.
- State exactly which profile, game and ini switches were used for a test. A
  camera change that works on one profile can break another engine.

## Source map

- `ModernCarCam/ModernCarCam.cpp` - `DllMain`, game detection, all hooks, shared
  engine helpers and RW wrappers.
- `ModernCarCam/CamVanilla.cpp` - authentic III/VC "on a string" and behind-boat
  cameras, plus the III/VC native-look hook chain.
- `ModernCarCam/CamSA.cpp` - SA-style follow camera used by SA, LCS, VCS and IV
  profiles (and by any profile with `CameraAnchoring = 0` on III/VC).
- `ModernCarCam/Profiles.cpp` - profile defaults and the camera distance/angle
  tables. `Settings.cpp` - ini parsing, per-car overrides. `Profiles.cpp` values
  are the baseline; `[Features]`/`[Multipliers]` override them.
- `ModernCarCam/ModernCarCam.h` - shared types, settings, collision/water helpers,
  `CameraFreeLookReturn`.

## Constraints and gotchas

- Compatibility matters: Widescreen Fix (this mod takes over its FOV and VCS
  shake), Manual Driveby mods, and fastman92 Limit Adjuster. Do not hard-code
  game array addresses that FLA can relocate (notably
  `CModelInfo::ms_modelInfoPtrs`; FLA's Apply ID Limit patch moves it).
  Read the live pointer from the game code instead.
- `Profile = Game` on SA uses the SA follow camera; on III/VC it uses the
  authentic vanilla engine. LCS/VCS/IV use `CamSA.cpp` even on III/VC because
  `CameraAnchoring` is 0 for them.
- The LCS/VCS camera tables intentionally carry LCS limits. Limits that are
  correct for the follow camera (e.g. the LCS boat max pitch) are not
  automatically the right limits for the mod's free-look; check both paths.
- `MouseFreeLook`, `SmoothSideView`, `DisableSideView`, `CameraCollisionProps`,
  `KeepCameraOverWater`, `ReverseCamera`, `CameraResetOrder`,
  `CameraResetFromStop` and `CameraReset*Time` are the existing knobs. Document
  any new knob in both `III.VC.SA.ModernCarCam.ini` and `README.md`.
- Do not push, publish, tag or open a pull request. Commit local checkpoints
  only.

## Current focus

Feedback fixes landed in 11/2026 and built successfully; in-game verification is
still pending (no game install is used by the agent):

- model-info array is resolved live (SA signature scan; III/VC read the
  FLA-patched `AddSimpleModel` immediates), with safe fallbacks;
- free look latches its radius instead of snapping to `CA_MAX_DISTANCE`;
- free look holds while stationary; a look taken at a standstill returns at a
  speed-scaled pace when pulling away, while a look taken during driving
  returns at the normal pace (all engines: SA follow, vanilla car, both boat
  cameras);
- LCS/VCS boat free-look uses the SA pitch limit, not the LCS follow limit;
- free-look collision checks map objects with a small-prop filter (III/VC by
  model name, SA by generated name-hash list plus a size fallback), always runs,
  and has a conservative ground clamp; own-boat hull collision pushes the camera
  out; the near-plane sphere ignores the player's own vehicle during free look;
- traffic pass-by wobble is for cars/motorcycles only; `IsBicycleVehicle`
  explicitly excludes the BMX/MTB ids and names even if a mod types one as a
  motorcycle;
- `CameraResetOrder` remains the only reset-order knob; `CameraResetFromStop`
  tunes only the pull-away rate, while normal driving returns are unaffected.

## Log

Progress and findings: `.agents/progress.md`.
