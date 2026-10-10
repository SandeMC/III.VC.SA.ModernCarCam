#include "ModernCarCam.h"

// ---------------------------------------------------------------------------
// Profiles.cpp - the camera profile model and the per-game camera tables.
//
// applyProfile() maps the user-facing ModernProfile onto the internal
// CameraProfileType table selectors and sets the default feature flags. The
// tables below are the authentic zoom/angle tables of SA, LCS, GTA III and
// Vice City; the Custom set is overwritten by LoadSettings() from the ini.
// ---------------------------------------------------------------------------

// Original values from SA / LCS / VC / III
const float CarZoomModesSA[] = {
	-1.0f, -0.2f, -3.2f, 0.05f, -2.41f, // near
	1.0f, 1.4f, 0.65f, 1.9f, 6.49f, // mid
	6.0f, 6.0f, 15.9f, 15.9f, 15.0f // far
};

const float CarZoomModesLCS[] = {
	-1.0f, -0.2f, -3.2f, 0.05f, -2.41f, // near
	2.0f, 2.2f, 1.65f, 2.9f, 6.49f, // mid
	6.0f, 6.0f, 15.9f, 15.9f, 15.0f // far
};

const float CarZoomModesVC[] = {
	-0.6f, 0.05f, -3.2f, 0.05f, -2.41f, // near
	1.9f, 1.4f, 0.65f, 1.9f, 6.49f,     // mid
	15.9f, 15.9f, 15.9f, 15.9f, 25.25f  // far
};

const float CarZoomModesIII[] = {
	0.05f, 0.05f, 0.05f, 0.05f, 0.05f, // near
	1.9f, 1.9f, 1.9f, 1.9f, 1.9f,       // mid
	3.9f, 3.9f, 3.9f, 3.9f, 3.9f        // far
};

// Alpha angles (up-down)
const float ZmOneAlphaOffset[] = { 0.08f, 0.08f, 0.15f, 0.08f, 0.08f };
const float ZmTwoAlphaOffset[] = { 0.07f, 0.08f, 0.3f, 0.08f, 0.08f };
const float ZmThreeAlphaOffset[] = { 0.055f, 0.05f, 0.15f, 0.06f, 0.08f };

const float ZmOneAlphaOffsetLCS[] = { 0.12f, 0.08f, 0.15f, 0.08f, 0.08f };
const float ZmTwoAlphaOffsetLCS[] = { 0.1f, 0.08f, 0.3f, 0.08f, 0.08f };
const float ZmThreeAlphaOffsetLCS[] = { 0.065f, 0.05f, 0.15f, 0.06f, 0.08f };

const float ZmOneAlphaOffsetVC[]   = { -0.01f, 0.10f, 0.125f, -0.10f, -0.06f }; // near (authentic Vice City)
const float ZmTwoAlphaOffsetVC[]   = {  0.045f, 0.12f, 0.045f,  0.045f, -0.035f }; // mid (authentic Vice City)
const float ZmThreeAlphaOffsetVC[] = {  0.005f, 0.005f, 0.15f,  0.005f,  0.12f }; // far (authentic Vice City)

const float ZmOneAlphaOffsetIII[]   = { -0.01f, 0.10f, 0.125f, -0.10f, -0.06f }; // near (authentic GTA III)
const float ZmTwoAlphaOffsetIII[]   = {  0.045f, 0.12f, 0.045f,  0.045f, -0.035f }; // mid (authentic GTA III)
const float ZmThreeAlphaOffsetIII[] = {  0.005f, 0.005f, 0.15f,  0.005f,  0.12f }; // far (authentic GTA III)

const float CARCAM_SET_SA[][15] = {
	{1.3f, 1.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.785398f, 1.5533431f},
	{1.1f, 1.0f, 0.1f, 10.0f, 11.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.75f, 0.78539819f, 1.5533431f},
	{1.1f, 1.0f, 0.2f, 10.0f, 15.0f, 0.05f, 0.05f, 0.0f, 0.9f, 0.05f, 0.01f, 0.05f, 1.0f, 0.17453294f, 1.2217305f},
	{1.1f, 3.5f, 0.2f, 10.0f, 25.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 1.5533431f, 1.5533431f},
	{1.3f, 1.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 0.0f, 0.9f, 0.05f, 0.005f, 0.05f, 1.0f, 0.34906587f, 1.2217305f},
	{1.1f, 1.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.78539819f, 1.5533431f}, // rc cars
	{1.1f, 1.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.34906587f, 1.2217305f}, // rc heli/planes
	{1.3f, 1.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.785398f, 1.5533431f}  // firetruck
};

const float CARCAM_SET_LCS[][15] = {
	{1.3f, 1.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.7854f, 1.5533f}, // cars
	{1.1f, 1.0f, 0.1f, 10.0f, 11.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.75f, 0.7854f, 1.5533f}, // bike
	{1.1f, 1.0f, 0.2f, 10.0f, 15.0f, 0.05f, 0.05f, 0.0f, 0.9f, 0.05f, 0.01f, 0.05f, 1.0f, 0.17453294f, 1.2217305f}, // heli (SA values)
	{1.1f, 3.5f, 0.2f, 10.0f, 25.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 1.5533431f, 1.5533431f}, // plane (SA values)
	{0.9f, 1.0f, 0.1f, 10.0f, 15.0f, 0.5f, 1.0f, 0.0f, 0.9f, 0.05f, 0.005f, 0.05f, 1.0f, -0.2f, 1.2217305f}, // boat
	{1.1f, 1.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.7854f, 1.5533f}, // rc cars
	{1.1f, 1.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.34906587f, 1.2217305f}, // rc heli/planes
	{1.3f, 1.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, -0.18f, 0.7f}, // firetruck...
};

const float CARCAM_SET_VANILLA[][15] = {
	{1.3f, 0.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.785398f, 1.5533431f}, // cars
	{1.1f, 0.0f, 0.1f, 10.0f, 11.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.75f, 0.785398f, 1.5533431f}, // bikes
	{1.1f, 0.0f, 0.2f, 10.0f, 15.0f, 0.05f, 0.05f, 0.0f, 0.9f, 0.05f, 0.01f, 0.05f, 1.0f, 0.17453294f, 1.2217305f}, // helis
	{1.1f, 0.0f, 0.2f, 10.0f, 25.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 1.5533431f, 1.5533431f}, // planes
	{1.3f, 0.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 0.0f, 0.9f, 0.05f, 0.005f, 0.05f, 1.0f, 0.34906587f, 1.2217305f}, // boats
	{1.1f, 0.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.78539819f, 1.5533431f}, // rc cars
	{1.1f, 0.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.34906587f, 1.2217305f}, // rc heli
	{1.3f, 0.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.785398f, 1.5533431f}  // firetruck
};

float CARCAM_SET_CUSTOM[8][15] = {
	{1.3f, 0.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.785398f, 1.5533431f}, // cars
	{1.1f, 0.0f, 0.1f, 10.0f, 11.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.75f, 0.785398f, 1.5533431f}, // bikes
	{1.1f, 0.0f, 0.2f, 10.0f, 15.0f, 0.05f, 0.05f, 0.0f, 0.9f, 0.05f, 0.01f, 0.05f, 1.0f, 0.17453294f, 1.2217305f}, // helis
	{1.1f, 0.0f, 0.2f, 10.0f, 25.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 1.5533431f, 1.5533431f}, // planes
	{1.3f, 0.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 0.0f, 0.9f, 0.05f, 0.005f, 0.05f, 1.0f, 0.34906587f, 1.2217305f}, // boats
	{1.1f, 0.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.78539819f, 1.5533431f}, // rc cars
	{1.1f, 0.0f, 0.2f, 10.0f, 5.0f, 0.5f, 1.0f, 1.0f, 0.75f, 0.1f, 0.005f, 0.2f, 1.0f, 0.34906587f, 1.2217305f}, // rc heli
	{1.3f, 0.0f, 0.4f, 10.0f, 15.0f, 0.5f, 1.0f, 1.0f, 0.85f, 0.2f, 0.075f, 0.05f, 0.8f, 0.785398f, 1.5533431f}  // firetruck
};

float CarZoomModesCustom[15] = {
	0.05f, 0.05f, 0.05f, 0.05f, 0.05f, // near
	1.9f, 1.9f, 1.9f, 1.9f, 1.9f,       // mid
	3.9f, 3.9f, 3.9f, 3.9f, 3.9f        // far
};

float ZmOneAlphaOffsetCustom[5]   = { -0.01f, 0.10f, 0.125f, -0.10f, -0.06f };
float ZmTwoAlphaOffsetCustom[5]   = {  0.045f, 0.12f, 0.045f,  0.045f, -0.035f };
float ZmThreeAlphaOffsetCustom[5] = {  0.005f, 0.005f, 0.15f,  0.005f,  0.12f };

const float TiltOverShoot[] = { 1.05f, 1.05f, 0.0f, 0.0f, 1.0f };
const float TiltTopSpeed[]  = { 0.035f, 0.035f, 0.001f, 0.005f, 0.035f };
const float TiltSpeedStep[] = { 0.016f, 0.016f, 0.0002f, 0.0014f, 0.016f };

// Resolve the user-facing profile into the internal table selectors and the
// default feature set. Only the free camera, free turret control and fixes are
// added on top of the game-matched camera; everything else is opt-in.
void applyProfile(ModernProfile profile, bool vc) {
	const bool sa = isSA();

	switch (profile) {
	case PROFILE_SA_CAM:
	case PROFILE_IV:
		masterProfile = PROFILE_SA;
		break;
	case PROFILE_LCS_CAM:
	case PROFILE_VCS:
		masterProfile = PROFILE_LCS;
		break;
	case PROFILE_CUSTOM_CAM:
		masterProfile = PROFILE_CUSTOM;
		break;
	default:
		// Game-Matched on San Andreas uses the SA follow camera (SA's native
		// vehicle camera), not the III/VC "on a string" camera.
		masterProfile = (profile == PROFILE_GAME_MATCHED && sa) ? PROFILE_SA : PROFILE_VANILLA;
		break;
	}
	distanceProfile = masterProfile;
	fovProfile = masterProfile;
	anglesProfile = masterProfile;

	// Game-matched baseline: reproduce the running game and only add the free
	// camera, free turret control and fixes. Every strength is a multiplier:
	// 1.0 = the profile default, 0.0 = off.
	cameraWobble = vc ? 1.0f : 0.0f;  // VC has steering roll, III does not
	elasticStringPhysics = 0.0f;
	pitchTilt = 0.0f;                  // master: off unless the profile turns it on
	pitchTiltUphill = -1.0f;           // <0 = use the master value
	pitchTiltDownhill = -1.0f;
	// Slopes shallower than 2 degrees are ignored so flat roads do not jitter;
	// while airborne the last ground tilt is held, then eased to the car's nose.
	pitchTiltMinAngle = DEGTORAD(2.0f);
	pitchTiltAirHoldTime = 0.35f;
	pitchTiltAirBlendTime = 1.0f;
	dynamicSpeedFOV = 0.0f;            // not native to III/VC
	dynamicSpeedFOVStartSpeed = -1.0f; // <0 = profile default
	dynamicSpeedFOVMaxFOV = -1.0f;
	vcsCamShake = 0.0f;
	vcsCamShakeStartSpeed = -1.0f;
	cameraAnchoring = 1.0f;            // rigid anchor for the III/VC cameras
	cameraStiffness = 1.0f;            // multiply the profile's default stiffness
	headingFollow = 1.0f;              // multiplier on the SA follow camera's yaw follow
	vehicleSpecificZoom = vc ? 1.0f : 0.0f;
	modernTurretControl = true;
	modernDriveBy = true;
	lockShootDirKBM = true;
	lockShootDirJOY = true;
	smoothSideView = false;            // vanilla instant look; Enhanced enables the smooth side view
	keyboardFreeLook = sa;             // keyboard drives the free camera in SA (native), not in III/VC
	topDownEnhance = false;            // the GTA III top-down enhancement is opt-in
	mouseFreeLook = true;
	heightIncreaseOnBike = vc;
	fixTheBug = true;
	trafficCamWobble = 0.0f;           // a mod effect: not part of any vanilla camera, opt-in
	trafficCamWobbleMinSpeed = 0.15f;  // forward speed (m/tick) needed to trigger a pass-by nudge
	trafficCamWobbleFullSpeed = 0.5f;  // speed at which the nudge reaches its max multiplier
	trafficCamWobbleMaxMultiplier = 2.0f; // nudge strength at full speed (before TrafficCamWobble)
	reverseCamDelay = 0.25f;           // seconds of reversing before the reverse camera swings
	dynamicSpeedFOVDecay = 0.98f;      // per-step FOV decay (Enhanced/SA feel)
	reverseCam = false;                // quality-of-life: Enhanced only, or forced in the ini
	seeUnderwater = false;
	cameraReturnSpeed = 1.0f;
	cameraReturnTime = 0.5f;           // seconds for the free-look auto-return
	cameraHeightOffset = 0.0f;
	cameraLateralOffset = 0.0f;
	cameraDistanceOffsetNear = 0.0f;
	cameraDistanceOffsetMid = 0.0f;
	cameraDistanceOffsetFar = 0.0f;
	cameraMinDistance = -1.0f;         // <0 = profile default
	cameraDistanceScale = 1.0f;
	cameraDriverOffset = CVector(0.0f, 0.0f, 0.0f);
	enhancedVC = false;

	// Vice City's game-matched camera reacts to the terrain downhill.
	if (vc) {
		pitchTilt = 1.0f;
		pitchTiltUphill = 0.0f;        // authentic VC: downhill only
	}

	if (sa) {
		// San Andreas' native camera aspects. The follow camera floats (no
		// anchor), uses its own stiffness, its per-vehicle zoom and bike-passenger
		// height, and has no III/VC steering wobble or slope tilt. Free look is
		// handled by the mod's camera on SA too (see CamSA.cpp), so the toggle
		// stays on. These are the defaults; the profile below can still turn its
		// own features on.
		cameraAnchoring = 0.0f;
		cameraStiffness = 1.0f;
		cameraWobble = 0.0f;
		pitchTilt = 0.0f;              // match game -> no slope tilt on SA
		pitchTiltUphill = -1.0f;
		pitchTiltDownhill = -1.0f;
		dynamicSpeedFOV = 0.0f;
		vehicleSpecificZoom = 1.0f;
		heightIncreaseOnBike = true;
		mouseFreeLook = true;
	}

	switch (profile) {
	case PROFILE_ENHANCED:
		// Game-matched camera plus quality-of-life additions.
		elasticStringPhysics = 1.0f;
		dynamicSpeedFOV = 1.0f;
		pitchTilt = 1.0f;              // both directions (uphill/downhill follow the master)
		pitchTiltUphill = -1.0f;
		pitchTiltDownhill = -1.0f;
		// Enhanced uses the VCS shake at half strength so it stays a subtle
		// quality-of-life touch rather than the full VCS feel.
		vcsCamShake = 0.5f;
		trafficCamWobble = 1.0f;       // the shipped gentle lean (70% of the raw effect)
		smoothSideView = true;         // smooth side view instead of the vanilla instant look
		lockShootDirKBM = false;       // keyboard/mouse drive-by re-aims freely
		lockShootDirJOY = true;        // gamepad drive-by keeps the burst direction
		{
			// Enhanced is the Vice City camera with the modern extras: adopt the
			// Vice City feature flags, camera angles, anchor and stiffness even in
			// GTA III, while the III-only engine behaviour (roof/ground height,
			// top-down camera, reversed turret) is left as-is.
			cameraWobble = 1.0f;
			vehicleSpecificZoom = 1.0f;
			heightIncreaseOnBike = true;
			cameraAnchoring = 1.0f;    // authentic III/VC rigid anchor
			cameraStiffness = 1.0f;    // authentic vanilla stiffness
			reverseCam = true;
			enhancedVC = true;
		}
		break;
	case PROFILE_III:
		// The GTA III camera has no steering wobble, no per-vehicle zoom table
		// and no bike-passenger height; pin them to the III camera even when the
		// profile is forced while Vice City is running. GTA III pitches the camera
		// downhill but not uphill, like Vice City.
		cameraWobble = 0.0f;
		pitchTilt = 1.0f;
		pitchTiltUphill = 0.0f;        // authentic GTA III: downhill only
		vehicleSpecificZoom = 0.0f;
		heightIncreaseOnBike = false;
		break;
	case PROFILE_VC:
		// Vice City's own camera feature set, also when forced on GTA III.
		cameraWobble = 1.0f;
		pitchTilt = 1.0f;
		pitchTiltUphill = 0.0f;        // authentic VC: downhill only
		vehicleSpecificZoom = 1.0f;
		heightIncreaseOnBike = true;
		break;
	case PROFILE_VCS:
		vcsCamShake = 1.0f;
		cameraWobble = 0.0f;
		pitchTilt = 0.0f;
		vehicleSpecificZoom = 1.0f;
		heightIncreaseOnBike = true;
		cameraAnchoring = 0.0f;
		dynamicSpeedFOV = 1.0f;
		break;
	case PROFILE_SA_CAM:
		// Authentic San Andreas camera with the mod's fixes: no steering wobble,
		// no slope tilt and no dynamic speed FOV.
		cameraWobble = 0.0f;
		pitchTilt = 0.0f;
		vehicleSpecificZoom = 1.0f;
		heightIncreaseOnBike = true;
		cameraAnchoring = 0.0f;
		dynamicSpeedFOV = 0.0f;
		break;
	case PROFILE_LCS_CAM:
		// Liberty City Stories has no steering wobble or slope tilt either.
		cameraWobble = 0.0f;
		pitchTilt = 0.0f;
		vehicleSpecificZoom = 1.0f;
		heightIncreaseOnBike = true;
		cameraAnchoring = 0.0f;
		dynamicSpeedFOV = 1.0f;
		break;
	case PROFILE_IV:
		// IV has no steering wobble, but it does pitch with the terrain in both
		// directions, so enable the slope tilt symmetrically (uphill and downhill
		// at the master strength). Its camera has a light elastic stretch and a
		// subtle speed FOV, both lighter than the Enhanced profile's. It stays on
		// the SA follow camera (cameraAnchoring 0): the elastic string is applied
		// there too, and the on-a-string engine sat the IV camera too high on III.
		cameraWobble = 0.0f;
		pitchTilt = 1.0f;
		pitchTiltUphill = -1.0f;
		pitchTiltDownhill = -1.0f;
		vehicleSpecificZoom = 1.0f;
		heightIncreaseOnBike = true;
		cameraAnchoring = 0.0f;
		// GTA III's follow is far more eager than Vice City's, so the IV profile
		// runs it at half strength there to hold the yaw into gentle turns like VC
		// does; Vice City keeps the full follow.
		headingFollow = isIII() ? 0.5f : 1.0f;
		elasticStringPhysics = 0.5f;   // lighter than the Enhanced profile's 1.0
		dynamicSpeedFOV = 0.5f;        // subtle speed FOV (half of Enhanced)
		dynamicSpeedFOVDecay = 0.90f;  // snaps back quickly when slowing down
		// GTA IV keeps the driver's seat centred on screen, so the orbit target
		// is the driver's seat (left, forward, up from the car centre). The height
		// was lowered because the IV profile sat too high in GTA III; fine-tune
		// with [Offsets] CameraHeightOffset.
		cameraDriverOffset = CVector(-0.32f, 0.20f, 0.30f);
		cameraDistanceScale = 0.85f;
		break;
	default:
		break;
	}

	// Enhanced on San Andreas enables the same features it does in GTA III, but
	// the SA camera keeps its own anchor, stiffness, distance, FOV and angles.
	if (sa && profile == PROFILE_ENHANCED) {
		masterProfile = PROFILE_SA;
		distanceProfile = PROFILE_SA;
		fovProfile = PROFILE_SA;
		anglesProfile = PROFILE_SA;
		cameraAnchoring = 0.0f;
		cameraStiffness = 1.0f;
		// The SA camera keeps its own steering feel (no VC steering wobble), but
		// the VCS high-speed shake is wanted on SA's Enhanced profile, at the same
		// half strength the Enhanced profile uses elsewhere.
		cameraWobble = 0.0f;
		vcsCamShake = 0.5f;
	}
}

const char *profileNames[] = { "Game", "III", "VC", "SA", "Enhanced", "LCS", "VCS", "IV", "Custom" };
