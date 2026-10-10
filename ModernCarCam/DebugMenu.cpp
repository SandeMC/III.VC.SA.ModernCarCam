#include "common.h"
#include "debugmenu_public.h"
#include "ModernCarCam.h"
#include <stdio.h>

// ---------------------------------------------------------------------------
// DebugMenu.cpp - the ModernCarCam section of aap's debug menu.
//
// The menu itself is debugmenu.dll, loaded on demand by DebugMenuLoad(); this
// unit only describes the entries and keeps them in step with the engine. The
// menu edits the variables in place, so the effects, offsets and camera knobs
// (all read every frame) are live the moment they change.
//
// A handful of ini values use friendlier units than the engine stores (angles
// are radians, the speed thresholds live on GTA's 0-180 speed scale). Those are
// shown through a shadow variable in the ini's own unit, with a trigger that
// writes the engine value back, so the menu numbers match the ini. The shadows
// are refreshed when the menu is registered and again on a profile change,
// because applyProfile() resets the engine values underneath them.
//
// registerDebugMenuEntries() runs once: from the game's debug-text-buffer init
// on GTA III / Vice City, and from the game's startup on San Andreas, which has
// no reversed debug-menu call site (see ModernCarCam.cpp).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Menu shadows (the value shown in the menu, in the ini's unit)
// ---------------------------------------------------------------------------

// [Features] KeepCameraOverWater is the inverse of the engine's seeUnderwater.
static bool dbgKeepCameraOverWater;

// Angles: degrees in the ini, radians in the engine. The two pitch limits use
// -1 to mean "keep the profile default".
static float dbgPitchTiltMinAngle;   // degrees
static float dbgPitchAngleMax;       // degrees, <0 = profile default
static float dbgPitchAngleMin;       // degrees, <0 = profile default
static float dbgCustomAngleNear;     // degrees
static float dbgCustomAngleMid;      // degrees
static float dbgCustomAngleFar;      // degrees

// Speeds: km/h in the ini, the 0-180 scale in the engine. <0 = profile default.
static float dbgDynamicFOVStartSpeed; // km/h
static float dbgVCSShakeStartSpeed;   // km/h
static float dbgTrafficMinSpeed;      // km/h
static float dbgTrafficFullSpeed;     // km/h

// <0 keeps the sentinel the engine reads as "use the profile default".
static inline float sentinelAngleToMenu(float radians) { return radians < 0.0f ? -1.0f : RADTODEG(radians); }
static inline float sentinelAngleFromMenu(float degrees) { return degrees < 0.0f ? -1.0f : DEGTORAD(degrees); }
static inline float sentinelSpeedToMenu(float scale) { return scale < 0.0f ? -1.0f : scale * SpeedKphFactor; }
static inline float sentinelSpeedFromMenu(float kph) { return kph < 0.0f ? -1.0f : kph / SpeedKphFactor; }

void syncDebugMenuShadows(void)
{
	dbgKeepCameraOverWater = !seeUnderwater;

	dbgPitchTiltMinAngle = RADTODEG(pitchTiltMinAngle);
	dbgPitchAngleMax = sentinelAngleToMenu(maxPitchAngle);
	dbgPitchAngleMin = sentinelAngleToMenu(minPitchAngle);
	dbgCustomAngleNear = RADTODEG(customAngleNear);
	dbgCustomAngleMid = RADTODEG(customAngleMid);
	dbgCustomAngleFar = RADTODEG(customAngleFar);

	dbgDynamicFOVStartSpeed = sentinelSpeedToMenu(dynamicSpeedFOVStartSpeed);
	dbgVCSShakeStartSpeed = sentinelSpeedToMenu(vcsCamShakeStartSpeed);
	dbgTrafficMinSpeed = sentinelSpeedToMenu(trafficCamWobbleMinSpeed);
	dbgTrafficFullSpeed = sentinelSpeedToMenu(trafficCamWobbleFullSpeed);
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

// debugMenuLoaded: 0 = not tried, 1 = debugmenu.dll missing, 2 = registered.
static int debugMenuLoaded = 0;

void registerDebugMenuEntries(void)
{
	if (debugMenuLoaded)
		return;
	if (!DebugMenuLoad()) {
		debugMenuLoaded = 1;
		return;
	}
	debugMenuLoaded = 2;

	syncDebugMenuShadows();

	// The profile is the one entry that rebuilds the whole feature set.
	DebugMenuAddInt8("ModernCarCam", "Camera profile", (int8_t*)&cameraProfile, onMasterProfileChange, 1, 0, 8, profileNames);

	// Advanced: the per-aspect table overrides from the ini's [General] section.
	// These select which table the camera reads; they do not rebuild anything,
	// so they take effect immediately and leave the feature values alone.
	{
		static const char *tableNames[] = { "SA/IV", "LCS/VCS", "Vanilla", "Custom" };
		DebugMenuAddInt8("ModernCarCam|Tables", "Distance table", (int8_t*)&distanceProfile, nil, 1, 0, 3, tableNames);
		DebugMenuAddInt8("ModernCarCam|Tables", "FOV table", (int8_t*)&fovProfile, nil, 1, 0, 3, tableNames);
		DebugMenuAddInt8("ModernCarCam|Tables", "Angles table", (int8_t*)&anglesProfile, nil, 1, 0, 3, tableNames);
	}

	// --- Effects (0 = off, 1 = the profile default, higher scales it) ---
	DebugMenuAddVar("ModernCarCam|Effects", "Camera wobble (x)", &cameraWobble, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Effects", "Elastic string (x)", &elasticStringPhysics, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Effects", "Pitch slope tilt (x)", &pitchTilt, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Effects", "Dynamic speed FOV (x)", &dynamicSpeedFOV, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Effects", "VCS camera shake (x)", &vcsCamShake, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Effects", "Traffic pass-by wobble (x)", &trafficCamWobble, nil, 0.05f, 0.0f, 5.0f);

	// --- Slope tilt ---
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Uphill strength (x, -1=def)", &pitchTiltUphill, nil, 0.05f, -1.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Downhill strength (x, -1=def)", &pitchTiltDownhill, nil, 0.05f, -1.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Min slope angle (deg)", &dbgPitchTiltMinAngle,
		[]() { pitchTiltMinAngle = DEGTORAD(dbgPitchTiltMinAngle); }, 0.5f, 0.0f, 45.0f);
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Air hold time (s)", &pitchTiltAirHoldTime, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Air blend time (s)", &pitchTiltAirBlendTime, nil, 0.05f, 0.0f, 10.0f);
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Max pitch (deg, -1=def)", &dbgPitchAngleMax,
		[]() { maxPitchAngle = sentinelAngleFromMenu(dbgPitchAngleMax); }, 1.0f, -1.0f, 89.0f);
	DebugMenuAddVar("ModernCarCam|Pitch tilt", "Min pitch (deg, -1=def)", &dbgPitchAngleMin,
		[]() { minPitchAngle = sentinelAngleFromMenu(dbgPitchAngleMin); }, 1.0f, -1.0f, 89.0f);

	// --- Speed-driven effects ---
	DebugMenuAddVar("ModernCarCam|Speed effects", "FOV start speed (km/h, -1=def)", &dbgDynamicFOVStartSpeed,
		[]() { dynamicSpeedFOVStartSpeed = sentinelSpeedFromMenu(dbgDynamicFOVStartSpeed); }, 5.0f, -1.0f, 300.0f);
	DebugMenuAddVar("ModernCarCam|Speed effects", "FOV max extra (deg, -1=def)", &dynamicSpeedFOVMaxFOV, nil, 1.0f, -1.0f, 90.0f);
	DebugMenuAddVar("ModernCarCam|Speed effects", "FOV decay (per step)", &dynamicSpeedFOVDecay, nil, 0.01f, 0.0f, 1.0f);
	DebugMenuAddVar("ModernCarCam|Speed effects", "Shake start speed (km/h, -1=def)", &dbgVCSShakeStartSpeed,
		[]() { vcsCamShakeStartSpeed = sentinelSpeedFromMenu(dbgVCSShakeStartSpeed); }, 5.0f, -1.0f, 300.0f);
	DebugMenuAddVar("ModernCarCam|Speed effects", "Traffic wobble min (km/h)", &dbgTrafficMinSpeed,
		[]() { trafficCamWobbleMinSpeed = sentinelSpeedFromMenu(dbgTrafficMinSpeed); }, 5.0f, -1.0f, 300.0f);
	DebugMenuAddVar("ModernCarCam|Speed effects", "Traffic wobble full (km/h)", &dbgTrafficFullSpeed,
		[]() { trafficCamWobbleFullSpeed = sentinelSpeedFromMenu(dbgTrafficFullSpeed); }, 5.0f, -1.0f, 300.0f);
	DebugMenuAddVar("ModernCarCam|Speed effects", "Traffic wobble max (x)", &trafficCamWobbleMaxMultiplier, nil, 0.1f, 0.0f, 10.0f);

	// --- Camera behaviour ---
	DebugMenuAddVar("ModernCarCam|Camera", "Anchoring (x)", &cameraAnchoring, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Camera", "Stiffness (x)", &cameraStiffness, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Camera", "Heading follow (x)", &headingFollow, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Camera", "Vehicle-specific zoom (x)", &vehicleSpecificZoom, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Camera", "Reverse cam delay (s)", &reverseCamDelay, nil, 0.05f, 0.0f, 10.0f);
	DebugMenuAddVar("ModernCarCam|Camera", "Free-look return speed (x)", &cameraReturnSpeed, nil, 0.05f, 0.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Camera", "Free-look return time (s)", &cameraReturnTime, nil, 0.05f, 0.0f, 5.0f);

	// --- Look and shooting ---
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Mouse free-look", (int8*)&mouseFreeLook, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Keyboard free-look", (int8*)&keyboardFreeLook, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Smooth side view", (int8*)&smoothSideView, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Reverse look-behind camera", (int8*)&reverseCam, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Modern drive-by", (int8*)&modernDriveBy, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Lock shot dir (KBM)", (int8*)&lockShootDirKBM, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Lock shot dir (pad)", (int8*)&lockShootDirJOY, nil);
	DebugMenuAddVarBool8("ModernCarCam|Look & shooting", "Modern turret control", (int8*)&modernTurretControl, nil);

	// --- Fixes ---
	DebugMenuAddVarBool8("ModernCarCam|Fixes", "Fix camera clipping", (int8*)&fixTheBug, nil);
	DebugMenuAddVarBool8("ModernCarCam|Fixes", "Keep camera over water", (int8*)&dbgKeepCameraOverWater,
		[]() { seeUnderwater = !dbgKeepCameraOverWater; });
	DebugMenuAddVarBool8("ModernCarCam|Fixes", "Bikes cam raise (passenger)", (int8*)&heightIncreaseOnBike, nil);

	// --- Offsets (applied on top of any profile) ---
	DebugMenuAddVar("ModernCarCam|Offsets", "Height offset (m)", &cameraHeightOffset, nil, 0.05f, -5.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Lateral offset (m)", &cameraLateralOffset, nil, 0.05f, -5.0f, 5.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Distance offset near (m)", &cameraDistanceOffsetNear, nil, 0.05f, -10.0f, 10.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Distance offset mid (m)", &cameraDistanceOffsetMid, nil, 0.05f, -10.0f, 10.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Distance offset far (m)", &cameraDistanceOffsetFar, nil, 0.05f, -10.0f, 10.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Min distance (m, -1=def)", &cameraMinDistance, nil, 0.05f, -1.0f, 20.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Distance scale (x)", &cameraDistanceScale, nil, 0.05f, 0.0f, 3.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Driver offset X", &cameraDriverOffset.x, nil, 0.05f, -3.0f, 3.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Driver offset Y", &cameraDriverOffset.y, nil, 0.05f, -3.0f, 3.0f);
	DebugMenuAddVar("ModernCarCam|Offsets", "Driver offset Z", &cameraDriverOffset.z, nil, 0.05f, -3.0f, 3.0f);

	// --- GTA III top-down camera enhancement ---
	DebugMenuAddVarBool8("ModernCarCam|Top-down", "Enhance top-down", (int8*)&topDownEnhance, nil);
	DebugMenuAddVar("ModernCarCam|Top-down", "Distance", &topDownDistance, nil, 0.5f, -1.0f, 60.0f);
	DebugMenuAddVar("ModernCarCam|Top-down", "Distance scale", &topDownDistanceScale, nil, 0.05f, 0.1f, 4.0f);
	DebugMenuAddVar("ModernCarCam|Top-down", "Clearance (m)", &topDownClearance, nil, 0.5f, 0.0f, 60.0f);

	// --- Custom camera shape (Profile = Custom) ---
	DebugMenuAddVar("ModernCarCam|Custom camera", "Distance near (m)", &customDistNear,
		[]() { buildCustomCameraTables(); }, 0.05f, -20.0f, 30.0f);
	DebugMenuAddVar("ModernCarCam|Custom camera", "Distance mid (m)", &customDistMid,
		[]() { buildCustomCameraTables(); }, 0.05f, -20.0f, 30.0f);
	DebugMenuAddVar("ModernCarCam|Custom camera", "Distance far (m)", &customDistFar,
		[]() { buildCustomCameraTables(); }, 0.05f, -20.0f, 30.0f);
	DebugMenuAddVar("ModernCarCam|Custom camera", "Base FOV (deg)", &customBaseFOV, nil, 1.0f, 20.0f, 160.0f);
	DebugMenuAddVar("ModernCarCam|Custom camera", "Angle near (deg)", &dbgCustomAngleNear,
		[]() { customAngleNear = DEGTORAD(dbgCustomAngleNear); buildCustomCameraTables(); }, 0.5f, -89.0f, 89.0f);
	DebugMenuAddVar("ModernCarCam|Custom camera", "Angle mid (deg)", &dbgCustomAngleMid,
		[]() { customAngleMid = DEGTORAD(dbgCustomAngleMid); buildCustomCameraTables(); }, 0.5f, -89.0f, 89.0f);
	DebugMenuAddVar("ModernCarCam|Custom camera", "Angle far (deg)", &dbgCustomAngleFar,
		[]() { customAngleFar = DEGTORAD(dbgCustomAngleFar); buildCustomCameraTables(); }, 0.5f, -89.0f, 89.0f);
}
