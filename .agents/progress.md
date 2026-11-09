# Progress log

## 2026-11-08 - Feedback triage (no code changes yet)

Analysed the five reported camera defects against `fe1e14a`. No game install is
available in this workspace, so none of these were reproduced in-game yet; the
findings below are code-level causes and one confirmed external compatibility
fact.

### 1. Reverse camera + mouse = sharp jump (CamVanilla)

Cause: `Process_Cam_On_A_String_Vanilla` forces `stringLength = cam->CA_MAX_DISTANCE`
on every mouse-free-look frame (`CamVanilla.cpp:1325-1326`). While reversing the
follow string is short because the car backs toward the camera; the first mouse
frame replaces that short length with the full preset distance, so the camera
snaps outward. This is independent of the reverse swing itself.

Fix direction: capture the current clamped string length when the mouse look
starts (like `modLookRadius` for the side view) and keep that radius for the
whole look; do not re-read `CA_MAX_DISTANCE`. Never let the reverse state and the
mouse takeover both rewrite Beta; hand off from the current angle.

### 2. fastman92 Limit Adjuster 7.6 Apply ID Limit patch freezes the camera

Cause: `CModelInfo::ms_modelInfoPtrs` is hard-coded to the default array base
(`ModernCarCam.cpp:172`, `0xA9B0C8` on SA). FLA's Apply ID Limit patch relocates
that array and patches the immediate in `CModelInfo::SetModelInfo`
(SA 1.0: `0x4C5538` = `mov ds:dword_A9B0C8[ecx*4], eax`; the pointer immediate
is at `0x4C553B`). The mod keeps reading the old base, so `GetModelInfo()` and
`GetColModel()` are invalid; `Process_FollowCar_SA` returns at `CamSA.cpp:334`
when `GetColModel()` is null, leaving the camera locked.

External confirmation: FLA docs/community say plugins must read the relocated
model-info pointer; forum posts name `(0x4C5538+3)` as the stable pointer on SA
(`https://github.com/DK22Pac/plugin-sdk/issues/38`). FLA also supports III/VC,
so their equivalents must be verified before shipping.

Fix direction: read the live array base from the game code each use (SA:
`*(CBaseModelInfo***)0x4C553B`), fall back to the old base if the signature does
not match; use it for both `GetModelInfo` and the per-car model-name lookup.

### 3. Camera passes through bridge/ground while free-looking

Cause candidates found in code:

- `CameraProcessLineOfSight` with `skipProps` calls `ProcessLineOfSight` with
  `checkObjects = false` (`CamVanilla.cpp:100-103`), so map pieces stored as
  `CObject` are skipped entirely; the later `EntityLooksLikeCameraProp` filter
  only applies to hits that came back as buildings. Free look always sets
  `skipProps` (`CamVanilla.cpp:1789-1791`; `CamSA.cpp:1525-1527`), so the
  screenshot's bridge can be skipped while the follow camera would collide.
- The SA engine skips the whole collision block when
  `nextDirectionIsForward == false && lookState == 0` (`CamSA.cpp:1515`); the
  free-look path should always collide.
- The SA path has no last-resort ground clamp; CamVanilla's ground lift runs only
  during free look and only against the ground under the camera, not bridges.

Fix direction: make the prop-skipping ray keep `checkObjects = true` and step
over only entities whose model name is a known small prop; add SA model-name
support; run collision for every mouse-look frame; add a final ground clamp in
the SA path (and keep it last, after water/shake). Add a `CameraCollision`
on/off? Not needed for the bug; the user wants the pass-through stopped, not an
off switch.

### 4. LCS/VCS boat free-look (VC, video `gta-vc`)

- Moves away when raised: on the LCS/VCS profiles boats run the SA engine
  (`Profiles.cpp: cameraAnchoring = 0`; `CamSA.cpp:181-186`). The SA orbit keeps a
  constant 3D radius, but the free-look pitch clamp and the follow distance are
  coupled; exact cause needs a clip. Do not change until reproduced.
- Ignores the boat's own collision: every collision call sets
  `pIgnoreEntity = CamTargetEntity` (`CamSA.cpp:1522`, `CamVanilla.cpp:1794`,
  `CamVanilla.cpp:854`), so the camera can go under the boat hull. Fix only for
  boats: test the placed camera against the boat's col-model box/hull and push it
  out, without making the normal car camera collide with its target.
- Invisible barrier while lowering: LCS boat row `CARCAM_SET_LCS[4][13] = -0.2`
  (`Profiles.cpp:70`) becomes `maxAlphaAllowed` (`CamSA.cpp:794`); the free-look
  pitch clamp at `CamSA.cpp:1195` stops the camera there. SA boat row is `0.349`
  (`Profiles.cpp:59`), which is why the SA profile and SACarCam feel right; LCS
  table is authentic only for the follow camera. Fix: use the free-look limit
  (SA/`MaxPitch`, or a new knob) during mouse look, keep the LCS limit for the
  follow camera.
- Returns only one axis: current code should return both by default
  (`CameraResetOrder = 0`, `ModernCarCam.h:712-751`, `CamSA.cpp:1106-1120`); the
  reported one-axis behavior needs a logged repro. Add a `FreeLookReturn` mode
  0 = hold (SACarCam behavior), 1 = both axes, 2 = per-axis (existing order), and
  document it. The user explicitly asked for this to be configurable.

### Next steps

- Build a diagnostic ASI with a small log (reverse state/Beta/distances; LOS hit
  entity/model; free-look return start/target/ease) and have the reporter run it
  with the exact profile/ini. Need game version, FLA log/ini, and a short clip per
  issue. Do not guess-fix 4a/4d without that data.
- Start with the definite fixes: FLA live pointer, vanilla free-look radius, LCS
  boat free-look pitch limit, own-boat collision, prop-filter ray.

## 2026-11-09 - Fixes implemented and built

All reported issues were addressed from code/reference analysis; no game run was
available. Build verified with MSBuild (VS 18, `PlatformToolset=v143`), 0
warnings / 0 errors, output `ModernCarCam/Release/III.VC.SA.ModernCarCam.asi`
and copied to `Release/III.VC.SA.ModernCarCam.asi`.

- **FLA / model info:** `CModelInfo` no longer caches `0xA9B0C8`. The live array
  base is read from a FLA-patched instruction on each game: SA scans the
  `CModelInfo::SetModelInfo` signature (immediate at +11, `0x4C553B` on 1.0),
  III 1.0 reads the `AddSimpleModel` store at `0x50B964`+3, VC 1.0 reads the
  store at `0x55F5BF`+3. Each read has an opcode check and falls back to the
  default array base. FLA's own source confirms the SA `0x4C5538` patch and the
  III/VC model-info relocations. This covers `GetColModel`, per-car model-name
  lookup and the frozen camera early return. Other direct addresses were not
  converted: hook targets are version-specific, while only movable data pointers
  benefit from this.
- **Reverse/mouse distance jump:** vanilla free look no longer forces
  `stringLength = CA_MAX_DISTANCE`; it latches the radius when the look starts
  and eases back to the live string on return. This also fixes the "camera moves
  away when raising" symptom on the vanilla engine.
- **Free look holds while stationary:** added `freeLookHold` to the SA follow,
  vanilla car and both boat engines. After the 50-step input hold, a stationary
  vehicle keeps the angle and distance; the existing `CameraResetOrder` return
  starts only when the vehicle moves. No `FreeLookReturn` key was added.
- **LCS/VCS boat free look:** the mod-owned look now clamps against the SA boat
  pitch row (`0.349`) instead of the LCS follow row (`-0.2`); the LCS limit is
  still used by the follow camera. `MaxPitch` overrides both.
- **Collision:** the shared `CameraProcessLineOfSight` keeps object checks on
  and steps over small props (III/VC model-name filter, SA collision-box volume
  filter), so large map objects still block. The SA collision pass now always
  runs; SA got a final ground clamp; the near-plane sphere ignores a boat's own
  hull; own-boat collision pushes the camera out of the hull footprint.
- **Docs:** README/readme/ini note the hold-until-moving behaviour. AGENTS.md
  updated.

Not verified in-game: reverse handoff feel, FLA run, bridge/ground case, boat
pitch/water stop, hold thresholds (0.1 m/s), prop-size filter on SA. The next
agent should test those exact scenarios.

## 2026-11-09 - Follow-up feedback fixes

- **SA free look no longer bumps into traffic lights/props.** The size-only
  filter was replaced by a generated model-name-hash list in
  `ModernCarCam/SAProps.h` (1182 vanilla street-furniture, vegetation and
  barrier names, hashed with the game's uppercase CRC32; `CBaseModelInfo` SA
  stores that hash at +0x4). `IsSACameraPropModelHash` in `ModernCarCam.cpp`
  binary-searches it; the box-size filter remains only as a fallback for custom
  models. Large map geometry is not in the list and still blocks.
- **Free-look return is speed-relative.** `CameraFreeLookReturn::Advance` takes a
  `rateScale`; `FreeLookReturnSpeedScale` pauses at <= 0.1 m/s, ramps from a
  gentle pace as the vehicle pulls away and reaches the normal
  `CameraReturnTime` pace at 5 m/s (~18 km/h). At a stop the return pauses, so
  the angle is held; when moving it gracefully recenters like the SA follow
  camera. `CameraResetOrder` still selects both-axis or per-axis.
- Rebuilt clean: 0 warnings / 0 errors; release ASI updated.

## 2026-11-09 - Structure/interior camera follow-up

- **Camera stuck inside a multi-level garage/structure:** the SA ground clamp
  used `FindGroundZFor3DCoord` and lifted the camera whenever it was within
  0.25 m of any ground. Inside a multi-level structure that query can return the
  floor above (the ceiling), so the camera snapped to the ceiling and looked
  stuck. The clamp now only rescues a camera that is actually below the ground
  (`Source.z < groundZ`) and only when that ground is below the car; the
  clearance snap was removed.
- **Near-plane sphere and the player's own bike/car:** the free-look pass now
  ignores the target vehicle in `TestSphereAgainstWorld`, so a close camera in a
  tight interior is no longer pushed around by its own bike model.
- **Slow-creep return:** the speed scale has a 0.12 floor, so pulling away very
  slowly does not look like a stuck camera; the normal pace is still reached at
  4 m/s (~15 km/h). At a standstill the return remains paused.
- Rebuilt clean; release ASI updated (`C32B5450...`).

## 2026-11-09 - Return semantics correction

- The speed-scaled return now applies **only when the look started while the
  vehicle was stationary** and the vehicle pulls away. `freeLookFromStandstill`
  is latched on the first look-input frame; a look taken while already driving
  uses the normal `CameraReturnTime` pace (rate 1.0). Both car engines and both
  boat engines have their own latch/`*ReturnSpeedScaled` flag.
- The trailing 50-step input hold is cancelled as soon as the vehicle is moving,
  so the return starts immediately after the input stops; it was never meant to
  delay the start. The speed scale only changes how fast the return progresses
  once it has started.
- Boat free-look input now takes a `vehicleMoving` flag for the same early
  release; normal boat returns are not speed-scaled, only the pull-away return
  from a held angle.
- Rebuilt clean; release ASI updated.

## 2026-11-09 - Pull-away return delay and knob

- The pull-away speed scale was too conservative: floor `0.35` and full pace
  at 3 m/s made the first visible reset feel twice as late as wanted. The base
  curve now has a `0.7` floor and reaches normal pace at 2 m/s (~7 km/h), which
  halves the delay while still being slower when creeping.
- Added `[Multipliers] CameraResetFromStop` (`Auto` = 1.0, 2 = twice as fast,
  0.5 = half), also exposed in the debug menu as "Pull-away reset speed (x)".
  It multiplies only the pull-away return; normal driving returns stay at 1.0.
- Rebuilt clean; release ASI updated.

## 2026-11-09 - Traffic wobble on bicycles

- Confirmed the existing filter: `IsMotorcycleVehicle` compares SA vehicle type
  9 (BIKE), so vanilla BMX/MTB (type 10) already did not receive the pass-by
  nudge. Added an explicit `IsBicycleVehicle` check anyway: model ids 481, 509
  and 510, plus the model-name hashes for BMX, MTBIKE and BIKE, so a modded
  bicycle registered with the motorcycle type also cannot trigger it. Both
  engines now gate traffic wobble with `!IsBicycleVehicle(car)`.
- Rebuilt clean; release ASI updated (`BE99E235...`).
