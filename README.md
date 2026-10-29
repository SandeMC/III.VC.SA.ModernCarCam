# III.VC.SA.ModernCarCam

- Also published on LibertyCity: [ModernCarCam (III, VC, SA)](https://libertycity.net/files/244225-moderncarcam-iii-vc-sa.html)

A universal vehicle camera ASI for GTA III, GTA Vice City and GTA San Andreas. With the shipped settings the camera reproduces the original game; every additional behaviour is an ini option or a profile layered on top.

The authentic camera is based on the reversed sources of re3 (GTA III) and reVC (Vice City). GTA III and Vice City implement the vehicle camera differently, so the mod keeps both algorithms and picks the right one for the running game. San Andreas already ships the follow camera this mod is built around, so on SA the mod hooks `CCam::Process_FollowCar_SA` directly and keeps the SA camera's own distance, FOV, angles, anchor and stiffness.

## Installation

1. Install an ASI loader (for example [Silent's ASI Loader](https://github.com/GTAmodding/ASI-Loader/releases/latest) or [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader)).
2. Copy `III.VC.SA.ModernCarCam.asi` and `III.VC.SA.ModernCarCam.ini` into the scripts folder or game folder.
3. Edit `III.VC.SA.ModernCarCam.ini` and set `Profile` to the camera you want. Everything else is optional.

For Widescreen Fix users: this mod takes over the Widescreen Fix's own Speed Sensitive FOV and VCS Camera Shake options: both are forced off in WSF while this mod is installed, so this mod's DynamicSpeedFOV and VCSCamShake settings are authoritative (the code is the same, this is done to avoid confusion)

For Drive-by mods users:
- Modernized Driveby features have been reimplemented in this mod; I wouldn't recommend using both mods at once
- [Manual Driveby VC](https://libertycity.net/files/gta-vice-city/213323-manual-driveby-vc.html), [Manual Driveby III](https://libertycity.net/files/gta-3/213322-manual-driveby-iii.html) and [Manual Driveby Refixed (SA)](https://libertycity.net/files/gta-san-andreas/213857-manual-driveby-refixed.html) are the only "Manual" style mods tested against this mod and verified to be compatible
- When Manual Driveby Refixed is found anywhere in the GTA San Andreas game folder, its side view is disabled by default (`DisableSideView = Auto`); set that key to `0` to override the detection

## Profiles

The camera is selected with a single `Profile` setting:

| Profile      | Description                                                        |
|--------------|--------------------------------------------------------------------|
| Game         | Whatever game is running (default)                                 |
| III          | The GTA III camera                                                 |
| VC           | The Vice City camera                                               |
| SA           | The San Andreas follow camera                                      |
| Enhanced     | Game plus quality-of-life additions (Vice City based)              |
| LCS          | Liberty City Stories camera                                        |
| VCS          | Vice City Stories camera                                           |
| IV           | GTA IV camera style                                                |

Game, III, VC and SA profiles add only the free camera and fixes on top of the original camera. III and VC also react to the terrain: their cameras pitch downhill but not uphill. Everything else (wobble, elastic string, dynamic FOV, shake, etc.) is off in those profiles and available as an override in `[Features]` or through the Enhanced / modern profiles.

Enhanced is based on the Vice City camera: it uses Vice City's feature set (steering roll, per-vehicle zoom, bike-with-passenger height), Vice City's per-zoom camera angles, and Vice City's camera anchor and stiffness, even when running GTA III. It also enables the elastic string, dynamic speed FOV, full terrain pitch tilt, VCS shake and the reverse look-behind camera. When running GTA San Andreas, Enhanced enables those same features but the SA camera keeps its own distance, FOV, angles, anchor and stiffness, and leaves the Vice City steering wobble and VCS shake off by default (they can still be turned on in `[Features]`). The III-only turret hook is not installed on SA.

Enhanced also turns on the smooth side view and sets the drive-by defaults it ships with: `LockShootDirectionKBM = 0` and `LockShootDirectionJOY = 1`.

IV is an approximation of IVs feel applied on top of the SA camera; its exact distances and angles can be dialed manually.

## Features

- One profile for the whole camera, with optional per-feature overrides
- Individual per-car camera configuration
- Every individual part of the camera system can be customized for your own camera presets
- Faithful GTA III, Vice City and San Andreas vehicle cameras recreations, can add any feature on-top of them
- Free mouse look from San Andreas, with a smoothed gamepad right stick view
- Keyboard free look that leaves the look/turret keys to vehicle special features (hydraulics, forklifts, tow trucks, etc.) on the axis each feature uses, while the other axis and the mouse still look around
- Free turret control from San Andreas (Rhino / Firetruck)
- Modern drive-by aiming - while firing, aim where the camera looks, with an optional per-burst direction lock, separate for keyboard/mouse and gamepad
- Smooth side view - the look left/right/behind transition swings into place instead of snapping
- Option to disable the side view entirely, automatically enabled on GTA San Andreas when Manual Driveby Refixed is detected in the game folder
- Configurable free-look reset: both axes together or one axis at a time (vertical first or horizontal first), with an overall duration multiplier and per-axis time shares
- Configurable camera collision: optionally ignore props such as lampposts and other cars in the normal rear view and collide only with buildings and map geometry (the free camera and side views always ignore props), with near-plane protection so the view cannot leak through walls
- Configurable dynamic speed FOV (ported from ThirteenAG's Widescreen Fix)
- Configurable Vice City Stories camera shake (ported from ThirteenAG's Widescreen Fix)
- Configurable Steering wobble from Vice City
- Configurable terrain pitch tilt, with a new option to also apply the tilt when going upwards and several adjustments made to how it works;
- Per-vehicle zoom from Vice City as an option
- Bike passenger height option
- Elastic string stretch from San Andreas as an option
- A configurable traffic pass-by wobble for cars and motorcycles (from 36 on the game's speed scale by default), disabled on bicycles, boats and aircraft
- Adjustable stiffness, anchoring and height
- Enhanced reverse driving camera, with an adjustable delay before it swings

## Debug menu

When the [game's debug menu](https://libertycity.net/files/gta-san-andreas/161611-debugmenu.html) is available, the mod adds a **ModernCarCam** section to it with live toggles for every feature.

## Building

Open `ModernCarCam.sln` and build `Release` (Win32). The output is `Release\III.VC.SA.ModernCarCam.asi`, next to the shipped `III.VC.SA.ModernCarCam.ini`.

## Credits & licenses

- Original SACarCam by erorcun and the SACarCamWithModernDriveBy fork by ICantReadYourMind.
- Authentic camera algorithms based on the reversed GTA III / Vice City sources of re3 / reVC, using the [Hezkore/hez-gta-re3](https://github.com/Hezkore/hez-gta-re3) fork. re3 is not released under a standard license; its terms (educational / documentation / modding use, non-commercial, keep derivative work open source, give proper credit) are reproduced in `licenses/re3.txt`.
- VCS camera shake and dynamic speed FOV ported from [ThirteenAG's WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack), MIT licensed. See `licenses/WidescreenFixesPack.txt`.
- San Andreas addresses and layouts grounded in [gta-reversed](https://github.com/gta-reversed/gta-reversed) and [DK22Pac/plugin-sdk](https://github.com/DK22Pac/plugin-sdk).
- The modern drive-by aiming (sideways aiming based on the camera angle) was worked out with help from [ModernizedDriveBy](https://libertycity.net/files/gta-san-andreas/215460-modernized-driveby.html) by void.
- This project is MIT licensed; see `LICENSE`. The third-party notices in `licenses/` apply to the portions they describe.
- This project has been developed primarily by AI agents, but it has been thoroughly tested throughout the way.
