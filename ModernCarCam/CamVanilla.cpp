#include "ModernCarCam.h"

// ---------------------------------------------------------------------------
// CamVanilla.cpp - the authentic GTA III / Vice City vehicle cameras.
//
// Three engines live here:
//
//   Process_BehindBoat_Vanilla  the shared GTA III / Vice City behind-boat cam
//   Process_BehindBoat_VC       the Vice City-only behind-boat cam (reVC)
//   Process_Cam_On_A_String_Vanilla  the vehicle "camera on a string" cam
//
// The implementations follow the reversed sources of re3 (GTA III) and reVC
// (Vice City), using the https://github.com/Hezkore/hez-gta-re3 fork; see
// licenses/re3.txt. GTA III and Vice City implement Process_Cam_On_A_String
// differently, so the two algorithms are kept separate. The mod's ini-driven
// features are layered on top of the authentic behaviour.
// ---------------------------------------------------------------------------

namespace {

// --- Vanilla on-a-string tunables (values from re3 / reVC) -----------------

// Distance/alpha tables are indexed by the vehicle class: car, bike, heli,
// plane, boat (the RC classes reuse the car/heli rows via index remapping).
constexpr float SmallBoatCloseAlphaMinus = 0.2f;
constexpr float BoatBetaDiffMult[3] = { 0.15f, 0.07f, 0.01f };
constexpr float BoatBetaSpeedDiffMult[3] = { 0.02f, 0.015f, 0.005f };
constexpr float BoatWaterZAddition = 2.75f;
constexpr float BoatMaxHeightUp = 15.0f;

} // namespace

// ---------------------------------------------------------------------------
// Native look handling for III/VC.
//
// The game looks left/right/behind itself, in CCam::Process, by calling
// CCam::LookBehind / LookLeft / LookRight while the bound key is held. The mod
// drives look left/right/up/down as a free camera from the game's own look and
// turret keys (see the free-look block below), so the native left/right snap is
// swallowed while free-look is on; look behind stays with the game. With the
// smooth side view on, the mod's smooth swing owns left/right/behind instead.
// ---------------------------------------------------------------------------
typedef void(__fastcall* VanillaLookFn)(void*);

static VanillaLookFn gLookBehind = nullptr;
static VanillaLookFn gLookLeft = nullptr;
static VanillaLookFn gLookRight = nullptr;

// True while the mod's free-look owns look left/right. Set each frame by
// Process_Cam_On_A_String_Vanilla, which runs before CCam::Process calls the
// native look, so the game's own side snap cannot fight the free camera.
static bool gFreeLookOwnsLR = false;

static void __fastcall HookLookBehind(void* cam) {
	// Look behind stays with the game unless the smooth side view owns it.
	if (!smoothSideView && gLookBehind)
		gLookBehind(cam);
}

static void __fastcall HookLookLeft(void* cam) {
	// Free-look (or the smooth side view) owns look left/right: never snap.
	if (!gFreeLookOwnsLR && !smoothSideView && gLookLeft)
		gLookLeft(cam);
}

static void __fastcall HookLookRight(void* cam) {
	if (!gFreeLookOwnsLR && !smoothSideView && gLookRight)
		gLookRight(cam);
}

void InitVanillaLookHooks(bool vc, bool iii) {
    if (vc) {
        gLookBehind = (VanillaLookFn)0x4851C7; // CCam::LookBehind (VC 1.0)
        gLookLeft = (VanillaLookFn)0x484B00;   // CCam::LookLeft
        gLookRight = (VanillaLookFn)0x4843E5;  // CCam::LookRight
        InjectHook(0x483F92, &HookLookBehind, PATCH_NOTHING); // calls in CCam::Process
        InjectHook(0x483FDA, &HookLookLeft, PATCH_NOTHING);
        InjectHook(0x48400E, &HookLookRight, PATCH_NOTHING);
    } else if (iii) {
        gLookBehind = (VanillaLookFn)0x458600; // CCam::LookBehind (III 1.0)
        gLookLeft = (VanillaLookFn)0x458C40;   // CCam::LookLeft
        gLookRight = (VanillaLookFn)0x458FB0;  // CCam::LookRight
        InjectHook(0x459E7C, &HookLookBehind, PATCH_NOTHING);
        InjectHook(0x459EAC, &HookLookLeft, PATCH_NOTHING);
        InjectHook(0x459EDC, &HookLookRight, PATCH_NOTHING);
    }
}

// ---------------------------------------------------------------------------
// Vanilla "behind boat" camera (CCam::Process_BehindBoat).
// GTA III and Vice City share this implementation, so it is reproduced here
// as part of the vanilla preset.
// ---------------------------------------------------------------------------
template<class CamClass, class CameraClass, class VehicleClass, class WorldClass>
void
Process_BehindBoat_Vanilla(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation)
{
	static CColPoint colPoint;
	static float TargetWhenChecksWereOn = 0.0f;
	static float CenterObscuredWhenChecksWereOn = 0.0f;
	static const float WaterZAddition = 2.75f;
	static const float FixerForGoingBelowGround = 0.4f;
	static const float AmountUp = 2.2f;

	// The keyboard free-look is a car-camera feature; boats keep the game's own
	// look, so make sure a flag left set by the last car frame cannot swallow it.
	gFreeLookOwnsLR = false;
	if (!car->IsVehicle()) {
		cam->ResetStatics = false;
		return;
	}

	CVector TargetCoors = CameraTarget;
	float DeltaBeta = 0.0f;
	float WaterLevel = 0.0f;
	float s, c;

	cam->Beta = GetATanOfXY(TargetCoors.x - cam->Source.x, TargetCoors.y - cam->Source.y);
	cam->FOV = DefaultFOV;

	if (cam->ResetStatics) {
		CenterObscuredWhenChecksWereOn = 0.0f;
		TargetWhenChecksWereOn = 0.0f;
		cam->Beta = TargetOrientation + PI;
	}

	CWaterLevel::GetWaterLevelNoWaves(TargetCoors.x, TargetCoors.y, TargetCoors.z, &WaterLevel);
	WaterLevel += WaterZAddition;
	if (-FixerForGoingBelowGround < TargetCoors.z - WaterLevel)
		WaterLevel += TargetCoors.z - WaterLevel - FixerForGoingBelowGround;

	bool obscured;
	if (cam->m_bCollisionChecksOn || cam->ResetStatics) {
		const float zoom = TheCamera->CarZoomValueSmooth;
		CVector testPoint;

		c = cosf(TargetOrientation); s = sinf(TargetOrientation);
		testPoint = zoom * CVector(-c, -s, 0.0f) + (zoom + 7.0f) * CVector(-c, -s, 0.0f) + TargetCoors;
		testPoint.z = WaterLevel + zoom;
		const bool test1 = WorldClass::GetIsLineOfSightClear(testPoint, TargetCoors, true, false, false, true, false, true, true);

		c = cosf(TargetOrientation + 0.8f); s = sinf(TargetOrientation + DEGTORAD(40.0f));
		testPoint = zoom * CVector(-c, -s, 0.0f) + (zoom + 7.0f) * CVector(-c, -s, 0.0f) + TargetCoors;
		testPoint.z = WaterLevel + zoom;
		const bool test2 = WorldClass::GetIsLineOfSightClear(testPoint, TargetCoors, true, false, false, true, false, true, true);

		c = cosf(TargetOrientation - 0.8f); s = sinf(TargetOrientation - DEGTORAD(40.0f));
		testPoint = zoom * CVector(-c, -s, 0.0f) + (zoom + 7.0f) * CVector(-c, -s, 0.0f) + TargetCoors;
		testPoint.z = WaterLevel + zoom;
		const bool test3 = WorldClass::GetIsLineOfSightClear(testPoint, TargetCoors, true, false, false, true, false, true, true);

		if (!test2) {
			DeltaBeta = TargetOrientation - cam->Beta - DEGTORAD(40.0f);
			if (cam->ResetStatics)
				cam->Beta = TargetOrientation - DEGTORAD(40.0f);
		} else if (!test3) {
			DeltaBeta = TargetOrientation - cam->Beta + DEGTORAD(40.0f);
			if (cam->ResetStatics)
				cam->Beta = TargetOrientation + DEGTORAD(40.0f);
		} else if (!test1) {
			DeltaBeta = 0.0f;
		} else {
			if (cam->ResetStatics)
				cam->Beta = TargetOrientation;
			DeltaBeta = TargetOrientation - cam->Beta;
		}

		c = cosf(cam->Beta); s = sinf(cam->Beta);
		testPoint.x = zoom * -c + (zoom + 7.0f) * -c + TargetCoors.x;
		testPoint.y = zoom * -s + (zoom + 7.0f) * -s + TargetCoors.y;
		testPoint.z = WaterLevel + zoom;
		CEntity* entity = nil;
		obscured = WorldClass::ProcessLineOfSight(testPoint, TargetCoors, colPoint, entity, true, false, false, true, false, true, true);
		CenterObscuredWhenChecksWereOn = obscured ? 1.0f : 0.0f;

		TargetWhenChecksWereOn = DeltaBeta + cam->Beta;
	} else {
		obscured = CenterObscuredWhenChecksWereOn != 0.0f;
	}

	if (obscured) {
		CEntity* entity = nil;
		WorldClass::ProcessLineOfSight(cam->Source, TargetCoors, colPoint, entity, true, false, false, true, false, true, true);
		cam->Source = colPoint.point;
	} else {
		WellBufferMe(TargetWhenChecksWereOn, &cam->Beta, &cam->BetaSpeed, 0.07f, 0.015f, true);

		const float zoom = TheCamera->CarZoomValueSmooth;
		s = sinf(cam->Beta); c = cosf(cam->Beta);
		cam->Source = zoom * CVector(-c, -s, 0.0f) + (zoom + 7.0f) * CVector(-c, -s, 0.0f) + TargetCoors;
		cam->Source.z = WaterLevel + zoom;
	}

	if (TheCamera->CarZoomValueSmooth < 0.05f)
		TargetCoors.z += AmountUp * (0.0f - TheCamera->CarZoomValueSmooth);
	TargetCoors.z += TheCamera->CarZoomValueSmooth + 0.5f;

	cam->m_cvecTargetCoorsForFudgeInter = TargetCoors;
	cam->Front = TargetCoors - cam->Source;
	cam->GetVectorsReadyForRW();
	cam->ResetStatics = false;
}


// ---------------------------------------------------------------------------
// Vice City "behind boat" camera (reVC CCam::Process_BehindBoat).
// The Vice City boat camera differs from the GTA III one, so it is a
// separate implementation.
// ---------------------------------------------------------------------------
template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void
Process_BehindBoat_VC(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation)
{
	const float MAX_HEIGHT_UP = BoatMaxHeightUp;
	const float WATER_Z_ADDITION = BoatWaterZAddition;
	const float SMALLBOAT_CLOSE_ALPHA_MINUS = SmallBoatCloseAlphaMinus;

	static float WaterLevelBuffered = 0.0f;
	static float WaterLevelSpeed = 0.0f;

	// See Process_BehindBoat_Vanilla: boats keep the game's own look.
	gFreeLookOwnsLR = false;
	if (!car->IsVehicle()) {
		cam->ResetStatics = false;
		return;
	}

	CVector TargetCoors = CameraTarget;
	float WaterLevel = 0.0f;
	float betaDiffMult = 0.0f;
	float betaSpeedDiffMult = 0.0f;

	cam->Beta = GetATanOfXY(TargetCoors.x - cam->Source.x, TargetCoors.y - cam->Source.y);
	cam->FOV = DefaultFOV;
	float targetAlpha = 0.0f;

	if (!cam->ResetStatics && cam->DirectionWasLooking != LOOKING_FORWARD)
		cam->Beta = TargetOrientation;

	if (!CWaterLevel::GetWaterLevelNoWaves(TargetCoors.x, TargetCoors.y, TargetCoors.z, &WaterLevel))
		WaterLevel = TargetCoors.z - 0.5f;
	if (cam->ResetStatics) {
		WaterLevelBuffered = WaterLevel;
		WaterLevelSpeed = 0.0f;
	}
	WellBufferMe(WaterLevel, &WaterLevelBuffered, &WaterLevelSpeed, 0.2f, 0.07f, false);

	const float fixerForGoingBelowGround = 0.4f;
	if (-fixerForGoingBelowGround < TargetCoors.z - WaterLevelBuffered + WATER_Z_ADDITION)
		WaterLevelBuffered += TargetCoors.z - WaterLevelBuffered + WATER_Z_ADDITION - fixerForGoingBelowGround;

	ColModelClass* carCol = (ColModelClass*)car->GetColModel();
	const CVector boatDimensions = carCol->boundingBox.max - carCol->boundingBox.min;
	float boatSize = boatDimensions.Magnitude2D();

	const bool isHeli = (GetHandlingFlags(car) & 0x20000) != 0;
	const bool isBike = (GetHandlingFlags(car) & 0x10000) != 0 || car->IsBike();
	const bool isPlane = (isIII() && car->m_modelIndex == MI_III_DODO) || (GetHandlingFlags(car) & 0x40000);
	const bool isCar = car->IsCar() && !isHeli && !isBike && !isPlane;
	const int index = isCar ? 0 : (isBike ? 1 : (isHeli ? 2 : (isPlane ? 3 : 4)));

	if ((int)TheCamera->CarZoomIndicator == 1) {
		targetAlpha = ZmOneAlphaOffsetVC[index];
		betaDiffMult = BoatBetaDiffMult[0];
		betaSpeedDiffMult = BoatBetaSpeedDiffMult[0];
	} else if ((int)TheCamera->CarZoomIndicator == 2) {
		targetAlpha = ZmTwoAlphaOffsetVC[index];
		betaDiffMult = BoatBetaDiffMult[1];
		betaSpeedDiffMult = BoatBetaSpeedDiffMult[1];
	} else if ((int)TheCamera->CarZoomIndicator == 3) {
		targetAlpha = ZmThreeAlphaOffsetVC[index];
		betaDiffMult = BoatBetaDiffMult[2];
		betaSpeedDiffMult = BoatBetaSpeedDiffMult[2];
	}
	if ((int)TheCamera->CarZoomIndicator == 1 && boatSize < 10.0f) {
		targetAlpha -= SMALLBOAT_CLOSE_ALPHA_MINUS;
		boatSize = 10.0f;
	}

	if (cam->ResetStatics) {
		cam->Alpha = targetAlpha;
		cam->AlphaSpeed = 0.0f;
	}
	WellBufferMe(targetAlpha, &cam->Alpha, &cam->AlphaSpeed, 0.15f, 0.07f, true);

	if (cam->ResetStatics) {
		cam->Beta = TargetOrientation;
		cam->BetaSpeed = 0.0f;
	}
	WellBufferMe(TargetOrientation, &cam->Beta, &cam->BetaSpeed,
		betaDiffMult * car->m_vecMoveSpeed.Magnitude(), betaSpeedDiffMult, true);

	cam->Source = (TheCamera->CarZoomValueSmooth + boatSize) * CVector(-cosf(cam->Beta), -sinf(cam->Beta), 0.0f) + TargetCoors;
	cam->Source.z = WaterLevelBuffered + WATER_Z_ADDITION + (boatDimensions.z / 2.0f + MAX_HEIGHT_UP) * sinf(cam->Alpha);

	cam->m_cvecTargetCoorsForFudgeInter = TargetCoors;

	// AvoidTheGeometry: keep the camera out of the world (approximated with a
	// line-of-sight clip, as in the mod's other camera paths).
	{
		pIgnoreEntity = (CEntity*)car;
		CColPoint colPoint;
		CEntity* entity = nil;
		if (WorldClass::ProcessLineOfSight(TargetCoors, cam->Source, colPoint, entity, true, false, false, true, false, false, true))
			cam->Source = colPoint.point;
		pIgnoreEntity = nil;
	}

	cam->Front = TargetCoors - cam->Source;
	cam->Front.Normalise();

	// Vice City vanilla steering roll.
	float targetRoll = 0.0f;
	if (cameraWobble > 0.0f) {
		float fwdSpeed = SpeedKphFactor * DotProduct(car->m_vecMoveSpeed, car->GetForward());
		if (fwdSpeed > MaxForwardSpeed)
			fwdSpeed = MaxForwardSpeed;
		const float steer = (float)pad0.GetSteeringLeftRight() / 128.0f;
		CVector fwdTarget = car->GetForward();
		fwdTarget.Normalise();
		const float angleDiff = acosf(clamp(fabsf(DotProduct(fwdTarget, cam->Front)), 0.0f, 1.0f));
		targetRoll = steer * (fwdSpeed / MaxForwardSpeed) * (DEGTORAD(10.0f) * TiltOverShoot[index] + cam->f_max_role_angle) * sinf(angleDiff) * cameraWobble;
	}
	WellBufferMe(targetRoll, &cam->f_Roll, &cam->f_rollSpeed, 0.15f, 0.07f, false);

	cam->GetVectorsReadyForRW();
	cam->ResetStatics = false;
}


// ---------------------------------------------------------------------------
// Vanilla "camera on a string" vehicle camera.
//
// This reproduces the original vehicle camera of both games. GTA III and
// Vice City implement CCam::Process_Cam_On_A_String differently, so the two
// algorithms are kept separate here; the mod's ini-driven features are
// layered on top of the authentic behaviour.
//
// The implementation follows the reversed sources of re3 (GTA III) and
// reVC (Vice City).
// ---------------------------------------------------------------------------
template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void
Process_Cam_On_A_String_Vanilla(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation)
{
	static float AlphaOffset = 0.0f;
	static float AlphaOffsetSpeed = 0.0f;
	static float LastTargetAlphaWithCollisionOn = 0.0f;
	static float LastTopAlphaSpeed = 0.15f;
	static float LastAlphaSpeedStep = 0.015f;
	static float heliTilt = 0.0f;
	static float heliTiltSpeed = 0.0f;
	static float stepsLeftToChangeBetaByMouse = 0.0f;
	static float heightIncreaseMult = 0.0f;
	static bool PreviousNearCheckNearClipSmall = false;

	gFreeLookOwnsLR = false;
	if (!car->IsVehicle())
		return;

	CPad* pad = &pad0;
	const bool vc = isVC();
	const bool isHeli = (GetHandlingFlags(car) & 0x20000) != 0;
	const bool isBike = (GetHandlingFlags(car) & 0x10000) != 0 || car->IsBike();
	const bool isPlane = (isIII() && car->m_modelIndex == MI_III_DODO) || (GetHandlingFlags(car) & 0x40000);
	const bool isCar = car->IsCar() && !isHeli && !isBike && !isPlane;
	const int index = isCar ? 0 : (isBike ? 1 : (isHeli ? 2 : (isPlane ? 3 : 4)));
	// Helicopters and the RC Baron yaw with the look-left/right keys, so their
	// side look must stay off in every game (vanilla does the same in
	// CCam::Process).
	const bool disableSideLook = DisableVehicleSideLook(car->m_modelIndex, isHeli);

	ColModelClass* carCol = (ColModelClass*)car->GetColModel();
	if (!carCol)
		return; // collision model not loaded yet (e.g. a vehicle added at an unused ID)
	CVector Dimensions = carCol->boundingBox.max - carCol->boundingBox.min;

	// The mod's free camera owns look left/right only while it is actually
	// driving the view (gFreeLookOwnsLR is set to mouseChangesBeta below). While
	// it is idle the game's own left/right snap runs untouched, which keeps the
	// keyboard look keys, the vanilla side view and the drive-by side aim
	// working. Look behind always stays with the game (GetLookBehindForCar);
	// the on-foot look-behind (GetLookBehindForPed) is never a vehicle input.
	gFreeLookOwnsLR = false;

	const uint8 nextDirectionIsForward =
		!pad->GetLookBehindForCar() &&
		cam->DirectionWasLooking == LOOKING_FORWARD;

	// ---- Field of view ----
	// The dynamic speed FOV is a faithful port of ThirteenAG's WidescreenFixesPack
	// "CarSpeedDependantFOV" (Misc.ixx): expand the FOV with forward speed, decay
	// it back at 0.98^dt and cap it 30 degrees over the base. MIT licensed, see
	// licenses/WidescreenFixesPack.txt.
	float baseFOV = (fovProfile == PROFILE_CUSTOM) ? customBaseFOV : DefaultFOV;
	float maxFOVAdd = 30.0f;
	float fovStartSpeed = 0.4f;
	if (dynamicSpeedFOVMaxFOV >= 0.0f)
		maxFOVAdd = dynamicSpeedFOVMaxFOV;
	if (dynamicSpeedFOVStartSpeed >= 0.0f)
		fovStartSpeed = dynamicSpeedFOVStartSpeed;
	if (cam->ResetStatics) {
		cam->FOV = baseFOV;
	} else if (dynamicSpeedFOV > 0.0f) {
		float forwardSpeed = DotProduct(car->GetForward(), car->m_vecMoveSpeed);
		if (forwardSpeed > fovStartSpeed)
			cam->FOV += (forwardSpeed - fovStartSpeed) * ms_fTimeStep * dynamicSpeedFOV;
		if (cam->FOV > baseFOV) {
			// 0.98 is the game's CAR_FOV_FADE_MULT decay per step; IV and any
			// ini override can wind the FOV down faster.
			float fovDecay = (dynamicSpeedFOVDecay > 0.0f && dynamicSpeedFOVDecay < 1.0f) ? dynamicSpeedFOVDecay : 0.98f;
			cam->FOV = powf(fovDecay, ms_fTimeStep) * (cam->FOV - baseFOV) + baseFOV;
		}
		cam->FOV = clamp(cam->FOV, baseFOV, baseFOV + maxFOVAdd * dynamicSpeedFOV);
	} else {
		cam->FOV = baseFOV;
	}

	// ---- Target position and base distance ----
	CVector TargetCoors = CameraTarget;
	TargetCoors.z += cameraHeightOffset;
	float BaseDist = vc ? Dimensions.Magnitude() : Dimensions.Magnitude2D();
	if (vc && isBike)
		BaseDist *= 1.45f;

	if (isBike && heightIncreaseOnBike) {
		if (car->pPassengers[0])
			heightIncreaseMult = min(1.0f, ms_fTimeStep * 0.02f + heightIncreaseMult);
		else
			heightIncreaseMult = max(0.0f, heightIncreaseMult - ms_fTimeStep * 0.02f);
		Dimensions.z += 0.4f * heightIncreaseMult;
	} else {
		heightIncreaseMult = 0.0f;
	}

	if (vc && isHeli && car->m_status != STATUS_PLAYER_REMOTE)
		TargetCoors += 0.6f * car->GetUp() * Dimensions.z;
	else
		TargetCoors.z += vc ? 0.8f * Dimensions.z : (Dimensions.z - 0.1f);

	// ---- Vehicle-specific zoom ----
	// Vice City scales the zoom per vehicle type itself. For the vanilla
	// profile we therefore leave CarZoomValueSmooth untouched, which also
	// keeps the Widescreen Fix (patching the same table) in control.
	float zoomValue = TheCamera->CarZoomValueSmooth;
	{
		const float* zoomModes = nullptr;
		if (distanceProfile == PROFILE_SA)
			zoomModes = CarZoomModesSA;
		else if (distanceProfile == PROFILE_LCS)
			zoomModes = CarZoomModesLCS;
		else if (distanceProfile == PROFILE_CUSTOM)
			zoomModes = CarZoomModesCustom;
		else if (!vc && vehicleSpecificZoom > 0.0f)
			zoomModes = CarZoomModesVC; // GTA III has no per-vehicle table; emulate Vice City's

		if (zoomModes) {
			// CarZoomValueSmooth is in the running game's own zoom range, so the
			// source endpoints are the game's native table (the row for this
			// vehicle class) and the target is the selected table. With the
			// [Custom] keys blank the two match, so this is the identity and
			// Custom reproduces the game's own camera.
			const float* srcModes = isReLCS ? CarZoomModesLCS : (isVC() ? CarZoomModesVC : CarZoomModesIII);
			auto srcVal = [&](int row) { return srcModes[index + row]; };
			// vehicleSpecificZoom blends the per-vehicle row (index) with the
			// generic car row (0): 1 = full per-vehicle, 0 = generic.
			float vsz = max(0.0f, vehicleSpecificZoom);
			auto zm = [&](int row) {
				return zoomModes[row] + (zoomModes[index + row] - zoomModes[row]) * vsz;
			};
			int ind = (int)TheCamera->CarZoomIndicator;
			if (ind == 3)
				zoomValue = zm(10);
			else if (ind == 2 && srcVal(10) != srcVal(5))
				zoomValue = zm(5) +
					(zoomValue - srcVal(5)) * (zm(10) - zm(5)) / (srcVal(10) - srcVal(5));
			else if (ind == 1 && srcVal(5) != srcVal(0))
				zoomValue = zm(0) +
					(zoomValue - srcVal(0)) * (zm(5) - zm(0)) / (srcVal(5) - srcVal(0));
			if (zoomValue < zm(0))
				zoomValue = zm(0);
		}
	}

	// ---- Elastic string stretch (adds SA-style speed stretch on top) ----
	float extraDist = 0.0f;
	if (elasticStringPhysics > 0.0f && (isCar || isBike || car->IsBoat())) {
		float forwardSpeed = DotProduct(car->m_vecMoveSpeed, car->GetForward()) * SpeedKphFactor;
		extraDist += clamp(forwardSpeed * (2.0f / MaxForwardSpeed), -1.0f, 2.0f) * elasticStringPhysics;
	}

	// ---- Vice City RC vehicles need extra distance / angle ----
	float extraAlpha = 0.0f;
	if (vc) {
		if (car->m_modelIndex == RcRaider || car->m_modelIndex == RcGoblin) {
			extraDist += 6.0f;
			extraAlpha = 0.2f;
		} else if (!isReLCS && car->m_modelIndex == MI_VC_RCBARON) {
			extraDist += 9.5f;
			extraAlpha = 0.295f;
		}
	}

	cam->CA_MAX_DISTANCE = BaseDist + 0.1f + zoomValue + extraDist + CameraDistanceOffsetForZoom((int)TheCamera->CarZoomIndicator);
	cam->CA_MIN_DISTANCE = (cameraMinDistance >= 0.0f) ? cameraMinDistance : min(BaseDist * 0.6f, 3.5f);
	cam->CA_MAX_DISTANCE *= cameraDistanceScale;
	if (cam->CA_MIN_DISTANCE > cam->CA_MAX_DISTANCE)
		cam->CA_MIN_DISTANCE = cam->CA_MAX_DISTANCE - 0.05f;

	// ---- Reset statics when entering the vehicle ----
	if (cam->ResetStatics) {
		cam->AlphaSpeed = 0.0f;
		cam->BetaSpeed = 0.0f;
		cam->f_Roll = 0.0f;
		cam->f_rollSpeed = 0.0f;
		heliTilt = 0.0f;
		heliTiltSpeed = 0.0f;
		AlphaOffset = 0.0f;
		AlphaOffsetSpeed = 0.0f;
		LastTargetAlphaWithCollisionOn = 0.0f;
		LastTopAlphaSpeed = 0.15f;
		LastAlphaSpeedStep = 0.015f;
		stepsLeftToChangeBetaByMouse = 0.0f;
		heightIncreaseMult = 0.0f;

		if (TheCamera->m_bIdleOn)
			TheCamera->m_uiTimeWeEnteredIdle = m_snTimeInMilliseconds;

		cam->Beta = LimitRadianAngle(TargetOrientation + (TheCamera->m_bCamDirectlyInFront ? PI : 0.0f));
		cam->Alpha = 0.0f;

		cam->Source.x = TargetCoors.x - cosf(cam->Beta) * cam->CA_MAX_DISTANCE;
		cam->Source.y = TargetCoors.y - sinf(cam->Beta) * cam->CA_MAX_DISTANCE;
		cam->Source.z = TargetCoors.z;
	}

	// ---- Look left / right / behind ----
	// III/VC handle look in CCam::Process (CCam::LookBehind / LookLeft /
	// LookRight), which run right after this function and set Source directly
	// from the car position/heading. Look behind is still left to the game, so
	// the mod must not move Beta for it: LookBehind picks the direction from
	// DeltaBeta = carHeading - Beta, and a front-facing Beta makes it latch
	// LookBehindCamWasInFront and flip the camera back behind (which looked like
	// "forwards"). Look left/right are now driven as free-look below (the native
	// snap is swallowed); the vanilla string physics bring the camera back.

	// ---- Mouse free-look (ported from the San Andreas camera) ----
	// The mouse is read before the camera direction is finalised. While the player
	// is free-looking, Beta and Alpha are kept as state (exactly like the SA
	// camera) so the vehicle's motion does not drag the view; the game re-takes
	// control once the 50-step hold runs out.
	// Pressing a look key must end any mouse hold immediately, otherwise the camera
	// stays stuck at the mouse angle until the 50-step release finishes.
	if (pad->GetLookBehindForCar() || pad->GetLookLeft() || pad->GetLookRight())
		stepsLeftToChangeBetaByMouse = 0.0f;
	bool mouseChangesBeta = false;
	// True only while the player is actively moving the look input this frame,
	// not during the trailing 50-step release hold. The reverse takeover keys off
	// this so a leftover hold from before the reverse cannot suppress the swing.
	bool lookInputThisFrame = false;
	float mouseXMovement = 0.0f;
	float mouseYMovement = 0.0f;
	if (mouseFreeLook && m_bUseMouse3rdPerson && !GetDisablePlayerControls(pad) && nextDirectionIsForward) {
		float mouseY = CPad::NewMouseControllerState.y * 2.0f;
		float mouseX = CPad::NewMouseControllerState.x * -2.0f;
		if ((mouseX != 0.0f || mouseY != 0.0f) && m_bDisableMouseSteering) {
			float v113 = cam->FOV * 0.0125f;
			float sensitivity = (index == 0) ? 0.8f : ((index == 1) ? 0.75f : 1.0f);
			mouseYMovement = mouseY * v113 * GetMouseAccel(TheCamera) * sensitivity;
			mouseXMovement = mouseX * v113 * GetMouseAccel(TheCamera) * sensitivity;
			cam->BetaSpeed = 0.0f;
			cam->AlphaSpeed = 0.0f;
			stepsLeftToChangeBetaByMouse = 50.0f;
			mouseChangesBeta = true;
			lookInputThisFrame = true;
		} else if (stepsLeftToChangeBetaByMouse > 0.0f) {
			cam->BetaSpeed = 0.0f;
			cam->AlphaSpeed = 0.0f;
			stepsLeftToChangeBetaByMouse = max(0.0f, stepsLeftToChangeBetaByMouse - ms_fTimeStep);
			mouseChangesBeta = true;
		}
	}

	// ---- Free-look (gamepad right stick + the game's own keyboard look keys) ----
	// GetCarGun(LR/UD) is the turret/right-stick axis. III/VC feed the game's
	// own keyboard look/turret keys into it too (VC: Look left/right; III: Turret
	// left/right; both: Turret/Lean up/down), so reading it gives the keyboard
	// free-look for free, driven exactly like the right stick. III/VC have no
	// free camera of their own, so the vertical axis is no longer zeroed out.
	if (mouseFreeLook && !GetDisablePlayerControls(pad) && nextDirectionIsForward) {
		float stickX = -(float)pad->GetCarGunLeftRight();
		float stickY = (float)pad->GetCarGunUpDown();

		// With KeyboardFreeLook off the keyboard must not move the free camera.
		// GInput tells a pad from a keyboard; without it the game's own look keys
		// are still detected (a pad's stick never presses them), so the keyboard
		// look keys do not get swallowed from the game either.
		if (!KeyboardFreeLookAxisEnabled() ||
		    (!keyboardFreeLook && (pad->GetLookLeft() || pad->GetLookRight() || pad->GetLookBehindForCar()))) {
			stickX = 0.0f;
			stickY = 0.0f;
		}

		// With the smooth side view on, the game's look keys swing the camera
		// (the mod's smooth look) instead of free-looking, so drop just the
		// horizontal keyboard contribution; the right stick still free-looks.
		// Helicopters/RC Baron are exempt: there those keys yaw the vehicle.
		if (smoothSideView && !disableSideLook && (pad->GetLookLeft() || pad->GetLookRight()))
			stickX = 0.0f;

		const bool ginputHasPad = GInputPadInHands();
		if (ginputHasPad && padSettings.InvertLook)
			stickY = -stickY;
		if (vc && *(bool*)0xA10AF7)
			stickY = -stickY;

		// Exponential smoothing so the gamepad camera does not feel rough.
		static float smoothedStickX = 0.0f;
		static float smoothedStickY = 0.0f;
		float stickSmoothing = min(1.0f, ms_fTimeStep * 0.25f);
		smoothedStickX += (stickX - smoothedStickX) * stickSmoothing;
		smoothedStickY += (stickY - smoothedStickY) * stickSmoothing;
		stickX = smoothedStickX;
		stickY = smoothedStickY;

		if (fabsf(stickX) > 0.05f || fabsf(stickY) > 0.05f) {
			float v113 = cam->FOV * 0.0125f;
			float sensitivity = (index == 0) ? 0.8f : ((index == 1) ? 0.75f : 1.0f);
			float xMovement = fabsf(stickX) * (v113 * 0.071428575f) * stickX * 0.007f * 0.007f;
			float yMovement = fabsf(stickY) * (v113 * 0.042857144f) * stickY * 0.007f * 0.007f;
			// Beta follows the stick horizontally; Alpha is raised by pushing the
			// stick up (the mouse path uses the opposite Y sign, so it is negated).
			mouseXMovement += xMovement * sensitivity;
			// Inverted on purpose: in GTA III / Vice City pushing the right stick
			// up looks up, which is the opposite sign to the mouse path.
			mouseYMovement += yMovement * sensitivity;
			cam->BetaSpeed = 0.0f;
			cam->AlphaSpeed = 0.0f;
			stepsLeftToChangeBetaByMouse = 50.0f;
			mouseChangesBeta = true;
			lookInputThisFrame = true;
		}
	}

	// While the mod's free camera is driving the view, swallow the game's own
	// left/right snap (see the look hooks). Once it is idle the native side view
	// takes over again, which restores the keyboard look keys and the drive-by.
	gFreeLookOwnsLR = mouseChangesBeta;

	// ---- Basic string constraint (Cam_On_A_String_Unobscured) ----
	// The string only constrains the distance to the target. The direction comes
	// from Beta, which is derived from the current position unless the player is
	// free-looking.
	// While free-looking the distance is pinned to the zoom target so looking
	// around can never change the zoom; otherwise the distance is the vanilla
	// world-anchored string.
	float stringLength;
	if (mouseChangesBeta) {
		stringLength = cam->CA_MAX_DISTANCE;
	} else {
		stringLength = (cam->Source - TargetCoors).Magnitude2D();
		if (cam->ResetStatics)
			stringLength = cam->CA_MAX_DISTANCE + 1.0f;
		if (stringLength < 0.001f)
			stringLength = cam->CA_MAX_DISTANCE;
		if (stringLength > cam->CA_MAX_DISTANCE)
			stringLength = cam->CA_MAX_DISTANCE;
		else if (stringLength < cam->CA_MIN_DISTANCE)
			stringLength = cam->CA_MIN_DISTANCE;
	}

	if (!mouseChangesBeta)
		cam->Beta = GetATanOfXY(TargetCoors.x - cam->Source.x, TargetCoors.y - cam->Source.y);

	// Alpha follows the opposite convention to the SA camera (here a positive
	// Alpha raises the camera), so the mouse pitch is inverted to match SA.
	cam->Beta = LimitRadianAngle(cam->Beta + mouseXMovement);
	cam->Alpha = LimitRadianAngle(cam->Alpha - mouseYMovement);

	cam->Source.x = TargetCoors.x - cosf(cam->Beta) * stringLength;
	cam->Source.y = TargetCoors.y - sinf(cam->Beta) * stringLength;

	// Vice City: when stationary and firing the Firetruck water cannon, the camera
	// follows the turret so the player can aim. (Process_Cam_On_A_String)
	if (vc && car->m_modelIndex == FireTruk && pad->GetCarGunFired() && car->m_vecMoveSpeed.Magnitude2D() < 0.01f) {
		float targetBeta = LimitRadianAngle(car->GetForward().Heading() - *GetDoomAnglePtrLR(car) + HALFPI);
		float firetruckDeltaBeta = LimitRadianAngle(targetBeta - cam->Beta);
		float dist = (TargetCoors - cam->Source).Magnitude();
		dist = 0.1f * dist * clamp(firetruckDeltaBeta, -0.8f, 0.8f);
		cam->Source += dist * CrossProduct(cam->Front, CVector(0.0f, 0.0f, 1.0f));
	}

	cam->m_fDistanceBeforeChanges = (cam->Source - TargetCoors).Magnitude2D();

	// ---- Alpha offset: the vertical angle for each zoom level ----
	{
		const int zoomIndicator = (int)TheCamera->CarZoomIndicator;
		// The Enhanced profile uses Vice City's per-zoom vertical angles even in
		// GTA III (enhancedVC), while the III-only height/collision pass below
		// still runs.
		if (vc || enhancedVC) {
			const float* off1 = ZmOneAlphaOffsetVC;
			const float* off2 = ZmTwoAlphaOffsetVC;
			const float* off3 = ZmThreeAlphaOffsetVC;
			if (anglesProfile == PROFILE_CUSTOM) {
				off1 = ZmOneAlphaOffsetCustom; off2 = ZmTwoAlphaOffsetCustom; off3 = ZmThreeAlphaOffsetCustom;
			} else if (anglesProfile == PROFILE_SA) {
				off1 = ZmOneAlphaOffset; off2 = ZmTwoAlphaOffset; off3 = ZmThreeAlphaOffset;
			} else if (anglesProfile == PROFILE_LCS) {
				off1 = ZmOneAlphaOffsetLCS; off2 = ZmTwoAlphaOffsetLCS; off3 = ZmThreeAlphaOffsetLCS;
			}
			float targetAlphaOffset = 0.0f;
			if (zoomIndicator == 1) targetAlphaOffset = off1[index] + extraAlpha;
			else if (zoomIndicator == 2) targetAlphaOffset = off2[index] + extraAlpha;
			else if (zoomIndicator == 3) targetAlphaOffset = off3[index] + extraAlpha;
			if (cam->ResetStatics)
				AlphaOffset = targetAlphaOffset;
			WellBufferMe(targetAlphaOffset, &AlphaOffset, &AlphaOffsetSpeed, 0.17f, 0.08f, false);
		} else if (anglesProfile == PROFILE_CUSTOM) {
			if (zoomIndicator == 1) AlphaOffset = customAngleNear;
			else if (zoomIndicator == 2) AlphaOffset = customAngleMid;
			else if (zoomIndicator == 3) AlphaOffset = customAngleFar;
		} else if (anglesProfile == PROFILE_SA || anglesProfile == PROFILE_LCS) {
			const float* off1 = (anglesProfile == PROFILE_LCS) ? ZmOneAlphaOffsetLCS : ZmOneAlphaOffset;
			const float* off2 = (anglesProfile == PROFILE_LCS) ? ZmTwoAlphaOffsetLCS : ZmTwoAlphaOffset;
			const float* off3 = (anglesProfile == PROFILE_LCS) ? ZmThreeAlphaOffsetLCS : ZmThreeAlphaOffset;
			if (zoomIndicator == 1) AlphaOffset = off1[index] + extraAlpha;
			else if (zoomIndicator == 2) AlphaOffset = off2[index] + extraAlpha;
			else if (zoomIndicator == 3) AlphaOffset = off3[index] + extraAlpha;
		} else {
			// GTA III: the offset is derived from the smooth zoom value.
			float zv = TheCamera->CarZoomValueSmooth;
			if (zv < 0.1f)
				zv = 0.1f;
			if (zoomIndicator == 1) AlphaOffset = GetATanOfXY(23.0f, zv);
			else if (zoomIndicator == 2) AlphaOffset = GetATanOfXY(10.8f, zv);
			else if (zoomIndicator == 3) AlphaOffset = GetATanOfXY(7.0f, zv);
		}
	}

	// Pitch before the height/pitch pass eases it. A smooth side view holds this
	// pitch, so the view does not climb or dip as the camera swings to the side.
	const float modLookAlphaHold = cam->Alpha;

	// ---- Camera height / pitch (WorkOutCamHeight) ----
	{
		const CVector forward = car->GetForward();
		float carSlope = LimitRadianAngle(GetATanOfXY(forward.Magnitude2D(), forward.z));
		float deltaBeta = LimitRadianAngle(cam->Beta - TargetOrientation);
		// Dead-zone: ignore shallow slopes so flat roads cannot jitter the camera.
		// The threshold is subtracted, so the tilt grows smoothly from zero as the
		// slope passes it instead of stepping on. Only the Vice City path needs
		// this: GTA III's path below already has its own 1.8 degree dead-zone, and
		// stacking the two was swallowing the tilt on ordinary roads.
		if (vc) {
			if (fabsf(carSlope) <= pitchTiltMinAngle)
				carSlope = 0.0f;
			else
				carSlope -= (carSlope > 0.0f ? pitchTiltMinAngle : -pitchTiltMinAngle);
		}
		float carAlpha = -carSlope * cosf(deltaBeta);
		const float length = (cam->Source - TargetCoors).Magnitude2D();

		// Slope tilt strengths: pitchTilt is the master; the uphill/downhill
		// keys override it per direction (<0 = use the master value).
		float tiltUphill = (pitchTiltUphill >= 0.0f) ? pitchTiltUphill : pitchTilt;
		float tiltDownhill = (pitchTiltDownhill >= 0.0f) ? pitchTiltDownhill : pitchTilt;
		bool tiltEnabled = tiltUphill > 0.0f || tiltDownhill > 0.0f;

		if (vc) {
			// Vice City: the Firetruck cannon pitches the camera while firing.
			if (car->m_modelIndex == FireTruk && pad->GetCarGunFired()) {
				carAlpha = DEGTORAD(10.0f);
			} else if (isHeli && tiltEnabled && length != 0.0f) {
				carAlpha = 0.0f;
				const float heliFwdSpeed = DotProduct(car->m_vecMoveSpeed, forward) * SpeedKphFactor;
				const float heliFwdZ = forward.z;
				const float heliFwdXY = forward.Magnitude2D();
				const float alphaAmount = min(fabsf(heliFwdSpeed / 90.0f), 1.0f);
				if (heliFwdXY != 0.0f || heliFwdZ != 0.0f)
					carAlpha = GetATanOfXY(heliFwdXY, fabsf(heliFwdZ)) * alphaAmount;

				CColPoint point;
				CEntity* entity = nil;
				CVector test = cam->Source;
				test.z = TargetCoors.z + 0.2f + length * sinf(carAlpha + AlphaOffset) + cam->m_fCloseInCarHeightOffset;
				if (WorldClass::ProcessVerticalLine(test, car->GetPosition().z, point, entity, true, false, false, false, false, false, nil)) {
					const float sinV = (point.point.z - TargetCoors.z - 0.2f - cam->m_fCloseInCarHeightOffset) / length;
					carAlpha = asinf(clamp(sinV, -1.0f, 1.0f)) - AlphaOffset;
					if (carAlpha < 0.0f)
						AlphaOffset += carAlpha;
				}
			}

			if (carAlpha > 0.0f) {
				float hi = (tiltUphill > 0.0f) ? 0.35f : DEGTORAD(89.0f);
				carAlpha = clamp(carAlpha, 0.0f, hi) * tiltDownhill;
			} else {
				carAlpha = clamp(carAlpha, -0.35f, 0.0f) * tiltUphill;
			}

			if (cam->ResetStatics)
				cam->Alpha = carAlpha;

			float targetAlpha = cam->Alpha;
			if (fabsf(LimitRadianAngle(carAlpha - targetAlpha)) > 0.0f && !IsVehicleSuspensionHigh(TheCamera))
				targetAlpha = carAlpha;

			if (!mouseChangesBeta) {
				if (isBike || isHeli)
					WellBufferMe(targetAlpha, &cam->Alpha, &cam->AlphaSpeed, 0.09f, 0.04f, true);
				else
					WellBufferMe(targetAlpha, &cam->Alpha, &cam->AlphaSpeed, 0.15f, 0.07f, true);
			}
		} else {
			// GTA III: nearly level with a small dead-zone plus roof/ground checks
			// (WorkOutCamHeight).
			float topAlphaSpeed = 0.15f;
			float alphaSpeedStep = 0.015f;
			bool camClear = true;

			if (!tiltEnabled) {
				if (carAlpha < -0.01f)
					carAlpha = -0.01f;
			} else if (carAlpha > 0.0f) {
				float hi = (tiltUphill > 0.0f) ? 0.35f : DEGTORAD(89.0f);
				carAlpha = clamp(carAlpha, 0.0f, hi) * tiltDownhill;
			} else {
				carAlpha = clamp(carAlpha, -0.35f, 0.0f) * tiltUphill;
			}

			if (cam->ResetStatics)
				cam->Alpha = carAlpha;

			float deltaAlpha = LimitRadianAngle(carAlpha - cam->Alpha);
			const float angleLimit = DEGTORAD(1.8f);
			if (deltaAlpha > angleLimit)
				deltaAlpha -= angleLimit;
			else if (deltaAlpha < -angleLimit)
				deltaAlpha += angleLimit;
			else
				deltaAlpha = 0.0f;

			if (cam->m_bCollisionChecksOn) {
				const float targetHeight = Dimensions.z;
				float targetAlpha = 0.0f;
				bool foundRoofCenter = false, foundRoofSide1 = false, foundRoofSide2 = false;
				bool foundCamRoof = false, foundCamGround = false;
				float camRoof = 0.0f;
				const float carBottom = TargetCoors.z - targetHeight / 2.0f;

				const float carRoof = WorldClass::FindRoofZFor3DCoord(TargetCoors.x, TargetCoors.y, carBottom, &foundRoofCenter);

				CVector fwd = car->GetForward();
				fwd.Normalise();
				const float carSideAngle = GetATanOfXY(fwd.x, fwd.y) + HALFPI;
				const float sideX = 2.5f * cosf(carSideAngle);
				const float sideY = 2.5f * sinf(carSideAngle);
				WorldClass::FindRoofZFor3DCoord(TargetCoors.x + sideX, TargetCoors.y + sideY, carBottom, &foundRoofSide1);
				WorldClass::FindRoofZFor3DCoord(TargetCoors.x - sideX, TargetCoors.y - sideY, carBottom, &foundRoofSide2);

				const float camGround = WorldClass::FindGroundZFor3DCoord(cam->Source.x, cam->Source.y,
					TargetCoors.z + length * sinf(cam->Alpha + AlphaOffset) + cam->m_fCloseInCarHeightOffset, &foundCamGround);
				float camTargetZ = 0.0f;
				if (foundCamGround) {
					camRoof = WorldClass::FindRoofZFor3DCoord(cam->Source.x, cam->Source.y, camGround + targetHeight, &foundCamRoof);
					camTargetZ = camGround + targetHeight * 1.5f + 0.1f;
				} else {
					foundCamRoof = false;
					camTargetZ = TargetCoors.z;
				}

				if (foundRoofCenter && !foundCamRoof && (foundRoofSide1 || foundRoofSide2)) {
					targetAlpha = GetATanOfXY(cam->CA_MAX_DISTANCE, carRoof - camTargetZ - 1.5f);
					camClear = false;
				}
				if (foundCamRoof) {
					const float roof = foundRoofCenter ? min(camRoof, carRoof) : camRoof;
					targetAlpha = GetATanOfXY(cam->CA_MAX_DISTANCE, roof - camTargetZ - 1.5f);
					camClear = false;
				}

				targetAlpha = LimitRadianAngle(targetAlpha);
				if (targetAlpha < DEGTORAD(-7.0f))
					targetAlpha = DEGTORAD(-7.0f);
				if (targetAlpha > AlphaOffset)
					camClear = true;

				PreviousNearCheckNearClipSmall = false;
				if (!camClear) {
					PreviousNearCheckNearClipSmall = true;
					RwCameraSetNearClipPlane(RwCamera, DefaultNearClip);
					deltaAlpha = LimitRadianAngle(targetAlpha - (cam->Alpha + AlphaOffset));
					topAlphaSpeed = 0.3f;
					alphaSpeedStep = 0.03f;
				}

				const float camZ = TargetCoors.z + length * sinf(cam->Alpha + deltaAlpha + AlphaOffset) + cam->m_fCloseInCarHeightOffset;
				bool foundGround, foundRoof;
				const float camGround2 = WorldClass::FindGroundZFor3DCoord(cam->Source.x, cam->Source.y, camZ, &foundGround);
				if (foundGround && camClear) {
					if (camZ - camGround2 < 1.5f) {
						PreviousNearCheckNearClipSmall = true;
						RwCameraSetNearClipPlane(RwCamera, DefaultNearClip);
						const float dz = camGround2 + 1.5f - TargetCoors.z;
						float a = (length == 0.0f || dz == 0.0f) ? cam->Alpha : GetATanOfXY(length, dz);
						deltaAlpha = LimitRadianAngle(a) - cam->Alpha;
					}
				} else if (camClear) {
					float camRoof2 = WorldClass::FindRoofZFor3DCoord(cam->Source.x, cam->Source.y, camZ, &foundRoof);
					if (foundRoof && camZ - camRoof2 < 1.5f) {
						PreviousNearCheckNearClipSmall = true;
						RwCameraSetNearClipPlane(RwCamera, DefaultNearClip);
						if (camRoof2 > TargetCoors.z + 3.5f)
							camRoof2 = TargetCoors.z + 3.5f;
						const float dz = camRoof2 + 1.5f - TargetCoors.z;
						float a = (length == 0.0f || dz == 0.0f) ? cam->Alpha : GetATanOfXY(length, dz);
						deltaAlpha = LimitRadianAngle(a) - cam->Alpha;
					}
				}

				LastTargetAlphaWithCollisionOn = deltaAlpha + cam->Alpha;
				LastTopAlphaSpeed = topAlphaSpeed;
				LastAlphaSpeedStep = alphaSpeedStep;
			} else if (PreviousNearCheckNearClipSmall) {
				RwCameraSetNearClipPlane(RwCamera, DefaultNearClip);
			}

			if (!mouseChangesBeta)
				WellBufferMe(LastTargetAlphaWithCollisionOn, &cam->Alpha, &cam->AlphaSpeed, LastTopAlphaSpeed, LastAlphaSpeedStep, true);
		}

		cam->Source.z = TargetCoors.z + sinf(cam->Alpha + AlphaOffset) * length + cam->m_fCloseInCarHeightOffset;
	}

	// ---- Free-look placement ----
	// Pitching down keeps a constant 3D distance (SA sphere) so the car never
	// changes size and the view can reach overhead; pitching up keeps the
	// vanilla horizontal string distance (cylinder) so the camera stays clear
	// of the car. The elevation is clamped to the SA range, but it is stopped
	// just short of a straight top-down view. The collision pass below still
	// stops the camera on the ground when looking up.
	if (mouseChangesBeta) {
		const float (*angleTable)[15] = (anglesProfile == PROFILE_CUSTOM) ? CARCAM_SET_CUSTOM :
			((anglesProfile == PROFILE_VANILLA) ? CARCAM_SET_VANILLA :
			((anglesProfile == PROFILE_LCS) ? CARCAM_SET_LCS : CARCAM_SET_SA));
		const float maxElevationRaw = (minPitchAngle >= 0.0f) ? minPitchAngle : angleTable[index][14];
		const float maxElevation = min(maxElevationRaw, DEGTORAD(80.0f));
		const float downLimit = (maxPitchAngle >= 0.0f) ? maxPitchAngle : angleTable[index][13];
		const float elevation = clamp(cam->Alpha + AlphaOffset, -downLimit, maxElevation);
		cam->Alpha = elevation - AlphaOffset;

		const float distance = cam->CA_MAX_DISTANCE;
		const float horizontal = (elevation >= 0.0f) ? cosf(elevation) * distance : distance;
		cam->Source.x = TargetCoors.x - cosf(cam->Beta) * horizontal;
		cam->Source.y = TargetCoors.y - sinf(cam->Beta) * horizontal;
		cam->Source.z = TargetCoors.z + sinf(elevation) * distance;
	}

	// ---- Reverse look-behind state ----
	// Owns Beta while engaged so the "rotate behind car" auto-fix below cannot
	// fight it; that fight is why the camera never reached 180 degrees and wobbled.
	static float reverseTime = 0.0f;
	static bool reverseLookActive = false;
	static float reverseBetaSpeed = 0.0f;
	// Latched when the player takes the mouse during a reverse: it blocks the
	// swing from re-arming so the mouse keeps Beta until the reverse ends.
	static bool reversePlayerOverride = false;
	const float reverseSpeed = DotProduct(car->GetForward(), car->m_vecMoveSpeed);
	// reverseTime is in seconds, so ReverseCameraDelay reads naturally.
	if (reverseSpeed < -0.05f)
		reverseTime += ms_fTimeStep * TimeStepToSeconds;
	else
		reverseTime = 0.0f;

	if (!nextDirectionIsForward) {
		// Player took over (look left/right/behind): abort the auto swing.
		reverseTime = 0.0f;
		reverseLookActive = false;
	}

	// Once the reverse ends, a later reverse may arm the swing again. If the
	// player has taken the mouse, the swing stays off for the rest of the reverse.
	if (reverseTime == 0.0f)
		reversePlayerOverride = false;
	// Latch only on an actual look input this frame, not the trailing release
	// hold, so a leftover hold from before the reverse cannot suppress the swing.
	if (lookInputThisFrame)
		reversePlayerOverride = true;

	if (!reverseCam || reversePlayerOverride) {
		reverseLookActive = false;
	}
	else if (reverseTime > reverseCamDelay) {
		reverseLookActive = true;
	}
	else if (reverseLookActive && reverseSpeed > 0.05f) {
		reverseLookActive = false; // driving forward again: hand back to the follow camera
	}

	if (!reverseLookActive)
		reverseBetaSpeed = 0.0f;

	// ---- Look (smooth side view) / rotate the camera behind the car ----
	bool modLookActive = false;
	{
		const float maxDiffBeta = DEGTORAD(160.0f);
		float forwardSpeed = reverseSpeed;
		bool movingForward = forwardSpeed > 0.02f; // signed, as re3: reversing must not trigger this

		if (fabsf(LimitRadianAngle(TargetOrientation - cam->Beta)) > PI - maxDiffBeta && movingForward && TheCamera->m_uiTransitionState == 0)
			cam->m_bFixingBeta = true;

		bool setBeta = TheCamera->m_bCamDirectlyBehind || TheCamera->m_bCamDirectlyInFront || TheCamera->m_bUseTransitionBeta;
		float stiffness = max(0.0f, cameraStiffness) * max(0.0f, cameraAnchoring);

		// Smooth side view on: the mod owns look left/right/behind and eases in
		// and back out like the SA engine. Off: the game's native
		// LookBehind/Left/Right is left untouched (see InitVanillaLookHooks).
		const bool lookBehindHeld = pad->GetLookBehindForCar();
		// Helicopters and the RC Baron yaw with the look-left/right keys, so the
		// smooth side view must ignore those keys there (vanilla disables its own
		// left/right handling for these vehicles in CCam::Process).
		const bool lookLeftHeld = !disableSideLook && pad->GetLookLeft();
		const bool lookRightHeld = !disableSideLook && pad->GetLookRight();
		const int lookNow = lookBehindHeld ? 1 : (lookLeftHeld ? 2 : (lookRightHeld ? 3 : 0));
		// The smooth side view owns Beta in its own accumulator. The vanilla string
		// re-derives Beta from the one-frame-old camera position and eases it back
		// towards the car heading every frame (the speed-scaled ease above); at high
		// speed that pull is strong enough to keep the side view short of a full 90
		// degrees. Writing Beta back from the accumulator after that ease keeps the
		// swing exact regardless of speed. (The SA engine skips its Beta
		// integration while a look is active, so it does not need this.)
		static float modLookBeta = 0.0f;
		static float modLookBetaSpeed = 0.0f;
		static bool modLookReturning = false;
		static bool modLookOwnsBeta = false;
		// Look radius, captured once when the look starts (from the follow camera's
		// own distance). It must not be re-read from the camera position each frame:
		// the collision below moves the camera, which would shrink the distance and
		// make the side view creep towards the car.
		static float vcLookDist = -1.0f;
		static float vcLookClipLen = -1.0f;
		if (cam->ResetStatics) {
			modLookBeta = 0.0f;
			modLookBetaSpeed = 0.0f;
			modLookReturning = false;
			modLookOwnsBeta = false;
			vcLookDist = -1.0f;
			vcLookClipLen = -1.0f;
		}
		// If the player starts free-looking during the return, abandon the return
		// immediately and let the mouse own Beta (otherwise the two fight until the
		// return reaches the default).
		if (mouseChangesBeta) {
			modLookReturning = false;
			modLookBetaSpeed = 0.0f;
			modLookOwnsBeta = false;
		}

		const bool wantLook = smoothSideView && (lookNow != 0 || modLookReturning);
		if (wantLook && !modLookOwnsBeta) {
			// Seed the accumulator from the current camera angle so the swing starts
			// smoothly from wherever the camera is, and take the follow camera's
			// distance as the look radius for this whole look.
			modLookBeta = cam->Beta;
			modLookBetaSpeed = 0.0f;
			modLookOwnsBeta = true;
			vcLookDist = stringLength;
		} else if (!wantLook) {
			modLookOwnsBeta = false;
			vcLookDist = -1.0f;
			vcLookClipLen = -1.0f;
		}
		modLookActive = wantLook;
		if (modLookActive) {
			if (lookNow != 0)
				modLookReturning = true;
			// reVC's LookBehind/Left/Right place the camera at CA_MAX_DISTANCE in
			// front/at the side of the car and look back at it. In this engine's
			// Beta convention that position is at (reVC angle + PI).
			float betaTarget = TargetOrientation;
			if (lookNow == 1)
				betaTarget = TargetOrientation + PI;     // behind: look from the front
			else if (lookNow == 2)
				betaTarget = TargetOrientation + HALFPI; // left
			else if (lookNow == 3)
				betaTarget = TargetOrientation - HALFPI; // right
			// Short way from the accumulator (the engine re-wraps Beta each frame).
			while (betaTarget < modLookBeta - PI) betaTarget += TWOPI;
			while (betaTarget > modLookBeta + PI) betaTarget -= TWOPI;
			WellBufferMe(betaTarget, &modLookBeta, &modLookBetaSpeed, 0.24f, 0.10f, true);
			modLookBeta = LimitRadianAngle(modLookBeta);
			// Take Beta back from the string engine so its ease cannot drag the side
			// view back towards the car heading.
			cam->Beta = modLookBeta;
			cam->BetaSpeed = 0.0f;
			// Release to the engine's own handler while still a few degrees out
			// (like SA). Do not force Beta onto the target here: that forced step
			// was the ~5 degree jump on release; the auto-fix below finishes it.
			if (lookNow == 0 && fabsf(LimitRadianAngle(modLookBeta - betaTarget)) < 0.02f) {
				modLookReturning = false;
				modLookBetaSpeed = 0.0f;
				modLookActive = false;
				modLookOwnsBeta = false;
				vcLookDist = -1.0f;
				vcLookClipLen = -1.0f;
			}
			// Use the radius captured when the look started (the follow camera's
			// own distance) and pin the pitch to the follow value. That keeps the
			// side view on the same radius and height as the normal camera, so
			// engaging the look does not lift or zoom the view, and the collision
			// below cannot feed back into the distance.
			float lookDist = (vcLookDist > 0.0f) ? vcLookDist : stringLength;
			if (lookDist < cam->CA_MIN_DISTANCE)
				lookDist = cam->CA_MIN_DISTANCE;
			cam->Alpha = modLookAlphaHold;
			cam->Source.x = TargetCoors.x - cosf(cam->Beta) * lookDist;
			cam->Source.y = TargetCoors.y - sinf(cam->Beta) * lookDist;
			cam->Source.z = TargetCoors.z + sinf(cam->Alpha + AlphaOffset) * lookDist + cam->m_fCloseInCarHeightOffset;
			// Pull the look camera in if its line to the car is blocked, so it
			// stops just short of the wall. The whole line is checked (not just the
			// horizontal one) so an overhang or canopy above the side view is
			// caught too; the camera recedes along that line, damped so a hit that
			// flickers as it swings cannot snap the view. The follow collision
			// below is skipped during a look, so this is the only clamp it gets.
			{
				CVector dir = cam->Source - TargetCoors;
				const float fullLen = dir.Magnitude();
				if (fullLen > 0.05f) {
					dir *= (1.0f / fullLen);
					float allowed = fullLen;
					pIgnoreEntity = (CEntity*)car;
					CColPoint lookCol;
					CEntity* lookHit = nil;
					if (WorldClass::ProcessLineOfSight(TargetCoors, cam->Source, lookCol, lookHit, true, false, false, true, false, false, true))
						allowed = max(0.5f, (lookCol.point - TargetCoors).Magnitude() - 0.35f);
					pIgnoreEntity = nil;
					if (vcLookClipLen < 0.0f)
						vcLookClipLen = allowed;
					else if (allowed < vcLookClipLen)
						vcLookClipLen += (allowed - vcLookClipLen) * min(1.0f, ms_fTimeStep * 0.6f);
					else
						vcLookClipLen += (allowed - vcLookClipLen) * min(1.0f, ms_fTimeStep * 0.12f);
					cam->Source = TargetCoors + dir * vcLookClipLen;
				}
			}
			// Publish the look into the cam flags the drive-by reads (the native
			// look would normally set these, but it is bypassed while smooth side
			// view is on). Publish the aim only when this camera is the active one;
			// a manual drive-by that has taken over the camera keeps its own aim.
			const bool pedAimCamera = IsPedAimCameraMode(cam->Mode);
			cam->LookingBehind = pedAimCamera ? false : (lookNow == 1);
			cam->LookingLeft = pedAimCamera ? false : (lookNow == 2);
			cam->LookingRight = pedAimCamera ? false : (lookNow == 3);
		} else if ((cam->m_bFixingBeta || setBeta) && !mouseChangesBeta && !reverseLookActive && nextDirectionIsForward) {
			WellBufferMe(TargetOrientation, &cam->Beta, &cam->BetaSpeed, 0.15f * stiffness, 0.007f * stiffness, true);
			if (TheCamera->m_bCamDirectlyBehind)
				cam->Beta = TargetOrientation;
			if (TheCamera->m_bCamDirectlyInFront)
				cam->Beta = TargetOrientation + PI;
			if (TheCamera->m_bUseTransitionBeta)
				cam->Beta = cam->m_fTransitionBeta;
			float d2 = (cam->Source - TargetCoors).Magnitude2D();
			if (d2 < 0.001f)
				d2 = cam->CA_MAX_DISTANCE;
			cam->Source.x = TargetCoors.x - cosf(cam->Beta) * d2;
			cam->Source.y = TargetCoors.y - sinf(cam->Beta) * d2;
			if (fabsf(LimitRadianAngle(TargetOrientation - cam->Beta)) < DEGTORAD(2.0f))
				cam->m_bFixingBeta = false;
		}
	}

	TheCamera->m_bCamDirectlyBehind = false;
	TheCamera->m_bCamDirectlyInFront = false;

	// ---- Reverse: smoothly swing round to look behind, and lock there ----
	if (reverseLookActive && !mouseChangesBeta) {
		cam->BetaSpeed = 0.0f;
		WellBufferMe(TargetOrientation + PI, &cam->Beta, &reverseBetaSpeed, 0.10f, 0.02f, true);
		float d2 = (cam->Source - TargetCoors).Magnitude2D();
		if (d2 < 0.1f)
			d2 = cam->CA_MAX_DISTANCE;
		cam->Source.x = TargetCoors.x - cosf(cam->Beta) * d2;
		cam->Source.y = TargetCoors.y - sinf(cam->Beta) * d2;
	}

	// ---- Keep the camera out of geometry ----
	// Skipped during a mod-driven look: the look gets its own pull-in clamp in
	// the look block above, and this one feeds the string length, which would
	// fight the look the way the general SA follow collision does.
	if (!modLookActive) {
		pIgnoreEntity = (CEntity*)car;
		CColPoint colPoint;
		CEntity* hitEntity = nil;
		if (WorldClass::ProcessLineOfSight(TargetCoors, cam->Source, colPoint, hitEntity, true, false, false, true, false, false, true))
			cam->Source = colPoint.point;
		pIgnoreEntity = nil;
	}

	// ---- Keep a little clearance above the ground and pull the near plane in ----
	// Without this, resting the camera on the ground lets the near clip plane
	// cut into it. SA shrinks the near clip plane in the same situation. Runs
	// during a mod-driven look too, so the side view cannot dip into the ground.
	if (mouseChangesBeta || modLookActive) {
		bool foundGround = false;
		const float groundZ = WorldClass::FindGroundZFor3DCoord(cam->Source.x, cam->Source.y, TargetCoors.z + 2.0f, &foundGround);
		if (foundGround) {
			if (cam->Source.z < groundZ + 0.25f)
				cam->Source.z = groundZ + 0.25f;
			const float clearance = cam->Source.z - groundZ;
			if (clearance < DefaultNearClip)
				RwCameraSetNearClipPlane(RwCamera, max(0.05f, clearance - 0.05f));
		}
	}

	// ---- Keep the camera above the water ----
	if (!seeUnderwater && CameraTarget.z >= -2.0f) {
		float level = -6000.0f;
		if (CWaterLevel::GetWaterLevelNoWaves(cam->Source.x, cam->Source.y, cam->Source.z, &level)) {
			if (cam->Source.z < level + 0.5f)
				cam->Source.z = level + 0.5f;
		}
	}

	// ---- Slight camera nudge when passing traffic very closely ----
	// A single out-and-back impulse per vehicle actually passed, not a continuous
	// lean: sitting next to a parked car does nothing, and the nudge only fires
	// while the car is moving forward faster than TrafficCamWobbleMinSpeed.
	// trafficCamWobble is a multiplier: 1.0 is the shipped gentle strength (70% of
	// the raw amplitude), 0 = off.
	float trafficWobbleTarget = 0.0f;
	if (trafficCamWobble > 0.0f) {
		const float kImpulseDuration = 0.3f;          // seconds, out and back
		const float kImpulseAmplitude = 0.7f * 0.05f; // 70% of the raw 0.05 cap
		static CEntity* trafficNearEntity = nil;      // vehicle whose pass-by already fired
		static float trafficImpulseTime = 0.0f;       // seconds left in the current nudge
		static float trafficImpulseSign = 0.0f;       // side the passed vehicle was on
		static float trafficImpulseScale = 1.0f;      // speed multiplier captured when the nudge fired
		// The sphere test is a world broadphase query; running it every frame
		// while driving was a measurable cause of microstutter. A passed vehicle
		// stays in the 2.2 m sphere for many frames, so a short cooldown loses
		// nothing.
		static int trafficQueryCooldown = 0;

		const float forwardSpeed = DotProduct(car->m_vecMoveSpeed, car->GetForward());

		// Scale the nudge with speed: 1x at MinSpeed, up to MaxMultiplier at
		// FullSpeed, so a slow crawl barely nudges and a fast pass is stronger.
		float trafficScale = 1.0f;
		if (trafficCamWobbleFullSpeed > trafficCamWobbleMinSpeed) {
			float t = clamp((forwardSpeed - trafficCamWobbleMinSpeed) / (trafficCamWobbleFullSpeed - trafficCamWobbleMinSpeed), 0.0f, 1.0f);
			trafficScale = 1.0f + t * (max(1.0f, trafficCamWobbleMaxMultiplier) - 1.0f);
		}

		if (trafficQueryCooldown > 0)
			trafficQueryCooldown--;
		CEntity* nearEnt = nil;
		bool didQuery = false;
		if (forwardSpeed > trafficCamWobbleMinSpeed && trafficQueryCooldown <= 0) {
			nearEnt = WorldClass::TestSphereAgainstWorld(car->GetPosition(), 2.2f, (CEntity*)car, false, true, false, false, false, false);
			trafficQueryCooldown = 6; // ~0.1 s at 50 FPS
			didQuery = true;
		}

		if (didQuery) {
			if (!nearEnt) {
				// Nothing in range: forget the last vehicle so a later pass can fire.
				trafficNearEntity = nil;
			} else if (nearEnt != trafficNearEntity && trafficImpulseTime <= 0.0f) {
				// A new vehicle entered the sphere while moving: one nudge, signed
				// by the side it is on. A vehicle that stays nearby cannot re-fire
				// (it is remembered until it leaves, which clears it above).
				CVector side = CrossProduct(car->GetForward(), CVector(0.0f, 0.0f, 1.0f));
				side.Normalise();
				float lateral = DotProduct(nearEnt->GetPosition() - car->GetPosition(), side);
				trafficImpulseSign = (lateral >= 0.0f) ? 1.0f : -1.0f;
				trafficImpulseScale = trafficScale;
				trafficImpulseTime = kImpulseDuration;
				trafficNearEntity = nearEnt;
			}
		}

		if (trafficImpulseTime > 0.0f) {
			// sin(pi * t) runs 0 -> peak -> 0 across the impulse; the roll buffer
			// below turns that into a smooth lean out and back.
			float progress = 1.0f - trafficImpulseTime / kImpulseDuration;
			trafficWobbleTarget = trafficImpulseSign * kImpulseAmplitude * trafficImpulseScale * sinf(progress * PI) * trafficCamWobble;
			trafficImpulseTime -= ms_fTimeStep * TimeStepToSeconds;
		}
	}

	cam->Front = TargetCoors - cam->Source;
	cam->Front.Normalise();

	// ---- Roll / wobble and helicopter tilt ----
	if (vc && isHeli) {
		float targetTilt = DotProduct(cam->Front, car->m_vecMoveSpeed);
		CVector upTarget = car->GetUp();
		upTarget.Normalise();
		int dir = targetTilt < 0.0f ? -1 : 1;
		if (heliTilt != 0.0f)
			targetTilt += TiltOverShoot[index] * targetTilt / heliTilt * dir;
		WellBufferMe(targetTilt, &heliTilt, &heliTiltSpeed, TiltTopSpeed[index], TiltSpeedStep[index], false);

		cam->Up = CVector(0.0f, 0.0f, 1.0f) - (CVector(0.0f, 0.0f, 1.0f) - upTarget) * heliTilt;
		cam->Up.Normalise();
		CVector left = CrossProduct(cam->Up, cam->Front);
		cam->Up = CrossProduct(cam->Front, left);
		cam->Up.Normalise();
	} else {
		float targetRoll = 0.0f;
		if (cameraWobble > 0.0f && !mouseChangesBeta) {
			float fwdSpeed = SpeedKphFactor * DotProduct(car->m_vecMoveSpeed, car->GetForward());
			if (fwdSpeed > MaxForwardSpeed)
				fwdSpeed = MaxForwardSpeed;

			float steer = (float)pad->GetSteeringLeftRight() / 128.0f;
			CVector fwdTarget = car->GetForward();
			fwdTarget.Normalise();
			float angleDiff = acosf(clamp(fabsf(DotProduct(fwdTarget, cam->Front)), 0.0f, 1.0f));

			targetRoll = steer * (fwdSpeed / MaxForwardSpeed) *
				(DEGTORAD(10.0f) * TiltOverShoot[index] + cam->f_max_role_angle) * sinf(angleDiff) * cameraWobble;
		}
		targetRoll += trafficWobbleTarget;
		WellBufferMe(targetRoll, &cam->f_Roll, &cam->f_rollSpeed, 0.15f, 0.07f, false);
		cam->GetVectorsReadyForRW();
	}

	cam->m_cvecTargetCoorsForFudgeInter = TargetCoors;
	// ---- VCS camera shake ----
	// Ported from ThirteenAG's WidescreenFixesPack
	// (MIT licensed, see licenses/WidescreenFixesPack.txt). Applied to the final
	// camera position, after Front/Up are settled, so it is the positional jolt
	// the reference gives rather than a rotation of the look-at direction. Only
	// cars (not bikes/boats/air) get it, exactly like the reference.
	if (vcsCamShake > 0.0f && isCar) {
		float vehSpeed = car->m_vecMoveSpeed.Magnitude();
		float shakeStart = (vcsCamShakeStartSpeed >= 0.0f) ? vcsCamShakeStartSpeed : VCSCamShakeStartSpeed;
		if (vehSpeed > shakeStart) {
			float shakeFactor = (min(vehSpeed, VCSCamShakeFullSpeed) - shakeStart) / VCSCamShakeRange / VCSCamShakeDivisor * vcsCamShake * VCSCamShakeScale();
			int r = rand();
			cam->Source.x += ((r & 0xF) - 7) * shakeFactor;
			cam->Source.y += (((r >> 4) & 0xF) - 7) * shakeFactor;
			cam->Source.z += (((r >> 8) & 0xF) - 7) * shakeFactor;
		}
	}

	lookingRelativelyLeft = false;
	lookingRelativelyRight = false;

	// ---- Turret control (Rhino / Firetruck) ----
	if (modernTurretControl && (car->m_modelIndex == Tank || car->m_modelIndex == FireTruk)) {
		CVector hi = Multiply3x3(cam->Front, car->GetMatrix());
		float angleToFace = (car->m_modelIndex == FireTruk ? -hi.Heading() : hi.Heading());

		if (angleToFace <= *GetDoomAnglePtrLR(car) + PI) {
			if (angleToFace < *GetDoomAnglePtrLR(car) - PI)
				angleToFace += TWOPI;
		} else {
			angleToFace -= TWOPI;
		}

		float neededTurn = angleToFace - *GetDoomAnglePtrLR(car);
		float turnPerFrame = ms_fTimeStep * (car->m_modelIndex == FireTruk ? 0.05f : 0.015f);
		if (neededTurn <= turnPerFrame) {
			if (neededTurn < -turnPerFrame)
				angleToFace = *GetDoomAnglePtrLR(car) - turnPerFrame;
		} else {
			angleToFace = turnPerFrame + *GetDoomAnglePtrLR(car);
		}

		if (car->m_modelIndex == Tank && *GetDoomAnglePtrLR(car) != angleToFace)
			DMAudio.PlayOneShot(car->m_audioEntityId, (isIII() ? 26 : 28), fabsf(angleToFace - *GetDoomAnglePtrLR(car)));

		*GetDoomAnglePtrLR(car) = angleToFace;
		if (*GetDoomAnglePtrLR(car) < -PI)
			*GetDoomAnglePtrLR(car) += TWOPI;
		else if (*GetDoomAnglePtrLR(car) > PI)
			*GetDoomAnglePtrLR(car) -= TWOPI;

		if (car->m_modelIndex == FireTruk) {
			float alphaToFace = atan2f(hi.z, hi.Magnitude2D()) + 0.2617994f;
			float neededAlphaTurn = alphaToFace - *GetDoomAnglePtrUD(car);
			float alphaTurnPerFrame = ms_fTimeStep * 0.02f;

			if (neededAlphaTurn > alphaTurnPerFrame) {
				*GetDoomAnglePtrUD(car) = alphaTurnPerFrame + *GetDoomAnglePtrUD(car);
			} else if (neededAlphaTurn >= -alphaTurnPerFrame) {
				*GetDoomAnglePtrUD(car) = alphaToFace;
			} else {
				*GetDoomAnglePtrUD(car) = *GetDoomAnglePtrUD(car) - alphaTurnPerFrame;
			}

			const float turretMinY = -0.34906587f;
			const float turretMaxY = 0.34906587f;
			if (*GetDoomAnglePtrUD(car) < turretMinY)
				*GetDoomAnglePtrUD(car) = turretMinY;
			else if (*GetDoomAnglePtrUD(car) > turretMaxY)
				*GetDoomAnglePtrUD(car) = turretMaxY;

			if (isReLCS && GetVehicleComponent(car, 8)) {
				CMatrix mat;
				mat.Attach(RwFrameGetMatrix(GetVehicleComponent(car, 8)));
				CVector pos = mat.GetPosition();
				mat.SetRotateZ(-(*GetDoomAnglePtrLR(car)));
				mat.GetPosition() = pos;
				mat.UpdateRW();
			}
		}
	} else if (modernDriveBy && !isHeli) {
		CVector hi = Multiply3x3(cam->Front, car->GetMatrix());
		if (hi.Heading() >= 0.5235987756f && hi.Heading() <= 2.617993878f)
			lookingRelativelyLeft = true;
		else if (hi.Heading() <= -0.5235987756f && hi.Heading() >= -2.617993878f)
			lookingRelativelyRight = true;
	}

	cam->ResetStatics = false;
}

// ---------------------------------------------------------------------------
// Explicit instantiations for GTA III and Vice City. CamSA.cpp is a separate
// translation unit and calls these templates, so the definitions are compiled
// here once for each game layout rather than being pulled into every caller.
// ---------------------------------------------------------------------------
template void Process_BehindBoat_Vanilla<CCamIII, CCameraIII, CVehicleIII, CWorldIII>(CCameraIII*, CCamIII*, CVehicleIII*, const CVector&, float);
template void Process_BehindBoat_Vanilla<CCamVC, CCameraVC, CVehicleVC, CWorldVC>(CCameraVC*, CCamVC*, CVehicleVC*, const CVector&, float);

template void Process_BehindBoat_VC<CCamIII, CCameraIII, CVehicleIII, CWorldIII, CColModelIII>(CCameraIII*, CCamIII*, CVehicleIII*, const CVector&, float);
template void Process_BehindBoat_VC<CCamVC, CCameraVC, CVehicleVC, CWorldVC, CColModelVC>(CCameraVC*, CCamVC*, CVehicleVC*, const CVector&, float);

template void Process_Cam_On_A_String_Vanilla<CCamIII, CCameraIII, CVehicleIII, CWorldIII, CColModelIII>(CCameraIII*, CCamIII*, CVehicleIII*, const CVector&, float);
template void Process_Cam_On_A_String_Vanilla<CCamVC, CCameraVC, CVehicleVC, CWorldVC, CColModelVC>(CCameraVC*, CCamVC*, CVehicleVC*, const CVector&, float);
