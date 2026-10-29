#pragma once

// ---------------------------------------------------------------------------
// ModernCarCam - shared internal interface.
//
// This header is the private interface shared by the mod's translation units.
// ModernCarCam.cpp owns DllMain and the memory hooks; the rest of the project
// is split by concern:
//
//   Profiles.cpp      the camera tables and applyProfile() (the profile model)
//   Settings.cpp      ini parsing and the settings/feature state
//   CamVanilla.cpp    the vanilla "camera on a string" and behind-boat engines
//   CamSA.cpp         the San Andreas follow-camera engine
//   DebugMenu.cpp     the ModernCarCam section of aap's debug menu
//   ModernCarCam.cpp  DllMain, hooks, addresses, shared maths helpers
//
// Profile model
// -------------
// The user picks a single user-facing ModernProfile in the ini (Game, III, VC,
// SA, Enhanced, LCS, VCS, IV, Custom). applyProfile() maps that onto the
// internal CameraProfileType (PROFILE_SA, PROFILE_LCS, PROFILE_VANILLA,
// PROFILE_CUSTOM), which selects the per-game camera tables, and sets the
// default feature flags. [Features] keys then optionally override individual
// flags without changing the tables.
//
// Camera engines
// --------------
// Two families of engine exist because GTA III and Vice City (and their later
// ports Re:LCS / SA) implement the vehicle camera differently:
//
//   * CamVanilla.cpp holds the authentic III/VC "on a string" camera, the GTA
//     III top-down-style height pass, and the two behind-boat cameras.
//   * CamSA.cpp holds the San Andreas follow car camera used by the SA, LCS,
//     VCS and IV profiles.
//
// Both families are templated on the game-specific types (CamClass,
// CameraClass, VehicleClass, WorldClass and, where the collision model is
// needed, ColModelClass); this is how one algorithm is shared between GTA III
// and Vice City layouts. Their entry points take the same argument order:
//
//   (CameraClass* TheCamera, CamClass* cam, VehicleClass* car,
//    const CVector& CameraTarget, float TargetOrientation)
//
// The authentic camera algorithms are based on the reversed GTA III / Vice City
// sources of re3 / reVC (https://github.com/Hezkore/hez-gta-re3); see
// licenses/re3.txt for the attribution and terms.
// ---------------------------------------------------------------------------

#include "common.h"
#include "MemoryMgr.h"
#include "GTA.h"
#include "Camera.h"
#include "Pad.h"
#include "GInputAPI.h"

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

extern HMODULE dllModule;
extern int gtaversion;

// isVC() also returns true for Re:LCS.
extern bool isReLCS;

// ---------------------------------------------------------------------------
// Profile model
// ---------------------------------------------------------------------------

// Internal camera table selector. This mirrors the original mod's table sets
// and is always derived from the user-facing ModernProfile.
enum CameraProfileType : int8_t {
	PROFILE_SA = 0,
	PROFILE_LCS = 1,
	PROFILE_VANILLA = 2,
	PROFILE_CUSTOM = 3
};

// User-facing profiles. "Game-Matched" reproduces whatever game is running;
// III/VC/SA/LCS/VCS/IV force a particular game's camera; "Enhanced" is the
// game-matched camera plus quality-of-life additions.
enum ModernProfile : int8_t {
	PROFILE_GAME_MATCHED = 0,
	PROFILE_III,
	PROFILE_VC,
	PROFILE_SA_CAM,
	PROFILE_ENHANCED,
	PROFILE_LCS_CAM,
	PROFILE_VCS,
	PROFILE_IV,
	PROFILE_CUSTOM_CAM
};

extern ModernProfile cameraProfile;
extern CameraProfileType masterProfile;
extern CameraProfileType distanceProfile;
extern CameraProfileType fovProfile;
extern CameraProfileType anglesProfile;

// Feature and tuning state. Every "effect strength" below is a multiplier:
//   1.0  = the profile's default strength (the way the feature shipped before)
//   0.0  = off
//   other positive values scale the effect linearly (0.5 = half, 2 = double)
// Negative values are treated as 0.
// applyProfile() sets the defaults for the selected profile; LoadSettings()
// then applies any [Features] overrides.
extern float cameraWobble;
extern float elasticStringPhysics;
extern float pitchTilt;         // master strength for both slope directions
extern float pitchTiltUphill;   // <0 = use the master value
extern float pitchTiltDownhill; // <0 = use the master value
extern float pitchTiltMinAngle; // radians: slopes shallower than this produce no tilt (jitter guard)
extern float pitchTiltAirHoldTime;  // seconds: hold the last ground tilt after leaving the ground
extern float pitchTiltAirBlendTime; // seconds: ease from the held tilt to the vehicle's nose while airborne
extern float maxPitchAngle;     // <0 = profile default; camera pitch limits
extern float minPitchAngle;     // <0 = profile default
extern float dynamicSpeedFOV;
extern float dynamicSpeedFOVStartSpeed; // <0 = profile default
extern float dynamicSpeedFOVMaxFOV;     // <0 = profile default
extern float vcsCamShake;
extern float vcsCamShakeStartSpeed;     // <0 = profile default
extern float cameraAnchoring;   // >0 = rigid anchor (scaled); 0 = SA float
extern float cameraStiffness;   // multiplies the profile's default stiffness
extern float headingFollow;     // multiplies the SA follow camera's yaw follow (1 = profile default; lower = camera holds its yaw longer)
extern float vehicleSpecificZoom;
extern float trafficCamWobble;
extern float trafficCamWobbleMinSpeed;    // forward speed (m/tick) below which the traffic nudge never fires
extern float trafficCamWobbleFullSpeed;   // forward speed (m/tick) at which the nudge reaches its max multiplier
extern float trafficCamWobbleMaxMultiplier; // nudge strength multiplier reached at TrafficCamWobbleFullSpeed
extern float reverseCamDelay;             // seconds of reversing before the reverse camera swings
extern float dynamicSpeedFOVDecay;        // per-step FOV decay base (smaller = winds down faster)
extern float cameraReturnSpeed; // multiplier on the free-look auto-return rate
extern float cameraReturnTime;  // seconds the free-look auto-return lasts
extern bool modernTurretControl;
extern bool modernDriveBy;
extern bool lockShootDirKBM;   // modern drive-by: keep the shot direction while firing (keyboard/mouse)
extern bool lockShootDirJOY;   // modern drive-by: keep the shot direction while firing (gamepad)
extern bool mouseFreeLook;
extern bool heightIncreaseOnBike;
extern bool fixTheBug;
extern bool reverseCam;
extern bool seeUnderwater;
extern bool enhancedVC;                    // Enhanced uses Vice City's features + camera angles even in GTA III
extern bool smoothSideView;                 // smooth side view: look left/right/behind swings smoothly instead of the vanilla instant change
extern bool disableSideView;                // no side view at all: look left/right/behind never move the camera
extern bool keyboardFreeLook;               // let the game's keyboard look/turret keys (numpad) drive the free camera; on by default in SA, off in III/VC
extern bool cameraPropCollision;            // camera collides with props (lampposts, cars); false = buildings/map geometry only
extern int8_t cameraResetOrder;             // free-look reset: 0 both, 1 vertical first, 2 horizontal first
extern float cameraResetTime;               // multiplier on the total free-look reset duration (1 = current)
extern float cameraResetVerticalTime;       // per-axis share of the reset (per-axis modes only)
extern float cameraResetHorizontalTime;     // per-axis share of the reset (per-axis modes only)

// ---- GTA III top-down camera enhancements (stacked on the game's own camera) ----
// GTA III's MODE_TOPDOWN / MODE_GTACLASSIC, post-processed so the distance can be
// tuned and so a ceiling above the car can drop the view to first person. The
// game's own top-down logic stays in charge; this only nudges the finished pose.
extern bool topDownEnhance;        // enable the GTA III top-down post-process
extern float topDownDistance;      // if > 0, absolute horizontal distance from the car
extern float topDownDistanceScale; // otherwise, distance scale
extern float topDownClearance;     // height above the car to scan for a ceiling; 0 disables the first-person fallback

void InitVanillaCameraEnhanceHooks(void);

// ---- First-person mod compatibility (e.g. Ultimate First Person) ----
// A first-person camera mod drives the vehicle camera look itself. When one is
// loaded the mod stops publishing the drive-by look flags (and stops suppressing
// the game's own clearing of them), so the two no longer fight over the view.
// Everything else (wobble, shake, FOV, side view, free-look) is kept.
extern bool firstPersonModCompat;
typedef bool(__cdecl* UltFP_IsFirstPerson_t)();
extern UltFP_IsFirstPerson_t pUltFP_IsFirstPerson;
void CheckFirstPersonModCompat(void);

// [Offsets] - camera offsets, applied independently of the selected profile.
extern float cameraHeightOffset;           // extra height in metres added to the camera target
extern float cameraLateralOffset;          // side offset of the camera
extern float cameraDistanceOffsetNear;     // extra distance in metres, near zoom view
extern float cameraDistanceOffsetMid;      // extra distance in metres, mid zoom view
extern float cameraDistanceOffsetFar;      // extra distance in metres, far zoom view
extern float cameraMinDistance;            // closest allowed distance
extern float cameraDistanceScale;          // distance multiplier for the whole camera
extern CVector cameraDriverOffset;         // target orbit offset, e.g. the IV driver seat

// The [Offsets] distance offset matching the camera's current zoom view
// (1 = near, 2 = mid, 3 = far). Anything else uses the mid offset.
inline float CameraDistanceOffsetForZoom(int zoomIndicator) {
	if (zoomIndicator == 1)
		return cameraDistanceOffsetNear;
	if (zoomIndicator == 3)
		return cameraDistanceOffsetFar;
	return cameraDistanceOffsetMid;
}

// [Custom] profile shape. Only used when a *Profile selector is Custom.
extern float customDistNear;
extern float customDistMid;
extern float customDistFar;

extern float customBaseFOV;

extern float customAngleNear;
extern float customAngleMid;
extern float customAngleFar;

// Debug-menu choice labels.
extern const char *profileNames[];

// ---------------------------------------------------------------------------
// Per-car camera overrides
//
// An ini section named "Car" + a vehicle key gives that one car its own camera,
// separate from the global Profile. The key is either the vehicle model id
// ([Car400], [Car411], ...) or the model name as it appears in the game's data
// files ([CarHOTRING], [CarYARDIE], ...), matched case-insensitively. A key may
// also be a comma-separated list to cover several models with one section (for
// example [Car494,502,503] or [Carhotring,hotrina,hotrinb]). Every
// key is optional: the entry starts as a copy of the global settings and only
// the keys it lists are changed, so omitted keys keep the global value.
//
// LoadSettings() parses the sections once. Process_FollowCar_SA applies the
// matching entry for the frame and restores the global settings afterwards, so
// the different camera applies only to that car. Any number of sections may be
// added.
// ---------------------------------------------------------------------------
struct CarCamSettings {
	CameraProfileType distanceProfile;
	CameraProfileType fovProfile;
	CameraProfileType anglesProfile;

	float customDistNear;
	float customDistMid;
	float customDistFar;
	float customBaseFOV;
	float customAngleNear; // radians (the ini keys are degrees)
	float customAngleMid;
	float customAngleFar;

	float cameraHeightOffset;
	float cameraLateralOffset;
	float cameraDistanceOffsetNear;
	float cameraDistanceOffsetMid;
	float cameraDistanceOffsetFar;
	float cameraMinDistance;
	float cameraDistanceScale;
	CVector cameraDriverOffset;
};

// Reads the current global camera settings into out.
void CaptureCarCamSettings(CarCamSettings& out);
// Writes s into the global camera settings and rebuilds the Custom tables.
void ApplyCarCamSettings(const CarCamSettings& s);
// Returns the override for a vehicle model index, or nil if it has none.
const CarCamSettings* FindCarCameraOverride(int modelIndex);
// Re-reads the [Car<model id>] sections from the current ini. Called after a
// profile change so per-car entries inherit the new global settings.
void ReloadPerCarCameraSections(void);

// ---------------------------------------------------------------------------
// Per-game camera tables
// ---------------------------------------------------------------------------

extern const float CarZoomModesSA[];
extern const float CarZoomModesLCS[];
extern const float CarZoomModesVC[];
extern const float CarZoomModesIII[];

// Alpha angles (up-down)
extern const float ZmOneAlphaOffset[];
extern const float ZmTwoAlphaOffset[];
extern const float ZmThreeAlphaOffset[];

extern const float ZmOneAlphaOffsetLCS[];
extern const float ZmTwoAlphaOffsetLCS[];
extern const float ZmThreeAlphaOffsetLCS[];

extern const float ZmOneAlphaOffsetVC[];
extern const float ZmTwoAlphaOffsetVC[];
extern const float ZmThreeAlphaOffsetVC[];

extern const float ZmOneAlphaOffsetIII[];
extern const float ZmTwoAlphaOffsetIII[];
extern const float ZmThreeAlphaOffsetIII[];

extern const float CARCAM_SET_SA[][15];
extern const float CARCAM_SET_LCS[][15];
extern const float CARCAM_SET_VANILLA[][15];

// The Custom table set is mutable: LoadSettings() fills it from the ini.
extern float CARCAM_SET_CUSTOM[8][15];
extern float CarZoomModesCustom[15];
extern float ZmOneAlphaOffsetCustom[5];
extern float ZmTwoAlphaOffsetCustom[5];
extern float ZmThreeAlphaOffsetCustom[5];

// Tilt table scaling used by the roll/wobble code.
extern const float TiltOverShoot[];
extern const float TiltTopSpeed[];
extern const float TiltSpeedStep[];

// ---------------------------------------------------------------------------
// GInput state
// ---------------------------------------------------------------------------

extern IGInputPad* ginputPad;
extern int ginputLoaded; // 1: not installed 2: installed
extern GINPUT_PAD_SETTINGS padSettings;

// True when GInput reports a pad actually in the player's hands. GInput is never
// null after GInput_Load (it installs a dummy on failure), but the pointer is
// checked anyway so the helper is safe to call at any time.
inline bool GInputPadInHands() {
	return ginputPad != nil && ginputPad->HasPadInHands();
}

// Whether the keyboard look/turret keys may drive the vehicle free camera. The
// game folds the keyboard keys and the analogue stick into the same right-stick
// axis, so GInput is consulted to tell a pad from a keyboard: with the toggle off
// the axis is only read while a pad is in the player's hands. Without GInput the
// two cannot be told apart, so the toggle is left alone.
inline bool KeyboardFreeLookAxisEnabled() {
	if (keyboardFreeLook)
		return true;
	if (ginputLoaded != 2)
		return true;
	return GInputPadInHands();
}

// True when the vehicle fire button is held. GetCarGunFired is only hooked for
// GTA III 1.0: the other III builds use addresses that are not installed (as
// with the rest of the III pad methods), so they report false instead of
// jumping through a null address.
inline bool CarGunFiredHeld() {
	if (isIII() && gtaversion != III_10)
		return false;
	return pad0.GetCarGunFired();
}

// ---------------------------------------------------------------------------
// Addresses and game globals
// ---------------------------------------------------------------------------

extern void (*&RwCamera);

// Actually static member of CCamera
extern bool& m_bUseMouse3rdPerson;
// Actually static member of CVehicle
extern bool& m_bDisableMouseSteering;

extern uint32& m_snTimeInMilliseconds;
extern float& ms_fTimeStep;

// These are static members of CWorld
extern CColPoint& ms_testSpherePoint;
extern CEntity*& pIgnoreEntity;

extern bool lookingRelativelyLeft;
extern bool lookingRelativelyRight;

// ---------------------------------------------------------------------------
// Vehicle model IDs and per-game class helpers
// ---------------------------------------------------------------------------

#define MI_III_YARDIE 135
#define MI_III_RCBANDIT 131
#define MI_III_RHINO 122
#define MI_III_DODO 126
#define MI_III_FIRETRUCK 97

#define MI_VC_VOODOO 142
#define MI_VC_FIRETRUCK 137
#define MI_VC_RHINO 162
#define MI_VC_RCBANDIT 171
#define MI_VC_RCBARON 194
#define MI_VC_RCRAIDER 195
#define MI_VC_RCGOBLIN 231

#define MI_RELCS_FIRETRUCK 138
#define MI_RELCS_RHINO 162
#define MI_RELCS_RCBANDIT 169 // original LCS didn't use this though, RC vehicles have same camera distance as normal veh - Ryadica926
#define MI_RELCS_YARDIE 173
#define MI_RELCS_RCGOBLIN 211 // same as rc bandit
#define MI_RELCS_RCRAIDER 212 // same as above

#define Tank (isIII() ? MI_III_RHINO : (isReLCS ? MI_RELCS_RHINO : MI_VC_RHINO))
#define FireTruk (isIII() ? MI_III_FIRETRUCK : (isReLCS ? MI_RELCS_FIRETRUCK : MI_VC_FIRETRUCK))
#define MI_SA_FIRETRUCK 407
constexpr int MI_SA_HYDRA = 520;
constexpr int MI_SA_ANDROMADA = 592;
// Fire truck model for the running game (SA uses its own id).
#define IsFireTruk(veh) ((veh)->m_modelIndex == (isSA() ? MI_SA_FIRETRUCK : FireTruk))
#define CarWithHydraulics (isIII() ? MI_III_YARDIE : (isReLCS ? MI_RELCS_YARDIE : MI_VC_VOODOO))
#define RcBandit (isIII() ? MI_III_RCBANDIT : (isReLCS ? MI_RELCS_RCBANDIT : MI_VC_RCBANDIT))

// These are being used only if isVC() is true
#define RcGoblin (isReLCS ? MI_RELCS_RCGOBLIN : MI_VC_RCGOBLIN)
#define RcRaider (isReLCS ? MI_RELCS_RCRAIDER : MI_VC_RCRAIDER)

// SA aircraft model IDs (vehicles.ide). The eVehicleType-based check is the
// primary one, but the model list is a reliable fallback in case the type field
// is not what we expect, so aircraft are never treated as cars.
inline bool IsSAAircraftModel(int modelIndex) {
	switch (modelIndex) {
	case 592: case 577: case 511: case 512: case 593: case 520: case 553: // planes
	case 476: case 519: case 460: case 513:
	case 548: case 425: case 417: case 487: case 488: case 497: case 563: // helicopters
	case 447: case 469:
	case 464: case 465: case 501:                                          // RC Baron / Raider / Goblin
		return true;
	default:
		return false;
	}
}

// Vanilla CCam::Process disables the vehicle look-left/right keys for
// helicopters and the RC Baron (their yaw keys share the same controls). The
// mod's side view and free-look have to mirror that, otherwise turning one of
// those vehicles swings the camera aside. Aircraft yaw/rudder on the same keys
// too, so planes are included.
inline bool DisableVehicleSideLook(int modelIndex, bool isHeli, bool isPlane) {
	if (isHeli || isPlane)
		return true;
	if (isSA() && IsSAAircraftModel(modelIndex))
		return true;
	if (isVC())
		return modelIndex == MI_VC_RCBARON;
	return false;
}

// The III/VC manual drive-by mods take the camera over to an on-foot/aim mode
// (CCamera::TakeControl) while the player aims from a vehicle. While one of those
// cameras is active the vehicle camera is not, and the mod must leave the whole
// camera and aim alone so it does not fight the manual drive-by.
inline bool IsPedAimCameraMode(int mode) {
	return mode == MODE_FOLLOWPED || mode == MODE_AIMING || mode == MODE_SNIPER;
}

#define DefaultFOV 70.0f
#define DefaultNearClip 0.9f

// Shared speed constants for the roll/wobble and elastic-string terms. The
// vanilla engines express forward speed both in m/s and on a 180-unit scale
// (the GTA convention), clamped at 210.
constexpr float MaxForwardSpeed = 210.0f;
constexpr float SpeedKphFactor = 180.0f;

// CTimer::ms_fTimeStep is expressed in 1/50 s units (about 1.0 at 50 FPS and
// 50/30 at the 30 FPS reference), not in seconds. Multiply it by this to
// advance a seconds-based timer each frame.
constexpr float TimeStepToSeconds = 1.0f / 50.0f;

// VCS camera shake (ported from ThirteenAG's WidescreenFixesPack): starts at
// 0.65 and ramps to 1.0, scaled down by 200 (WSF's UpdatePlayerVehicleSpeedBlur
// uses the same 0.65 -> 1.0 ramp and /200; a rework had doubled this to 400,
// which halved the shake and made it feel like it was not working).
constexpr float VCSCamShakeStartSpeed = 0.65f;
constexpr float VCSCamShakeFullSpeed = 1.0f;
constexpr float VCSCamShakeRange = 0.35f;
constexpr float VCSCamShakeDivisor = 200.0f;

// On the Enhanced profile the VCS-style shake is scaled per platform: GTA III
// and Vice City get a 1.5x boost so it lands as hard as it does on San Andreas,
// while San Andreas is damped to 0.5x. Every other profile uses the faithful
// 1.0 strength, matching the WidescreenFixesPack/VCS reference exactly.
constexpr float NonSACamShakeScale = 1.5f;
constexpr float SAEnhancedCamShakeScale = 0.5f;

// Height above the water surface the camera is held at when KeepCameraOverWater
// is on. 0.5 matches the offset the III/VC engines already use, and keeps the
// near plane from cutting into the surface.
constexpr float WaterSurfaceOffset = 0.5f;

// True when the camera is under solid cover that is itself below the water
// surface: a tunnel under a lake, the house pools above Richman's tunnel, and
// similar. Lifting the camera to the water level there would push it through
// the cover, so the water clamp leaves it alone.
template<class WorldClass>
inline bool CameraUnderCoverBelowWater(const CVector& source, float waterLevel) {
	bool foundRoof = false;
	const float roofZ = WorldClass::FindRoofZFor3DCoord(source.x, source.y, source.z + 0.1f, &foundRoof);
	return foundRoof && roofZ < waterLevel;
}

// KeepCameraOverWater, shared by every camera engine. The query is made at the
// camera itself, not the car: that is the only way the free camera and the side
// views can be kept above water, and it removes the old car-height thresholds
// that made the rule change from one situation to the next. Water that is above
// the camera but behind solid cover is ignored (see above). Returns true when it
// moved the camera.
template<class WorldClass>
inline bool KeepCameraAboveWater(WorldClass*, CVector& source) {
	if (seeUnderwater)
		return false;

	float waterLevel = -6000.0f;
	if (!CWaterLevel::GetWaterLevelNoWaves(source.x, source.y, source.z, &waterLevel))
		return false;
	if (source.z >= waterLevel + WaterSurfaceOffset)
		return false;
	if (CameraUnderCoverBelowWater<WorldClass>(source, waterLevel))
		return false;

	source.z = waterLevel + WaterSurfaceOffset;
	return true;
}

inline float VCSCamShakeScale(void) {
	if (cameraProfile != PROFILE_ENHANCED)
		return 1.0f;
	return isSA() ? SAEnhancedCamShakeScale : NonSACamShakeScale;
}

#define RwFrameGetMatrix(frame) (RwMatrix*)((addr)frame + 0x10)
#define GetVehicleComponent(car, comp) *(void**)((addr)car + (isIII() ? 0x37C : 0x394) + comp*4) // In CAutomobile. normally returns RwFrame*

// SA's CMouseControllerState is { lmb..bmx2, align, z, x, y } whereas the
// III/VC one is { lmb..bmx2, x, y }. Reading the shared struct's x/y on SA
// would return z/x, so the SA offsets are used explicitly.
inline float GetMouseControllerX(void) {
	if (isSA())
		return *(float*)(0xB73418 + 0x0C); // CPad::NewMouseControllerState.x
	return CPad::NewMouseControllerState.x;
}
inline float GetMouseControllerY(void) {
	if (isSA())
		return *(float*)(0xB73418 + 0x10); // CPad::NewMouseControllerState.y
	return CPad::NewMouseControllerState.y;
}

// SA CPad stores DisablePlayerControls as an unsigned short bitfield at 0x10E.
inline bool GetDisablePlayerControls(void* pad) {
	if (isSA())
		return *((uint16*)((addr)pad + 0x10E)) != 0;
	return *((uint8*)((addr)pad + (isIII() ? 0xDF : 0xF0))) != 0;
}
// San Andreas caches the handling flags on the vehicle itself (CVehicleSA::
// m_nHandlingFlags at 0x38C); III/VC only expose them through the handling
// data.
#define GetHandlingFlags(veh) *((uint32*)(isSA() ? ((addr)veh + 0x38C) : ((addr)veh->pHandling + (isIII() ? 0xC8 : 0xCC))))
// SA CAutomobile::m_nWheelsOnGround 0x961; SA CBike::m_nNumWheelsOnGround 0x805.
#define GetWheelsOnGround(veh) *((uint8*)((addr)veh + (isSA() ? 0x961 : (isIII() ? 0x590 : 0x5C4)))) // In CAutomobile
#define GetMysteriousWheelRelatedThingBike(veh) *((uint8*)((addr)veh + (isSA() ? 0x805 : 0x4DC))) // In CBike
#define GetDoomAnglePtrLR(veh) (float*)((addr)veh + (isIII() ? 0x580 : 0x5B0)) // In CAutomobile
#define GetDoomAnglePtrUD(veh) (float*)((addr)veh + (isIII() ? 0x584 : 0x5B4)) // In CAutomobile
#define GetPedObjective(ped) *((uint32*)((addr)ped + (isIII() ? 0x164 : 0x160)))
#define GetNearPlane() *(float*)((addr)RwCamera + 0x80)

// Virtual func. in GTA. On SA the automobile height is CAutomobile::m_fFrontHeightAboveRoad
// (0x898); boats/bikes fall back to the collision model bounding box so no
// out-of-bounds read can happen for a non-CAutomobile vehicle.
#define GetHeightAboveRoad(veh, classToCast) (isSA() ? \
	((veh->IsCar() || veh->IsHeliType() || veh->IsPlaneType()) ? *((float*)((addr)veh + 0x898)) : \
		-1.0f * ((classToCast*)veh->GetColModel())->boundingBox.min.z) : \
	(veh->IsCar() ? *((float*)((addr)veh + (isIII() ? 0x50C : 0x530))) : \
		-1.0f * ((classToCast*)veh->GetColModel())->boundingBox.min.z))

// Per-game vehicle type classification used by the SA engine, so the shared
// algorithm does not have to know the handling-flag offsets of each game.
inline bool CVehicleIII::IsHeliType(void) { return (GetHandlingFlags(this) & 0x20000) != 0; }
inline bool CVehicleIII::IsPlaneType(void) { return (isIII() && m_modelIndex == MI_III_DODO) || (GetHandlingFlags(this) & 0x40000) != 0; }
inline bool CVehicleIII::IsBikeType(void) { return (GetHandlingFlags(this) & 0x10000) != 0 || IsBike(); }
inline bool CVehicleVC::IsHeliType(void) { return (GetHandlingFlags(this) & 0x20000) != 0; }
inline bool CVehicleVC::IsPlaneType(void) { return (GetHandlingFlags(this) & 0x40000) != 0; }
inline bool CVehicleVC::IsBikeType(void) { return (GetHandlingFlags(this) & 0x10000) != 0 || IsBike(); }

// True when the vehicle has working hydraulics. San Andreas keeps this in the
// handling data (tHandlingData::m_bHydraulicInst, handling-flags bit 17), which
// covers every lowrider instead of hard-coding model ids; III/VC/Re:LCS have a
// single hydraulics car. The hydraulics keys are the same CarGun/turret axes
// the camera free look reads, so this is also used to keep the camera from
// moving while those keys operate the suspension.
template<class VehicleClass>
inline bool VehicleHasHydraulics(VehicleClass* car) {
	if (!car || !car->pHandling || !car->IsCar())
		return false;
	if (isSA())
		return (GetHandlingFlags(car) & 0x20000) != 0;
	return car->m_modelIndex == CarWithHydraulics;
}

// True for a motorcycle. San Andreas uses vehicle type 10 for the BMX and the
// mountain bike; those are bicycles, not motorcycles. III/VC have no bicycles,
// so every bike there is a motorcycle.
template<class VehicleClass>
inline bool IsMotorcycleVehicle(VehicleClass* car) {
	if (isSA())
		return *((int32*)((addr)car + 0x590)) == 9; // CVehicleSA::m_nVehicleType
	return car->IsBike();
}

// ---------------------------------------------------------------------------
// Shared maths helpers
// ---------------------------------------------------------------------------

inline float LimitRadianAngle(float angle) {
	while (angle >= PI) angle -= TWOPI;
	while (angle < -PI) angle += TWOPI;
	return angle;
}

inline const CVector
Multiply3x3(const CMatrix& mat, const CVector& vec)
{
	return CVector(
		mat.m_matrix.right.x * vec.x + mat.m_matrix.up.x * vec.y + mat.m_matrix.at.x * vec.z,
		mat.m_matrix.right.y * vec.x + mat.m_matrix.up.y * vec.y + mat.m_matrix.at.y * vec.z,
		mat.m_matrix.right.z * vec.x + mat.m_matrix.up.z * vec.y + mat.m_matrix.at.z * vec.z);
}

inline const CVector
Multiply3x3(const CVector& vec, const CMatrix& mat)
{
	return CVector(
		mat.m_matrix.right.x * vec.x + mat.m_matrix.right.y * vec.y + mat.m_matrix.right.z * vec.z,
		mat.m_matrix.up.x * vec.x + mat.m_matrix.up.y * vec.y + mat.m_matrix.up.z * vec.z,
		mat.m_matrix.at.x * vec.x + mat.m_matrix.at.y * vec.y + mat.m_matrix.at.z * vec.z);
}

inline float GetATanOfXY(float x, float y) {
	if (x == 0.0f && y == 0.0f)
		return 0.0f;

	float xabs = fabsf(x);
	float yabs = fabsf(y);

	if (xabs < yabs) {
		if (y > 0.0f) {
			if (x > 0.0f)
				return 0.5f * PI - atan2f(x / y, 1.0f);
			else
				return 0.5f * PI + atan2f(-x / y, 1.0f);
		}
		else {
			if (x > 0.0f)
				return 1.5f * PI + atan2f(x / -y, 1.0f);
			else
				return 1.5f * PI - atan2f(-x / -y, 1.0f);
		}
	}
	else {
		if (y > 0.0f) {
			if (x > 0.0f)
				return atan2f(y / x, 1.0f);
			else
				return PI - atan2f(y / -x, 1.0f);
		}
		else {
			if (x > 0.0f)
				return 2.0f * PI - atan2f(-y / x, 1.0f);
			else
				return PI + atan2f(-y / -x, 1.0f);
		}
	}
}

inline float GetAspectRatio() {
	if (isSA())
		return *(float*)0xC3EFA4; // CDraw::ms_fAspectRatio (SA 1.0 US)
	else if (isVC())
		return *(float*)0x94DD38; // CDraw::ms_fAspectRatio
	else {
		static float aspectRatio = 0.0f;
		static bool gotTheAR = false;
		if (!gotTheAR) {
			gotTheAR = true;

			RsGlobalType& RsGlobal = *(RsGlobalType*)0x8F4360;
			return aspectRatio = ((float)RsGlobal.width) / ((float)RsGlobal.height);
		} else
			return aspectRatio;
	}
}

inline float GetMouseAccel(CCameraVC *camera) {
	UNREFERENCED_PARAMETER(camera);
	return *(float*)0x94DBD0; // CCamera::m_fMouseAccelHorzntl
}

inline float GetMouseAccel(CCameraIII *camera) {
	return camera->m_fMouseAccelHorzntl;
}

inline float GetMouseAccel(CCameraSA *camera) {
	UNREFERENCED_PARAMETER(camera);
	return *(float*)0xB6EC1C; // CCamera::m_fMouseAccelHorzntl (SA 1.0 US)
}

inline bool IsVehicleSuspensionHigh(CCameraVC* camera) { return camera->m_bVehicleSuspenHigh; }
inline bool IsVehicleSuspensionHigh(CCameraIII*) { return false; }
inline bool IsVehicleSuspensionHigh(CCameraSA* camera) { return camera->m_bVehicleSuspenHigh; }

// The engine step buffer; defined in ModernCarCam.cpp.
void WellBufferMe(float Target, float* CurrentValue, float* CurrentSpeed, float MaxSpeed, float Acceleration, bool IsAngle);

// Shared free-look return timing. CameraReturnTime is the base duration in
// seconds and CameraReturnSpeed scales it (higher = quicker). CameraResetTime
// scales the result again; 1.0 keeps the existing duration. Every camera engine
// uses the same smoothstep so the horizontal and vertical axes finish together
// instead of the pitch snapping back first.
inline float CameraFreeLookReturnDuration(void) {
	return (max(0.02f, cameraReturnTime) / max(0.05f, cameraReturnSpeed)) *
		max(0.05f, cameraResetTime);
}

inline float CameraFreeLookReturnEase(float progress) {
	progress = clamp(progress, 0.0f, 1.0f);
	return progress * progress * (3.0f - 2.0f * progress);
}

// Ease for one axis. In the default mode both axes use the same progress. In a
// per-axis mode the total duration is shared according to the two time
// multipliers, and only one axis moves at a time.
inline float CameraFreeLookAxisEase(float progress, bool vertical,
	bool verticalMoves = true, bool horizontalMoves = true) {
	progress = clamp(progress, 0.0f, 1.0f);
	if (cameraResetOrder != 1 && cameraResetOrder != 2)
		return CameraFreeLookReturnEase(progress);

	// An axis that is already at its target must not reserve a slice of the
	// reset time, otherwise the other axis waits for it before starting.
	if (!verticalMoves && !horizontalMoves)
		return 1.0f;
	if (!verticalMoves)
		return vertical ? 1.0f : CameraFreeLookReturnEase(progress);
	if (!horizontalMoves)
		return vertical ? CameraFreeLookReturnEase(progress) : 1.0f;

	float verticalWeight = max(0.0f, cameraResetVerticalTime);
	float horizontalWeight = max(0.0f, cameraResetHorizontalTime);
	if (verticalWeight + horizontalWeight <= 0.0001f) {
		verticalWeight = 1.0f;
		horizontalWeight = 1.0f;
	}

	// A zero share means that axis needs no time and is already back.
	if (vertical && verticalWeight <= 0.0f)
		return 1.0f;
	if (!vertical && horizontalWeight <= 0.0f)
		return 1.0f;

	const float verticalPart = verticalWeight / (verticalWeight + horizontalWeight);
	const float firstPart = (cameraResetOrder == 1) ? verticalPart : (1.0f - verticalPart);
	const bool first = (cameraResetOrder == 1) ? vertical : !vertical;

	// Split the single smoothstep instead of easing each half separately. Easing
	// each half would come to a stop at the hand-off, so the second axis would
	// appear to wait before starting. With this split the second axis starts on
	// the same frame the first reaches its target, with no extra pause.
	const float global = CameraFreeLookReturnEase(progress);
	if (first)
		return clamp((firstPart > 0.0001f) ? global / firstPart : 1.0f, 0.0f, 1.0f);
	return clamp((firstPart < 0.9999f) ? (global - firstPart) / (1.0f - firstPart) : 1.0f, 0.0f, 1.0f);
}

// Per-camera free-look return state. Every engine used to keep the same seven
// statics and repeat the same begin/advance/blend logic; this owns that state so
// the engines only decide when the return starts and where it should end.
struct CameraFreeLookReturn
{
	bool wasActive = false;
	bool returning = false;
	float progress = 0.0f;
	float startBeta = 0.0f;
	float startAlpha = 0.0f;
	float targetBeta = 0.0f;
	float targetAlpha = 0.0f;
	float betaEase = 0.0f;
	float alphaEase = 0.0f;

	// Full reset, used when (re)entering a vehicle or switching camera mode.
	void Reset() {
		wasActive = false;
		Cancel();
		progress = 0.0f;
		betaEase = 0.0f;
		alphaEase = 0.0f;
	}

	// Abandon the current return without touching the tracking state.
	void Cancel() {
		returning = false;
	}

	// Start a return from the current angles to the frozen follow targets.
	void Begin(float beta, float alpha, float targetB, float targetA) {
		returning = true;
		progress = 0.0f;
		betaEase = 0.0f;
		alphaEase = 0.0f;
		startBeta = beta;
		startAlpha = alpha;
		targetBeta = targetB;
		targetAlpha = targetA;
	}

	// Advance by one frame and recompute both axis eases.
	void Advance() {
		if (!returning)
			return;

		const bool verticalMoves = fabsf(targetAlpha - startAlpha) > 0.0001f;
		const bool horizontalMoves =
			fabsf(LimitRadianAngle(targetBeta - startBeta)) > 0.0001f;

		progress += (ms_fTimeStep * TimeStepToSeconds) / CameraFreeLookReturnDuration();
		betaEase = CameraFreeLookAxisEase(progress, false, verticalMoves, horizontalMoves);
		alphaEase = CameraFreeLookAxisEase(progress, true, verticalMoves, horizontalMoves);
	}

	bool Finished() const {
		return returning && progress >= 1.0f;
	}

	float Beta() const {
		return LimitRadianAngle(startBeta +
			LimitRadianAngle(targetBeta - startBeta) * betaEase);
	}

	float Alpha() const {
		return startAlpha + (targetAlpha - startAlpha) * alphaEase;
	}
};

// RwCamera near clip; defined in ModernCarCam.cpp.
void* RwCameraSetNearClipPlane(void* camera, float nearClip);

// ---------------------------------------------------------------------------
// Settings / profiles / debug menu entry points
// ---------------------------------------------------------------------------

void applyProfile(ModernProfile profile, bool vc);
void LoadSettings();
void OnGInputSettingsReload();
void registerDebugMenu();          // III/VC wrapper: registers the menu, then runs the game's debug-text-buffer init
void registerDebugMenuEntries();   // one-time registration into debugmenu.dll (all three games)
void syncDebugMenuShadows();       // refresh the menu's unit shadows from the engine values
void onMasterProfileChange(void);
void InitVanillaLookHooks(bool vc, bool iii);   // takes over the III/VC native look for SmoothSideView / keyboard free-look

// Rebuilds the mutable Custom camera tables from the customDist*/customAngle*
// scalars. LoadSettings() calls it once; the debug menu calls it when a Custom
// value is edited so the change reaches the camera without an ini reload.
void buildCustomCameraTables();

// ---------------------------------------------------------------------------
// Camera engine entry points
//
// The argument order is uniform across all engines:
//   (TheCamera, cam, car, CameraTarget, TargetOrientation).
// ColModelClass is only templated where the collision model's bounding box is
// needed (the behind-boat and cam-on-a-string engines do not require it).
// ---------------------------------------------------------------------------

template<class CamClass, class CameraClass, class VehicleClass, class WorldClass>
void Process_BehindBoat_Vanilla(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation);

template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void Process_BehindBoat_VC(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation);

template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void Process_Cam_On_A_String_Vanilla(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation);

template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void Process_FollowCar_SA(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation);
