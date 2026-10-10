#include "ModernCarCam.h"

// ---------------------------------------------------------------------------
// CamSA.cpp - the San Andreas family vehicle follow camera.
//
// This engine reproduces the SA-style "follow car" camera; the SA, LCS, VCS
// and IV profiles all run through it. Its look and handling are selected by the
// distance/angle/fov table profiles and the feature flags (anchoring,
// stiffness, wobble, slope tilt, dynamic FOV, elastic string, reverse look).
//
// The vanilla III/VC engines are still reachable from here: when anchoring is
// active and the game/profile is vanilla, control is handed to CamVanilla.cpp.
//
// The implementation follows the reversed SA/LCS camera and the re3/reVC
// behaviour for the III/VC fallback (https://github.com/Hezkore/hez-gta-re3;
// see licenses/re3.txt). VCS shake and dynamic FOV are ported from
// ThirteenAG's WidescreenFixesPack (MIT licensed, see
// licenses/WidescreenFixesPack.txt).
// ---------------------------------------------------------------------------

// SA exposes CVehicle::m_bEnableMouseSteering (the inverse of the III/VC
// m_bDisableMouseSteering the shared engine was written against).
inline bool MouseSteeringDisabled() {
	if (isSA())
		return !*(bool*)0xC1CC02; // CVehicle::m_bEnableMouseSteering
	return m_bDisableMouseSteering;
}

// San Andreas vehicles whose special feature (a moving part) is driven by the
// same right-stick axes the vehicle camera reads. The numpad keys (2/4/6/8,
// Del, End) are bound to VEHICLE_TURRET* and PED_1RST_PERSON_LOOK_*, which map
// onto that stick, so without this the camera pivots while the feature moves.
// The vanilla CCam::Process_FollowCar_SA zeroes the stick look for exactly
// these model ids; the mod reads the stick directly, so it has to do the same.
static bool IsSpecialFeatureVehicle(int modelIndex) {
	switch (modelIndex) {
	case 406: // Dumper
	case 443: // Packer
	case 486: // Dozer
	case 520: // Hydra
	case 524: // Cement
	case 525: // Towtruck
	case 530: // Forklift
	case 531: // Tractor
	case 564: // RC Tiger
	case 592: // Andromada
		return true;
	default:
		return false;
	}
}

// Runs the authentic III/VC anchoring engine. Specialised to a no-op for SA:
// San Andreas has no "camera on a string" vehicle mode, so anchoring is
// ignored there (the SA follow camera always runs). The generic definition is
// only instantiated for III/VC, whose engines CamVanilla.cpp provides.
template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void
RunVanillaAnchor(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation)
{
	if (cam->Mode == MODE_BEHINDBOAT) {
		if (isVC())
			Process_BehindBoat_VC<CamClass, CameraClass, VehicleClass, WorldClass, ColModelClass>(TheCamera, cam, car, CameraTarget, TargetOrientation);
		else
			Process_BehindBoat_Vanilla<CamClass, CameraClass, VehicleClass, WorldClass>(TheCamera, cam, car, CameraTarget, TargetOrientation);
	} else {
		Process_Cam_On_A_String_Vanilla<CamClass, CameraClass, VehicleClass, WorldClass, ColModelClass>(TheCamera, cam, car, CameraTarget, TargetOrientation);
	}
}

template<>
void
RunVanillaAnchor<CCamSA, CCameraSA, CVehicleSA, CWorldSA, CColModelSA>(CCameraSA*, CCamSA*, CVehicleSA*, const CVector&, float)
{
}

// Applies a per-car [Car<model id>] camera override for the duration of the
// camera call, then restores the global settings. The camera tables are rebuilt
// on both apply and restore, so the change reaches the engine and leaves no
// trace once the frame is done.
namespace {
class CarCameraScope {
public:
	explicit CarCameraScope(int modelIndex) : active(false) {
		if (const CarCamSettings* override = FindCarCameraOverride(modelIndex)) {
			CaptureCarCamSettings(saved);
			ApplyCarCamSettings(*override);
			active = true;
		}
	}
	~CarCameraScope() {
		if (active)
			ApplyCarCamSettings(saved);
	}
private:
	bool active;
	CarCamSettings saved;
};
}

template<class CamClass, class CameraClass, class VehicleClass, class WorldClass, class ColModelClass>
void
Process_FollowCar_SA(CameraClass* TheCamera, CamClass* cam, VehicleClass* car, const CVector& CameraTarget, float TargetOrientation)
{
	// Persistent camera state the vanilla CCam keeps as members; the mod engine
	// has to hold it in static locals instead.
	static CVector m_aTargetHistoryPosOne;
	static CVector m_aTargetHistoryPosTwo;
	static float lastBeta = -9999.0f;
	static float lastAlpha = -9999.0f;
	static float stepsLeftToChangeBetaByMouse;
	static float flt_9BF250;
	static bool alphaCorrected;
	static float heightIncreaseMult;
	static float camPitchTilt = 0.0f;
	static float camPitchTiltSpeed = 0.0f;
	static float slopeAirborneTime = 0.0f;   // seconds spent off the ground (slope tilt continuity)
	static float slopeLastGroundTilt = 0.0f;  // last ground tilt, held through short hops

	if (!cam->CamTargetEntity->IsVehicle())
		return;

	// Swap in this car's [Car<model id>] camera override (if any) for the whole
	// camera call; the scope restores the globals on every exit path.
	CarCameraScope carCameraScope(car->m_modelIndex);

	if (!ginputLoaded) {
		if (GInput_Load(&ginputPad)) {
			OnGInputSettingsReload();
			ginputPad->SendEvent(GINPUT_EVENT_REGISTER_SETTINGS_RELOAD_CALLBACK, OnGInputSettingsReload);
			ginputLoaded = 2;
		} else
			ginputLoaded = 1;
	}

	// GTA IV (and other driver-centred cameras): shift the orbit target to the
	// driver's seat instead of the car centre. The offset is rotated by the car's
	// heading only (yaw), not its full matrix: body roll and road pitch were
	// rotating the "up"/"side" parts of the seat into the forward direction, so
	// the target wandered and the camera settled back onto the car centre after
	// a few turns. Yaw-only keeps the seat fixed relative to the car's heading.
	CVector adjustedTarget = CameraTarget;
	if (cameraDriverOffset.x != 0.0f || cameraDriverOffset.y != 0.0f || cameraDriverOffset.z != 0.0f) {
		const float h = car->GetForward().Heading();
		const CVector flatForward(-sinf(h), cosf(h), 0.0f);
		const CVector flatRight(cosf(h), sinf(h), 0.0f);
		adjustedTarget += flatRight * cameraDriverOffset.x
			+ flatForward * cameraDriverOffset.y
			+ CVector(0.0f, 0.0f, cameraDriverOffset.z);
	}

	// San Andreas has no "on a string" vehicle mode, so anchoring is ignored
	// there; the SA follow camera always runs. cameraAnchoring > 0 selects the
	// rigid anchor (its magnitude scales the anchor stiffness).
	bool useAnchoring = !isSA() && cameraAnchoring > 0.0f;
	if (useAnchoring) {
		RunVanillaAnchor<CamClass, CameraClass, VehicleClass, WorldClass, ColModelClass>(
			TheCamera, cam, car, adjustedTarget, TargetOrientation);
		return;
	}

	CVector TargetCoors = adjustedTarget;
	TargetCoors.z += cameraHeightOffset;

	uint8 camSetArrPos = 0;

	// Vehicle classification differs per game, so it is delegated to the
	// per-game wrapper (handling flags on III/VC, eVehicleType on SA).
	bool isPlane = car->IsPlaneType();
	bool isHeli = car->IsHeliType();
	bool isBike = car->IsBikeType();
	bool isCar = car->IsCar() && !isPlane && !isHeli && !isBike;

	CPad* pad = &pad0;

	// Pad look inputs, read once per frame (also consumed by nextDirectionIsForward).
	// Look behind is the vehicle look-behind only; the on-foot look-behind
	// (GetLookBehindForPed) shares the pad's right-stick click with the
	// sub-mission / hide-landing-gear key, so it must never drive the car camera.
	const bool lookBehindCar = pad->GetLookBehindForCar();
	const bool lookLeftKey = pad->GetLookLeft();
	const bool lookRightKey = pad->GetLookRight();
	// Helicopters and the RC Baron yaw with the look-left/right keys, so their
	// side look stays off (vanilla does the same in CCam::Process).
	const bool disableSideLook = DisableVehicleSideLook(car->m_modelIndex, isHeli);
	const bool anyLookInput = lookBehindCar || lookLeftKey || lookRightKey;

	// True while the follow camera is in control: no look input is held and it
	// was already looking forward.
	uint8 nextDirectionIsForward = !anyLookInput &&
		cam->DirectionWasLooking == LOOKING_FORWARD;

	if (distanceProfile == PROFILE_LCS && car->m_modelIndex == FireTruk) {
		camSetArrPos = 7;
	}
	else if (car->m_modelIndex == RcBandit || (isVC() && !isReLCS && car->m_modelIndex == MI_VC_RCBARON)) {
		camSetArrPos = 5;
	}
	else if (isVC() && (car->m_modelIndex == RcRaider || car->m_modelIndex == RcGoblin)) {
		camSetArrPos = 6;
	}
	else if (car->IsBoat()) {
		camSetArrPos = 4;
	}
	else if (isBike) {
		camSetArrPos = 1;
	}
	else if (isPlane) {
		camSetArrPos = 3;
	}
	else if (isHeli) {
		camSetArrPos = 2;
	}

	const float (*CARCAM_SET)[15] = (distanceProfile == PROFILE_CUSTOM) ? CARCAM_SET_CUSTOM :
		((distanceProfile == PROFILE_VANILLA) ? CARCAM_SET_VANILLA :
		((distanceProfile == PROFILE_LCS) ? CARCAM_SET_LCS : CARCAM_SET_SA));

	const float *CarZoomModes = (distanceProfile == PROFILE_CUSTOM) ? CarZoomModesCustom :
		((distanceProfile == PROFILE_VANILLA) ? ((isVC() || vehicleSpecificZoom > 0.0f) ? CarZoomModesVC : CarZoomModesIII) :
		((distanceProfile == PROFILE_LCS) ? CarZoomModesLCS : CarZoomModesSA));

	const float (*ANGLES_CARCAM_SET)[15] = (anglesProfile == PROFILE_CUSTOM) ? CARCAM_SET_CUSTOM :
		((anglesProfile == PROFILE_VANILLA) ? CARCAM_SET_VANILLA :
		((anglesProfile == PROFILE_LCS) ? CARCAM_SET_LCS : CARCAM_SET_SA));

	// RC helicopters and planes reuse the heli/plane alpha rows; everything else
	// past the boat row falls back to the car row.
	uint8 alphaArrPos = (camSetArrPos > 4 ? (isPlane ? 3 : (isHeli ? 2 : 0)) : camSetArrPos);
	float zoomModeAlphaOffset = 0.0f;

	if (isHeli && car->GetStatus() == STATUS_PLAYER_REMOTE) {
		if (anglesProfile == PROFILE_CUSTOM)
			zoomModeAlphaOffset = ZmTwoAlphaOffsetCustom[alphaArrPos];
		else if (anglesProfile == PROFILE_VANILLA)
			zoomModeAlphaOffset = isVC() ? ZmTwoAlphaOffsetVC[alphaArrPos] : ZmTwoAlphaOffsetIII[alphaArrPos];
		else if (anglesProfile == PROFILE_LCS)
			zoomModeAlphaOffset = ZmTwoAlphaOffsetLCS[alphaArrPos];
		else
			zoomModeAlphaOffset = ZmTwoAlphaOffset[alphaArrPos];
	}
	else {
		switch ((int)TheCamera->CarZoomIndicator) {
			// near
			case 1:
				if (anglesProfile == PROFILE_CUSTOM)
					zoomModeAlphaOffset = ZmOneAlphaOffsetCustom[alphaArrPos];
				else if (anglesProfile == PROFILE_VANILLA)
					zoomModeAlphaOffset = isVC() ? ZmOneAlphaOffsetVC[alphaArrPos] : ZmOneAlphaOffsetIII[alphaArrPos];
				else if (anglesProfile == PROFILE_LCS)
					zoomModeAlphaOffset = ZmOneAlphaOffsetLCS[alphaArrPos];
				else
					zoomModeAlphaOffset = ZmOneAlphaOffset[alphaArrPos];
				break;
			// mid
			case 2:
				if (anglesProfile == PROFILE_CUSTOM)
					zoomModeAlphaOffset = ZmTwoAlphaOffsetCustom[alphaArrPos];
				else if (anglesProfile == PROFILE_VANILLA)
					zoomModeAlphaOffset = isVC() ? ZmTwoAlphaOffsetVC[alphaArrPos] : ZmTwoAlphaOffsetIII[alphaArrPos];
				else if (anglesProfile == PROFILE_LCS)
					zoomModeAlphaOffset = ZmTwoAlphaOffsetLCS[alphaArrPos];
				else
					zoomModeAlphaOffset = ZmTwoAlphaOffset[alphaArrPos];
				break;
			// far
			case 3:
				if (anglesProfile == PROFILE_CUSTOM)
					zoomModeAlphaOffset = ZmThreeAlphaOffsetCustom[alphaArrPos];
				else if (anglesProfile == PROFILE_VANILLA)
					zoomModeAlphaOffset = isVC() ? ZmThreeAlphaOffsetVC[alphaArrPos] : ZmThreeAlphaOffsetIII[alphaArrPos];
				else if (anglesProfile == PROFILE_LCS)
					zoomModeAlphaOffset = ZmThreeAlphaOffsetLCS[alphaArrPos];
				else
					zoomModeAlphaOffset = ZmThreeAlphaOffset[alphaArrPos];
				break;
			default:
				break;
		}
	}

	ColModelClass* carCol = (ColModelClass*)car->GetColModel();
	if (!carCol)
		return; // collision model not loaded yet (e.g. a vehicle added at an unused ID)
	float colMaxZ = carCol->boundingBox.max.z;  // As opposed to LCS and SA, VC does this: carCol->boundingBox.max.z - carCol->boundingBox.min.z;
	float approxCarLength = 2.0f * fabsf(carCol->boundingBox.min.y); // SA taxi min.y = -2.95, max.z = 0.883502f

	// Turned off by default on LCS_CAM
	if(heightIncreaseOnBike) {
		if (!isBike) {
			heightIncreaseMult = 0.0f;
		} else {
			// Increase colMaxZ slowly when there is a passenger on bike
			if (car->pPassengers[0])
			{
				if (heightIncreaseMult < 1.0f) {
					heightIncreaseMult = min(1.0f, ms_fTimeStep * 0.02f + heightIncreaseMult);
				}
			}
			else
			{
				if (heightIncreaseMult > 0.0f) {
					heightIncreaseMult = max(0.0f, heightIncreaseMult - ms_fTimeStep * 0.02f);
				}
			}
			colMaxZ += 0.4f * heightIncreaseMult;
		}
	}

	float hackedZoomValue = TheCamera->CarZoomValueSmooth;

	// GTA III has no per-vehicle zoom table, so emulate its per-vehicle zoom
	// from the CarZoomModes rows. The camera only transitions far-to-near, and
	// vehicleSpecificZoom blends the per-vehicle row with the generic car row
	// (index 0): 1 = full per-vehicle, 0 = generic.
	if (vehicleSpecificZoom > 0.0f && isIII()) {
		auto zoomVal = [&](int row) {
			return CarZoomModes[0 + row * 5] +
				(CarZoomModes[alphaArrPos + row * 5] - CarZoomModes[0 + row * 5]) * vehicleSpecificZoom;
		};
		if ((int)TheCamera->CarZoomIndicator == 3)
			hackedZoomValue = zoomVal(2);
		else if ((int)TheCamera->CarZoomIndicator == 2) {
			hackedZoomValue = zoomVal(1) +
							  (hackedZoomValue - 1.9f) * (zoomVal(2) - zoomVal(1)) / (3.9f - 1.9f);
		} else if ((int)TheCamera->CarZoomIndicator == 1) {
			hackedZoomValue = zoomVal(0) +
				(hackedZoomValue - 0.05f) * (zoomVal(1) - zoomVal(0)) / (1.9f - 0.05f);
		}

		// Keep the zoom from creeping closer in tunnels and other cramped spots.
		if (hackedZoomValue < zoomVal(0))
			hackedZoomValue = zoomVal(0);
	}

	// San Andreas already has a per-vehicle zoom table, so CarZoomValueSmooth is
	// the game's own distance and is used as-is for the SA profile. When the
	// selected distance table is not SA's (the Custom profile, or LCS/VCS forced
	// on SA) that table has to become the distance instead: map the game's SA
	// zoom through it, exactly as the III/VC engines do (see CamVanilla). Without
	// this the [Custom] distances have no effect on SA.
	if (isSA() && (distanceProfile == PROFILE_CUSTOM || distanceProfile == PROFILE_LCS)) {
		auto srcVal = [&](int row) { return CarZoomModesSA[alphaArrPos + row * 5]; };
		auto dstVal = [&](int row) { return CarZoomModes[alphaArrPos + row * 5]; };
		int ind = (int)TheCamera->CarZoomIndicator;
		if (ind == 3)
			hackedZoomValue = dstVal(2);
		else if (ind == 2 && srcVal(2) != srcVal(1))
			hackedZoomValue = dstVal(1) +
				(hackedZoomValue - srcVal(1)) * (dstVal(2) - dstVal(1)) / (srcVal(2) - srcVal(1));
		else if (ind == 1 && srcVal(1) != srcVal(0))
			hackedZoomValue = dstVal(0) +
				(hackedZoomValue - srcVal(0)) * (dstVal(1) - dstVal(0)) / (srcVal(1) - srcVal(0));

		// Keep the zoom from creeping closer in tunnels and other cramped spots.
		if (hackedZoomValue < dstVal(0))
			hackedZoomValue = dstVal(0);
	}

	float newDistance;
	float minDistForThisCar;
	if (distanceProfile == PROFILE_VANILLA) {
		CVector dimensions = carCol->boundingBox.max - carCol->boundingBox.min;
		float baseDist = dimensions.Magnitude();
		if (isBike)
			baseDist *= 1.45f;
		newDistance = baseDist + 0.1f + hackedZoomValue;
		if (isVC()) {
			if (car->m_modelIndex == RcRaider || car->m_modelIndex == RcGoblin)
				newDistance += 6.0f;
			else if (!isReLCS && car->m_modelIndex == MI_VC_RCBARON)
				newDistance += 9.5f;
		}
		minDistForThisCar = min(baseDist * 0.6f, 3.5f);
	} else {
		float perVehOffset = CARCAM_SET[camSetArrPos][1];
		float genericOffset = CARCAM_SET[0][1];
		float zoomDistOffset = genericOffset + (perVehOffset - genericOffset) * max(0.0f, vehicleSpecificZoom);
		newDistance = hackedZoomValue + zoomDistOffset + approxCarLength;
		if (isVC()) {
			if (car->m_modelIndex != RcRaider && car->m_modelIndex != RcGoblin) {
				if (!isReLCS && car->m_modelIndex == MI_VC_RCBARON)
					newDistance += 9.5f;
			} else
				newDistance += 6.0f;
		}
		minDistForThisCar = approxCarLength * CARCAM_SET[camSetArrPos][3];
	}

	if (cameraMinDistance >= 0.0f)
		minDistForThisCar = cameraMinDistance;

	if (elasticStringPhysics > 0.0f && (isCar || isBike || car->IsBoat())) {
		float forwardSpeed = DotProduct(car->m_vecMoveSpeed, car->GetForward()) * SpeedKphFactor;
		newDistance += clamp(forwardSpeed * (2.0f / MaxForwardSpeed), -1.0f, 2.0f) * elasticStringPhysics;
	}

	newDistance += CameraDistanceOffsetForZoom((int)TheCamera->CarZoomIndicator);
	newDistance *= cameraDistanceScale;

	if (distanceProfile == PROFILE_VANILLA || anglesProfile == PROFILE_VANILLA || distanceProfile == PROFILE_CUSTOM || anglesProfile == PROFILE_CUSTOM) {
		float vehHeight = carCol->boundingBox.max.z - carCol->boundingBox.min.z;
		if (isBike) {
			TargetCoors += 0.6f * vehHeight * car->GetUp();
		}
		else if (isHeli && car->GetStatus() != STATUS_PLAYER_REMOTE) {
			TargetCoors.x += 0.6f * car->GetUp().x * colMaxZ;
			TargetCoors.y += 0.6f * car->GetUp().y * colMaxZ;
			TargetCoors.z += 0.6f * car->GetUp().z * colMaxZ;
		}
		else {
			TargetCoors.z += (isVC() ? 0.8f * vehHeight : (vehHeight - 0.1f));
		}
	}
	else {
		if (!isHeli || car->GetStatus() == STATUS_PLAYER_REMOTE) {
			float radiusToStayOutside = colMaxZ * CARCAM_SET[camSetArrPos][0] - CARCAM_SET[camSetArrPos][2];
			if (radiusToStayOutside > 0.0f) {
				TargetCoors.z += radiusToStayOutside;
				newDistance += radiusToStayOutside;
				zoomModeAlphaOffset += 0.3f / newDistance * radiusToStayOutside;
			}
		}
		else {
			// The 0.6 factor matches the game's fTestShiftHeliCamTarget offset.
			TargetCoors.x += 0.6f * car->GetUp().x * colMaxZ;
			TargetCoors.y += 0.6f * car->GetUp().y * colMaxZ;
			TargetCoors.z += 0.6f * car->GetUp().z * colMaxZ;
		}
	}

	// Slope tilt. pitchTilt is the master strength; the uphill/downhill keys
	// override it per direction (<0 = use the master value). The tilt follows
	// the ground while the wheels are down; in the air it holds the last ground
	// value briefly (so small bumps and hops do not reset it), then eases to the
	// vehicle's nose so the camera lines up with the car while jumping.
	float uphillStrength = (pitchTiltUphill >= 0.0f) ? pitchTiltUphill : pitchTilt;
	float downhillStrength = (pitchTiltDownhill >= 0.0f) ? pitchTiltDownhill : pitchTilt;
	bool tiltEnabled = uphillStrength > 0.0f || downhillStrength > 0.0f;

	float targetSlopeTilt = 0.0f;

	if (tiltEnabled && (isCar || isBike)) {
		// The car's nose slope (positive = pointing up), scaled into the tilt
		// angle for the current camera position (carAlpha > 0 is downhill).
		const float rawForwardSlope = atan2f(car->GetForward().z, car->GetForward().Magnitude2D());
		auto slopeTiltFor = [&](float forwardSlope) {
			float carAlpha = -forwardSlope * cosf(cam->Beta - (car->GetForward().Heading() - HALFPI));
			return (carAlpha > 0.0f)
				? clamp(carAlpha, 0.0f, 0.35f) * downhillStrength
				: clamp(carAlpha, -0.35f, 0.0f) * uphillStrength;
		};

		bool wheelsOnGround = isBike ? (GetMysteriousWheelRelatedThingBike(car) > 0) : (GetWheelsOnGround(car) > 0);
		if (wheelsOnGround) {
			slopeAirborneTime = 0.0f;

			// Dead-zone: ignore shallow slopes so flat roads cannot jitter the
			// camera. The threshold is subtracted, so the tilt grows smoothly
			// from zero as the slope passes it instead of stepping on.
			float forwardSlope = rawForwardSlope;
			if (fabsf(forwardSlope) <= pitchTiltMinAngle)
				forwardSlope = 0.0f;
			else
				forwardSlope -= (forwardSlope > 0.0f ? pitchTiltMinAngle : -pitchTiltMinAngle);

			slopeLastGroundTilt = slopeTiltFor(forwardSlope);
			targetSlopeTilt = slopeLastGroundTilt;
		} else {
			slopeAirborneTime += ms_fTimeStep * TimeStepToSeconds;
			if (slopeAirborneTime <= pitchTiltAirHoldTime) {
				// Continuity: keep the last ground tilt through short hops.
				targetSlopeTilt = slopeLastGroundTilt;
			} else {
				// Ease toward the nose over the blend time, so the camera settles
				// onto the vehicle while flying instead of snapping to default.
				const float noseTilt = slopeTiltFor(rawForwardSlope);
				const float blend = clamp(
					(slopeAirborneTime - pitchTiltAirHoldTime) / max(0.01f, pitchTiltAirBlendTime),
					0.0f, 1.0f);
				targetSlopeTilt = slopeLastGroundTilt + (noseTilt - slopeLastGroundTilt) * blend;
			}
		}
	} else {
		slopeAirborneTime = 0.0f;
	}

	if (tiltEnabled) {
		float bufferTopSpeed = isBike ? 0.09f : 0.15f;
		float bufferStep = isBike ? 0.04f : 0.07f;
		WellBufferMe(targetSlopeTilt, &camPitchTilt, &camPitchTiltSpeed, bufferTopSpeed, bufferStep, true);
	}
	else {
		camPitchTilt = 0.0f;
		camPitchTiltSpeed = 0.0f;
	}

	if (isVC()) {
		((CCamVC*)cam)->m_fTilt = camPitchTilt;
		((CCamVC*)cam)->m_fTiltSpeed = camPitchTiltSpeed;
	}

	// Vice City nudges the RC Goblin camera up by the amount SA bakes into its
	// tweak angle for that model.
	if (isVC() && car->m_modelIndex == RcGoblin)
		zoomModeAlphaOffset += 0.178997f;

	float minDistForVehType = CARCAM_SET[camSetArrPos][4];
	if (cameraMinDistance >= 0.0f)
		minDistForVehType = cameraMinDistance;
	if ((int)TheCamera->CarZoomIndicator == 1 && (camSetArrPos < 2 || (distanceProfile == PROFILE_LCS && camSetArrPos == 7))) {
		minDistForVehType = minDistForVehType * 0.65f;
	}

	float nextDistance = max(newDistance, minDistForVehType);

	cam->CA_MAX_DISTANCE = newDistance;
	cam->CA_MIN_DISTANCE = 3.5f;

	float currentBaseFOV = (fovProfile == PROFILE_CUSTOM) ? customBaseFOV : DefaultFOV;
	float maxFOVAdd = 30.0f;
	float fovStartSpeed = 0.4f;
	if (dynamicSpeedFOVMaxFOV >= 0.0f)
		maxFOVAdd = dynamicSpeedFOVMaxFOV;
	if (dynamicSpeedFOVStartSpeed >= 0.0f)
		fovStartSpeed = dynamicSpeedFOVStartSpeed;

	if (cam->ResetStatics) {
		cam->FOV = currentBaseFOV;

		if (isIII()) {
			// GTA III tracks the idle-camera entry time in its vehicle camera.
			if (TheCamera->m_bIdleOn)
				TheCamera->m_uiTimeWeEnteredIdle = m_snTimeInMilliseconds;
		}
	}
	else {
		if (dynamicSpeedFOV > 0.0f && (isCar || isBike)) {
			float forwardSpeed = DotProduct(car->GetForward(), car->m_vecMoveSpeed);
			if (forwardSpeed > fovStartSpeed)
				cam->FOV += (forwardSpeed - fovStartSpeed) * ms_fTimeStep * dynamicSpeedFOV;
		}

		if (cam->FOV > currentBaseFOV) {
			// 0.98 is the game's CAR_FOV_FADE_MULT decay per step; IV and any
			// ini override can wind the FOV down faster.
			float fovDecay = (dynamicSpeedFOVDecay > 0.0f && dynamicSpeedFOVDecay < 1.0f) ? dynamicSpeedFOVDecay : 0.98f;
			cam->FOV = pow(fovDecay, ms_fTimeStep) * (cam->FOV - currentBaseFOV) + currentBaseFOV;
		}

		float fovCap = currentBaseFOV + maxFOVAdd * ((dynamicSpeedFOV > 0.0f) ? dynamicSpeedFOV : 1.0f);
		if (cam->FOV <= fovCap)
		{
			if (cam->FOV < currentBaseFOV)
				cam->FOV = currentBaseFOV;
		} else
			cam->FOV = fovCap;

		if (!(dynamicSpeedFOV > 0.0f)) {
			cam->FOV = currentBaseFOV;
		}
	}

	// The mod owns look here (Beta) because SA's own look derives its distance
	// from cam+0xD0, which does not match the mod camera, and the mod look path
	// also skips the follow collision while looking. Smooth side view off moves
	// to the angle immediately; on buffers it. State: 0 idle | 1 behind | 2 left
	// | 3 right | 4 behind returning | 5 side returning. lookReturnFrames forces
	// the return to finish (and hands Beta back to the follow) even while the car
	// is turning.
	static int lookState = 0;
	static float lookBetaSpeed = 0.0f;
	static int lookReturnFrames = 0;

	// Reverse look-behind state (driven further below); reset on (re)entry.
	static float reverseTime = 0.0f;
	static int reverseState = 0;
	static float reverseBetaSpeed = 0.0f;
	// Latched when the player takes the mouse during a reverse: it blocks the
	// swing from re-arming so the mouse keeps Beta until the reverse ends.
	static bool reversePlayerOverride = false;

	// Runs once when the player just entered the car.
	if (cam->ResetStatics) {
		cam->ResetStatics = false;
		cam->Rotating = false;
		cam->m_bCollisionChecksOn = true;
		cam->f_Roll = 0.0f;
		cam->f_rollSpeed = 0.0f;

		// GTA III's garage-exit camera does not settle correctly, so the mod
		// re-seeds the angle there; elsewhere it skips while that camera is active.
		if (isIII() || !TheCamera->m_bJustCameOutOfGarage)
		{
			cam->Alpha = 0.0f;
			cam->Beta = car->GetForward().Heading() - HALFPI;
			if (TheCamera->m_bCamDirectlyInFront) {
				cam->Beta += PI;
			}
		}

		cam->BetaSpeed = 0.0;
		cam->AlphaSpeed = 0.0;
		cam->Distance = newDistance;
		cam->DistanceSpeed = 0.0;
		reverseTime = 0.0f;
		reverseState = 0;
		reverseBetaSpeed = 0.0f;
		reversePlayerOverride = false;
		lookState = 0;
		lookBetaSpeed = 0.0f;
		lookReturnFrames = 0;
		// Do not jump camPitchTilt to the raw slope target here: this branch also
		// runs when returning from look left/right/behind, and the instant change
		// read as the camera slamming to the top. The buffer above tracks the
		// slope smoothly instead.
		camPitchTiltSpeed = 0.0f;
		slopeAirborneTime = 0.0f;
		slopeLastGroundTilt = 0.0f;

		cam->Front.x = -(cos(cam->Beta) * cos(cam->Alpha));
		cam->Front.y = -(sin(cam->Beta) * cos(cam->Alpha));
		cam->Front.z = sin(cam->Alpha);

		m_aTargetHistoryPosOne = TargetCoors - nextDistance * cam->Front;

		m_aTargetHistoryPosTwo = TargetCoors - newDistance * cam->Front;

		if (isIII() || !TheCamera->m_bJustCameOutOfGarage)
			cam->Alpha = -zoomModeAlphaOffset - camPitchTilt;
	}

	// ---- Reverse look-behind state (drives Beta below) ----
	// This owns cam->Beta while active so the normal follow logic does not also
	// integrate Beta every frame; that double update was the wobble that stopped
	// the camera from settling behind the car. Declared above, reset on (re)entry.
	const float reverseSpeed = DotProduct(car->GetForward(), car->m_vecMoveSpeed);
	// reverseTime is in seconds, so ReverseCameraDelay reads naturally.
	if (reverseSpeed < -0.05f)
		reverseTime += ms_fTimeStep * TimeStepToSeconds;
	else
		reverseTime = 0.0f;

	if (!nextDirectionIsForward) {
		// Player took over (look left/right/behind): abort the auto swing.
		reverseTime = 0.0f;
		reverseState = 0;
		reverseBetaSpeed = 0.0f;
	}

	// Once the reverse ends, a later reverse may arm the swing again.
	if (reverseTime == 0.0f)
		reversePlayerOverride = false;

	// The player-mouse override blocks re-arming, so the swing cannot re-grab
	// Beta after the mouse has taken it (see the free-look cancel below).
	if (reverseState != 0 && !reverseCam)
		reverseState = 0;
	else if (reverseCam && reverseTime > reverseCamDelay && !reversePlayerOverride)
		reverseState = 1;
	else if (reverseState == 1 && reverseSpeed > 0.05f)
		reverseState = 2;

	if (reverseState == 0)
		reverseBetaSpeed = 0.0f;

	// Look state: holding an input selects it, releasing swings back. The same
	// state drives both modes; smooth side view only changes how the angle is
	// applied.
	int lookNow = lookBehindCar ? 1 : (!disableSideLook && lookLeftKey ? 2 : (!disableSideLook && lookRightKey ? 3 : 0));
	if (lookNow != 0) {
		lookState = lookNow;
		lookReturnFrames = 0;
	} else if (lookState == 1) {
		lookState = 4;
		lookReturnFrames = 0;
	} else if (lookState == 2 || lookState == 3) {
		lookState = 5;
		lookReturnFrames = 0;
	}

	cam->Front = TargetCoors - m_aTargetHistoryPosOne;
	cam->Front.Normalise();

	// Rotate the camera around the car towards its direction of travel.
	float camRightHeading = cam->Front.Heading() - HALFPI;
	if (camRightHeading < -PI)
		camRightHeading = camRightHeading + TWOPI;

	float desiredRightHeading;
	if (car->m_vecMoveSpeed.Magnitude2D() <= 0.02f)
		desiredRightHeading = camRightHeading;
	else
		desiredRightHeading = car->m_vecMoveSpeed.Heading() - HALFPI;

	if (desiredRightHeading < camRightHeading - PI)
		desiredRightHeading += TWOPI;
	else if (desiredRightHeading > camRightHeading + PI)
		desiredRightHeading -= TWOPI;

	float profileStiffness = (masterProfile == PROFILE_LCS) ? 0.4f : 0.25f;
	float stiffnessMult = profileStiffness * max(0.0f, cameraStiffness);

	// Heading follow strength. 1.0 is the profile's normal follow; lower values
	// make the camera hold its yaw longer before swinging in behind the car (the
	// IV profile softens it on GTA III, whose follow is naturally more eager).
	// Exposed as [Features] HeadingFollow for custom profiles.
	const float headingFollowScale = max(0.0f, headingFollow);
	float v70 = ms_fTimeStep * CARCAM_SET[camSetArrPos][10] * (stiffnessMult * 4.0f) * headingFollowScale;
	float v153 = ms_fTimeStep * CARCAM_SET[camSetArrPos][11] * (stiffnessMult * 4.0f) * headingFollowScale;

	float a6f = (car->m_vecMoveSpeed - DotProduct(car->m_vecMoveSpeed, cam->Front) * cam->Front).Magnitude();

	float v76 = min(1.0f, v70 * a6f) * (desiredRightHeading - camRightHeading);
	if (v76 <= v153)
	{
		if (v76 < -v153)
			v76 = -v153;
	}
	else
	{
		v76 = v153;
	}
	float targetBeta = camRightHeading + v76;

	while (targetBeta < cam->Beta - PI)
		targetBeta += TWOPI;
	while (targetBeta > cam->Beta + PI)
		targetBeta -= TWOPI;

	float carPosChange = (TargetCoors - m_aTargetHistoryPosTwo).Magnitude();
	if (carPosChange < newDistance && newDistance > minDistForThisCar) {
		newDistance = max(minDistForThisCar, carPosChange);
	}
	float maxAlphaAllowed = (maxPitchAngle >= 0.0f) ? maxPitchAngle : ANGLES_CARCAM_SET[camSetArrPos][13];

	// Raise the pitch limit so the camera cannot clip into the car when it is
	// nearly still, or whenever fixTheBug keeps the fix on.
	if (fixTheBug || car->m_vecMoveSpeed.MagnitudeSqr() < 0.04f)
		if (distanceProfile != PROFILE_LCS || car->m_modelIndex != FireTruk)
			if (!isBike || GetMysteriousWheelRelatedThingBike(car) > 3)
				if (!isHeli && (!isPlane || GetWheelsOnGround(car))) {
					CVector out = CrossProduct(car->GetForward(), CVector(0.0f, 0.0f, 1.0f));
					out.Normalise();
					CVector v173 = CrossProduct(out, car->GetForward());
					v173.Normalise();
					float v83 = DotProduct(v173, cam->Front);
					if (v83 > 0.0)
					{
						float v88 = asinf(fabsf(sinf(cam->Beta - (car->GetForward().Heading() - HALFPI))));
						float v200;
						if (v88 <= atan2f(carCol->boundingBox.max.x, -carCol->boundingBox.min.y))
						{
							v200 = (1.5f - carCol->boundingBox.min.y) / cosf(v88);
						}
						else
						{
							float a6g = 1.2f + carCol->boundingBox.max.x;
							v200 = a6g / cos(max(0.0f, HALFPI - v88));
						}
						maxAlphaAllowed = cos(cam->Beta - (car->GetForward().Heading() - HALFPI)) * atan2f(car->GetForward().z, car->GetForward().Magnitude2D())
							+ atan2f(TargetCoors.z - car->GetPosition().z + GetHeightAboveRoad(car, ColModelClass), v200 * 1.2f);
						if (isCar && GetWheelsOnGround(car) > 1
							&& fabsf(DotProduct(car->m_vecTurnSpeed, car->GetForward())) < 0.05f)
						{
							maxAlphaAllowed += cosf(cam->Beta - (car->GetForward().Heading() - HALFPI) + HALFPI) * atan2f(car->GetRight().z, car->GetRight().Magnitude2D());
						}
					}
				}

	// The camera's default pitch for every profile. Previously a non-vanilla
	// angles profile fed the camera's own angle back in via asin(Front.z), which
	// made the camera drift pitch endlessly.
	float targetAlpha = -zoomModeAlphaOffset - camPitchTilt;
	if (targetAlpha <= maxAlphaAllowed)
	{
		float minPitch = (minPitchAngle >= 0.0f) ? minPitchAngle : ANGLES_CARCAM_SET[camSetArrPos][14];
		if (targetAlpha < -minPitch)
			targetAlpha = -minPitch;
	}
	else
	{
		targetAlpha = maxAlphaAllowed;
	}
	float maxAlphaBlendAmount = ms_fTimeStep * CARCAM_SET[camSetArrPos][6];
	float targetAlphaBlendAmount = (1.0f - pow(CARCAM_SET[camSetArrPos][5], ms_fTimeStep)) * (targetAlpha - cam->Alpha);
	if (targetAlphaBlendAmount <= maxAlphaBlendAmount)
	{
		if (targetAlphaBlendAmount < -maxAlphaBlendAmount)
			targetAlphaBlendAmount = -maxAlphaBlendAmount;
	}
	else
	{
		targetAlphaBlendAmount = maxAlphaBlendAmount;
	}

	// The camera only follows the game's own camera-look bindings (the right
	// stick or whatever the player has bound). No hard-coded numpad keys are added
	// here, so the game's binds drive the camera exactly like the vanilla camera.
	// The one exception is the moving-part vehicles, where the same bindings run
	// the special feature instead; see specialFeatureVehicle below.
	float stickX = (float)-(pad->GetCarGunLeftRight());
	float stickY = (float)(pad->GetCarGunUpDown());

	// KeyboardFreeLook off means the keyboard/turret keys must not move the
	// camera: read the axis only while a pad is in the player's hands (GInput
	// decides; without GInput the keyboard and pad share the axis, so the game's
	// own look keys are used as a keyboard tell-tale instead).
	if (!KeyboardFreeLookAxisEnabled() ||
	    (!keyboardFreeLook && (pad->GetLookLeft() || pad->GetLookRight() || pad->GetLookBehindForCar()))) {
		stickX = 0.0f;
		stickY = 0.0f;
	}

	// SA gates this on m_bUseMouse3rdPerson so num2/num8 do not move the camera
	// with Keyboard & Mouse controls; checking the actual pad state works better
	// with GInput. On SA the vertical axis is always read, so the pad can look up
	// and down.
	const bool ginputHasPad = GInputPadInHands();
	if (!isSA() && (ginputLoaded == 2 ? !ginputHasPad : m_bUseMouse3rdPerson))
		stickY = 0.0f;
	else {
		// GInput does not hook the VC Y-axis invert option, so apply it here.
		if (ginputHasPad && padSettings.InvertLook)
			stickY = -stickY;

		// VC's hidden Y-axis invert option.
		if (isVC())
			if (*(bool*)0xA10AF7)
				stickY = -stickY;
	}

	// Exponential smoothing so the gamepad camera does not feel rough.
	static float smoothedStickX = 0.0f;
	static float smoothedStickY = 0.0f;
	float stickSmoothing = min(1.0f, ms_fTimeStep * 0.25f);
	smoothedStickX += (stickX - smoothedStickX) * stickSmoothing;
	smoothedStickY += (stickY - smoothedStickY) * stickSmoothing;
	stickX = smoothedStickX;
	stickY = smoothedStickY;

	const bool stickActive = fabsf(stickX) > 0.05f || fabsf(stickY) > 0.05f;

	float v103 = cam->FOV * 0.0125f;

	float xMovement = fabsf(stickX) * (v103 * 0.071428575) * stickX * 0.007f * 0.007f;
	float yMovement = fabsf(stickY) * (v103 * 0.042857144) * stickY * 0.007f * 0.007f;

	// On the moving-part vehicles the right stick drives the feature, not the
	// camera, so the camera must ignore it. This mirrors where the vanilla
	// camera zeroes the stick look. The mouse path below is separate and still
	// works, so free look is not lost.
	const bool specialFeatureVehicle = isSA() && IsSpecialFeatureVehicle(car->m_modelIndex);
	if (specialFeatureVehicle) {
		xMovement = 0.0f;
		yMovement = 0.0f;
	}

	bool correctAlpha = true;
	if (!isCar || car->m_modelIndex != CarWithHydraulics) {
		correctAlpha = false;
	} else {
		xMovement = 0.0f;
		yMovement = 0.0f;
	}

	if (!nextDirectionIsForward) {
		yMovement = 0.0;
		xMovement = 0.0;
	}

	// The III/VC "steering nudges the camera" tweak reads the driver's ped
	// objective, which is a III/VC-only field layout. San Andreas does not need
	// it (its own camera handles it).
	if (!isSA() && (camSetArrPos == 0 || (distanceProfile == PROFILE_LCS && camSetArrPos == 7))) {
		// III/VC only ties the left stick to steering in Classic Configuration, so
		// this rarely fires with GInput or a pad.
		if (fabsf(pad->GetSteeringUpDown()) > 120.0f) {

			// 13 on III / 16 on VC is OBJECTIVE_LEAVE_VEHICLE.
			if (car->pDriver && GetPedObjective(car->pDriver) != (uint32)(isIII() ? 13 : 16)) {
				yMovement += fabsf(pad->GetSteeringUpDown()) * (cam->FOV * 0.0125 * 0.042857144) * pad->GetSteeringUpDown() * 0.007f * 0.007f * 0.5;
			}
		}
	}

	if (yMovement > 0.0)
		yMovement = yMovement * 0.5;

	// Pressing a look key must end any mouse hold immediately, otherwise the camera
	// stays stuck at the mouse angle until the 50-step release finishes.
	if (anyLookInput)
		stepsLeftToChangeBetaByMouse = 0.0f;
	bool mouseChangesBeta = false;
	// True only while the player is actively moving the look input this frame,
	// not during the trailing 50-step release hold. The reverse takeover keys off
	// this so a leftover hold from before the reverse cannot suppress the swing.
	bool lookInputThisFrame = false;

	// Free look is native to SA, so it runs there even when the mod's toggle is
	// off. It is disabled during a drive-by, matching SA, whose original mouse
	// handling there is buggy.
	if ((mouseFreeLook || isSA()) && m_bUseMouse3rdPerson && !GetDisablePlayerControls(pad) && nextDirectionIsForward)
	{
		float mouseY = GetMouseControllerY() * 2.0f;
		float mouseX = GetMouseControllerX() * -2.0f;

		// III gates free look on the mouse-steering option; SA keeps its native
		// mouse look regardless. SA's m_bVehicleMouseLook has no III equivalent.
		if ((mouseX != 0.0 || mouseY != 0.0) && (isSA() || MouseSteeringDisabled()))
		{
			float v113 = cam->FOV * 0.0125;
			yMovement = mouseY * v113 * GetMouseAccel(TheCamera); // Same as SA, horizontal sensitivity.
			cam->BetaSpeed = 0.0;
			cam->AlphaSpeed = 0.0;
			xMovement = mouseX * v113 * GetMouseAccel(TheCamera);
			targetAlpha = cam->Alpha;
			stepsLeftToChangeBetaByMouse = 1.0f * 50.0f;
			mouseChangesBeta = true;
			lookInputThisFrame = true;
		}
		else if (stepsLeftToChangeBetaByMouse > 0.0f)
		{
			// Finish the rotation by decaying speed once the mouse stops moving.
			cam->BetaSpeed = 0.0;
			cam->AlphaSpeed = 0.0;
			yMovement = 0.0;
			xMovement = 0.0;
			targetAlpha = cam->Alpha;
			stepsLeftToChangeBetaByMouse = max(0.0f, stepsLeftToChangeBetaByMouse - ms_fTimeStep);
			mouseChangesBeta = true;
		}
	}

	// Gamepad right stick drives the camera directly (the III/VC free-look)
	// instead of through SA's damped speed blending, which fights the player.
	// This runs after the mouse block so a stick push is not cancelled by the
	// mouse hold from the previous frame.
	if ((mouseFreeLook || isSA()) && stickActive && !GetDisablePlayerControls(pad) && nextDirectionIsForward && !specialFeatureVehicle) {
		xMovement = fabsf(stickX) * (v103 * 0.071428575f) * stickX * 0.007f * 0.007f;
		yMovement = fabsf(stickY) * (v103 * 0.042857144f) * stickY * 0.007f * 0.007f;
		cam->BetaSpeed = 0.0f;
		cam->AlphaSpeed = 0.0f;
		targetAlpha = cam->Alpha;
		stepsLeftToChangeBetaByMouse = 50.0f;
		mouseChangesBeta = true;
		lookInputThisFrame = true;
	}

	// Ease back to the default camera angle after free look, like the III/VC
	// camera, instead of letting the native blend jump straight back. The scale
	// ramps from a gentle 0.2 up to full once the hold has run out.
	static bool freeLookWasActive = false;
	static float freeLookReturn = 0.0f;
	const bool freeLookNow = mouseChangesBeta;
	if (freeLookWasActive && !freeLookNow)
		freeLookReturn = 1.0f;
	freeLookWasActive = freeLookNow;
	float freeLookReturnScale = 1.0f;
	if (freeLookReturn > 0.0f) {
		// CameraReturnTime is how many seconds the auto-return lasts;
		// CameraReturnSpeed scales how quickly it settles back.
		float returnRate = max(0.05f, cameraReturnSpeed) / max(0.02f, cameraReturnTime);
		freeLookReturn = max(0.0f, freeLookReturn - ms_fTimeStep * returnRate);
		freeLookReturnScale = 0.2f + 0.8f * (1.0f - freeLookReturn);
	}

	if (correctAlpha) {
		if (previousMode != MODE_CAMONASTRING)
			alphaCorrected = false;

		if (!alphaCorrected && fabsf(zoomModeAlphaOffset + cam->Alpha) > 0.05f) {
			yMovement = (-zoomModeAlphaOffset - cam->Alpha) * 0.05f;
		} else
			alphaCorrected = true;
	}
	float alphaSpeedFromStickY = yMovement * CARCAM_SET[camSetArrPos][12];
	float betaSpeedFromStickX = xMovement * CARCAM_SET[camSetArrPos][12];
	float v117 = CARCAM_SET[camSetArrPos][9];
	float angleChangeStep = pow(CARCAM_SET[camSetArrPos][8], ms_fTimeStep);
	float targetBetaWithStickBlendAmount = betaSpeedFromStickX + (targetBeta - cam->Beta) / max(ms_fTimeStep, 1.0f);

	if (targetBetaWithStickBlendAmount < -v117)
		targetBetaWithStickBlendAmount = -v117;
	else if (targetBetaWithStickBlendAmount > v117)
		targetBetaWithStickBlendAmount = v117;

	// Free look during a reverse takes Beta over: cancel the swing (both the
	// look-behind swing, state 1, and its revert, state 2) so it cannot fight the
	// player. The override latch is set only on an actual look input this frame,
	// not the trailing release hold: a leftover hold from before the reverse must
	// not suppress the swing for the whole manoeuvre. This runs before the Beta
	// integration so the mouse's angle is applied on this very frame.
	if (mouseChangesBeta) {
		reverseState = 0;
		reverseBetaSpeed = 0.0f;
	}
	if (lookInputThisFrame)
		reversePlayerOverride = true;

	float angleChangeStepLeft = 1.0 - angleChangeStep;
	if ((reverseState == 0 && lookState == 0) || mouseChangesBeta) {
		cam->BetaSpeed = targetBetaWithStickBlendAmount * angleChangeStepLeft + angleChangeStep * cam->BetaSpeed;
		if (fabsf(cam->BetaSpeed) < 0.0001f)
			cam->BetaSpeed = 0.0f;

		float v121;
		if (mouseChangesBeta)
			v121 = betaSpeedFromStickX;
		else
			v121 = ms_fTimeStep * cam->BetaSpeed * freeLookReturnScale;
		cam->Beta = v121 + cam->Beta;
	}
	
	// SA re-derives Beta from the camera direction while leaving the garage.
	if (TheCamera->m_bJustCameOutOfGarage)
		cam->Beta = GetATanOfXY(cam->Front.x, cam->Front.y) + PI;

	if (cam->Beta < -PI)
		cam->Beta += TWOPI;
	else if (cam->Beta > PI)
		cam->Beta -= TWOPI;

	if ((camSetArrPos <= 1 || (distanceProfile == PROFILE_LCS && camSetArrPos == 7)) && targetAlpha < cam->Alpha && carPosChange >= newDistance) {
		if (isCar && GetWheelsOnGround(car) > 1 ||
			isBike && GetMysteriousWheelRelatedThingBike(car) > 1)
			alphaSpeedFromStickY += (targetAlpha - cam->Alpha) * 0.075f;
	}

	cam->AlphaSpeed = angleChangeStepLeft * alphaSpeedFromStickY + angleChangeStep * cam->AlphaSpeed;
	float maxAlphaSpeed = v117;
	if (alphaSpeedFromStickY > 0.0f)
		maxAlphaSpeed = maxAlphaSpeed * 0.5;

	if (cam->AlphaSpeed <= maxAlphaSpeed)
	{
		float minAlphaSpeed = -maxAlphaSpeed;
		if (cam->AlphaSpeed < minAlphaSpeed)
			cam->AlphaSpeed = minAlphaSpeed;
	}
	else
	{
		cam->AlphaSpeed = maxAlphaSpeed;
	}

	if (fabsf(cam->AlphaSpeed) < 0.0001f)
		cam->AlphaSpeed = 0.0f;

	float alphaWithSpeedAccounted;
	if (mouseChangesBeta)
	{
		alphaWithSpeedAccounted = alphaSpeedFromStickY + targetAlpha;
		cam->Alpha += alphaSpeedFromStickY;
	}
	else
	{
		alphaWithSpeedAccounted = ms_fTimeStep * cam->AlphaSpeed + targetAlpha;
		cam->Alpha += targetAlphaBlendAmount * freeLookReturnScale;
	}

	if (cam->Alpha <= maxAlphaAllowed)
	{
		float minAlphaAllowed = -((minPitchAngle >= 0.0f) ? minPitchAngle : CARCAM_SET[camSetArrPos][14]);
		if (minAlphaAllowed > cam->Alpha)
		{
			cam->Alpha = minAlphaAllowed;
			cam->AlphaSpeed = 0.0;
		}
	}
	else
	{
		cam->Alpha = maxAlphaAllowed;
		cam->AlphaSpeed = 0.0;
	}

	// Ignore angle changes too small to matter.
	if (fabsf(lastAlpha - cam->Alpha) < 0.0001f)
		cam->Alpha = lastAlpha;

	lastAlpha = cam->Alpha;

	if (fabsf(lastBeta - cam->Beta) < 0.0001f)
		cam->Beta = lastBeta;

	lastBeta = cam->Beta;

	// ---- Reverse look-behind: drive Beta with its own state and speed so it
	// settles smoothly behind the car instead of fighting the follow logic ----
	// (The free-look cancel above has already cleared the swing for this frame;
	// this guard is kept as a safety net so the drive below can never run while
	// the mouse owns Beta.)
	if (mouseChangesBeta) {
		reverseState = 0;
		reverseBetaSpeed = 0.0f;
	}
	if (reverseState != 0) {
		float target = (reverseState == 1)
			? car->GetForward().Heading() - HALFPI + PI // look behind the car
			: car->GetForward().Heading() - HALFPI;     // back to behind the car
		while (target < cam->Beta - PI) target += TWOPI;
		while (target > cam->Beta + PI) target -= TWOPI;
		cam->BetaSpeed = 0.0f;
		WellBufferMe(target, &cam->Beta, &reverseBetaSpeed, 0.10f, 0.02f, true);
		lastBeta = cam->Beta;
		if (reverseState == 2 && fabsf(LimitRadianAngle(cam->Beta - target)) < 0.02f) {
			reverseState = 0;
			reverseBetaSpeed = 0.0f;
		}
	}

	// ---- Look (owned Beta) ----
	// Owns Beta while active so the follow logic above does not integrate it too.
	// The return is a fixed-duration smoothstep back to the angle the follow will
	// hold (velocity heading while moving), so the hand-off is seamless, quick and
	// cannot overshoot or stay anchored.
	// If the player free-looks during the look return, abandon it immediately so
	// the mouse owns Beta (otherwise they fight until the return reaches default).
	if (mouseChangesBeta && (lookState == 4 || lookState == 5)) {
		lookState = 0;
		lookBetaSpeed = 0.0f;
		lookReturnFrames = 0;
	}
	if (lookState != 0) {
		// Swing-in uses a stable target (the car heading) so the ease does not chase
		// the jittery velocity vector; the return uses the angle the follow camera
		// actually settles at (velocity heading while moving) for a seamless hand-off.
		float lookBase;
		if (lookState == 1 || lookState == 2 || lookState == 3)
			lookBase = car->GetForward().Heading() - HALFPI;
		else
			lookBase = (car->m_vecMoveSpeed.Magnitude2D() <= 0.02f)
				? (car->GetForward().Heading() - HALFPI)
				: (car->m_vecMoveSpeed.Heading() - HALFPI);
		float target;
		if (lookState == 1)
			target = lookBase + PI;        // behind: look from the front of the car
		else if (lookState == 2)
			target = lookBase + HALFPI;    // left
		else if (lookState == 3)
			target = lookBase - HALFPI;    // right
		else
			target = lookBase;             // 4 / 5: return to behind the car
		while (target < cam->Beta - PI) target += TWOPI;
		while (target > cam->Beta + PI) target -= TWOPI;
		cam->BetaSpeed = 0.0f;
		if (!smoothSideView) {
			// Move to the angle immediately.
			cam->Beta = LimitRadianAngle(target);
			lookBetaSpeed = 0.0f;
			if (lookState == 4 || lookState == 5) {
				lookState = 0;
				lookReturnFrames = 0;
			}
		} else {
			// The same buffered ease swings in and returns, giving a natural
			// accelerate/decelerate feel. The rate is the SA SmoothSideView speed
			// the user tuned (0.25 / 0.10); the VC engine's 0.24 / 0.10 below is
			// the equivalent feel and is deliberately left alone. Once the return
			// is close, hand Beta back to the follow.
			WellBufferMe(target, &cam->Beta, &lookBetaSpeed, 0.25f, 0.10f, true);
			if (lookState == 4 || lookState == 5) {
				lookReturnFrames++;
				if (fabsf(LimitRadianAngle(cam->Beta - target)) < 0.02f || lookReturnFrames > 240) {
					lookState = 0;
					lookBetaSpeed = 0.0f;
					lookReturnFrames = 0;
				}
			}
		}
		lastBeta = cam->Beta;
	}

	// Publish the look direction into the cam flags the game's drive-by reads
	// (CAutomobile::DoDriveByShootings / CPed::IsPedDoingDriveByShooting use
	// Cams[ActiveCam].LookingLeft/Right). The native look that normally sets these
	// is suppressed on SA, so they are set here instead; CCam::Process clears them
	// right after, so 0x527DC9 is NOPed in the hooks.
	bool wantBehind = (lookState == 1);
	bool wantLeft = (lookState == 2);
	bool wantRight = (lookState == 3);
	// Modern drive-by: aim to the side whenever the camera is angled relative to
	// the car (not only while a look key is held), like the III/VC lookingRelatively*.
	// The fire truck is excluded: its fire button sprays water and the sideways
	// weapon aim would make it shoot the gun instead.
	if (modernDriveBy && !IsFireTruk(car)) {
		const CVector relFwd = Multiply3x3(cam->Front, car->GetMatrix());
		const float relH = relFwd.Heading();
		if (relH >= 0.5235987756f && relH <= 2.617993878f)        // > 30 and < 150 deg
			wantLeft = true;
		else if (relH <= -0.5235987756f && relH >= -2.617993878f) // < -30 and > -150 deg
			wantRight = true;
	}

	// Lock-shoot-direction: while the drive-by fire is held, keep the direction the
	// shot started in (so looking elsewhere mid-burst does not move the aim).
	// Separately configurable for keyboard/mouse and gamepad aiming.
	static bool lockBehind = false, lockLeft = false, lockRight = false;
	{
		// GTA III has no CPad::GetCarGunFired (its mod address is null; see
		// Pad.cpp), so calling it there jumped to address 0 and crashed the SA
		// engine on III. III never reports a held car-gun fire in this camera, so
		// treat it as not firing (short-circuit keeps the null call out).
		const bool firing = isIII() ? false : pad->GetCarGunFired();
		const bool aimingWithPad = pad->GetCarGunLeftRight() != 0 || pad->GetCarGunUpDown() != 0;
		const bool lock = aimingWithPad ? lockShootDirJOY : lockShootDirKBM;
		if (!(firing && lock)) {
			lockBehind = wantBehind;
			lockLeft = wantLeft;
			lockRight = wantRight;
		}
	}
	// Publish the aim only when this vehicle camera is the active one; while a
	// manual drive-by has taken the camera to a ped/aim mode, leave the aim alone.
	const bool pedAimCamera = IsPedAimCameraMode(cam->Mode);
	cam->LookingBehind = pedAimCamera ? false : lockBehind;
	cam->LookingLeft = pedAimCamera ? false : lockLeft;
	cam->LookingRight = pedAimCamera ? false : lockRight;

	cam->Front.x = -(cos(cam->Beta) * cos(cam->Alpha));
	cam->Front.y = -(sin(cam->Beta) * cos(cam->Alpha));
	cam->Front.z = sin(cam->Alpha);

	// Slight camera nudge when passing traffic very closely. A single out-and-back
	// impulse per vehicle actually passed, not a continuous lean: sitting next to
	// a parked car does nothing, and the nudge only fires while the car is moving
	// forward faster than TrafficCamWobbleMinSpeed.
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

	// Steering camera wobble: authentic GTA III / Vice City roll and inertia.
	float targetRoll = 0.0f;
	bool manualCameraMovement = mouseChangesBeta || fabsf(stickX) > 0.05f || fabsf(stickY) > 0.05f || !nextDirectionIsForward;
	if (cameraWobble > 0.0f && (isCar || isBike || car->IsBoat() || isPlane) && !manualCameraMovement) {
		float forwardSpeed = DotProduct(car->m_vecMoveSpeed, car->GetForward()) * SpeedKphFactor;
		if (forwardSpeed > MaxForwardSpeed)
			forwardSpeed = MaxForwardSpeed;
		else if (forwardSpeed < -MaxForwardSpeed)
			forwardSpeed = -MaxForwardSpeed;

		float steer = (float)pad->GetSteeringLeftRight();
		float steerFactor = -(steer / 128.0f) * (forwardSpeed / MaxForwardSpeed);

		CVector fwdTarget = car->GetForward();
		fwdTarget.Normalise();
		float angleDiff = acosf(clamp(fabsf(DotProduct(fwdTarget, cam->Front)), 0.0f, 1.0f));

		float tiltOvershoot = (isCar || isBike) ? 1.05f : (isPlane ? 1.0f : 0.0f);
		float maxRoll = cam->f_max_role_angle;
		if (maxRoll == 0.0f)
			maxRoll = DEGTORAD(5.0f);

		// Vanilla attenuation by sin(angleDiff).
		targetRoll = steerFactor * (DEGTORAD(10.0f) * tiltOvershoot + maxRoll) * sinf(angleDiff) * cameraWobble;
	}
	targetRoll += trafficWobbleTarget;
	WellBufferMe(targetRoll, &cam->f_Roll, &cam->f_rollSpeed, 0.15f, 0.07f, false);

	cam->Distance = newDistance;
	cam->DistanceSpeed = 0.0f;

	cam->GetVectorsReadyForRW();
	if (cameraWobble > 0.0f && cam->f_Roll != 0.0f) {
		CVector unrolledUp(0.0f, 0.0f, 1.0f);
		if (fabsf(cam->Front.x) < 0.0001f && fabsf(cam->Front.y) < 0.0001f)
			cam->Front.x = 0.0001f;
		CVector right = CrossProduct(cam->Front, unrolledUp);
		right.Normalise();
		CVector up0 = CrossProduct(right, cam->Front);
		up0.Normalise();
		cam->Up = up0 * cosf(cam->f_Roll) - right * sinf(cam->f_Roll);
		cam->Up.Normalise();
	}
	TheCamera->m_bCamDirectlyBehind = false;
	TheCamera->m_bCamDirectlyInFront = false;

	cam->Source = TargetCoors - newDistance * cam->Front;

	// GTA IV-style over-the-shoulder side offset.
	if (cameraLateralOffset != 0.0f) {
		CVector side = CrossProduct(car->GetForward(), CVector(0.0f, 0.0f, 1.0f));
		side.Normalise();
		cam->Source += side * cameraLateralOffset;
	}

	// (VCS camera shake moved below, after the final camera vectors are built.)

	cam->m_cvecTargetCoorsForFudgeInter = TargetCoors;
	float v140 = alphaWithSpeedAccounted + zoomModeAlphaOffset;
	float v144 = -(cos(cam->Beta) * cos(v140));
	float v145 = -(sin(cam->Beta) * cos(v140));
	float v146 = sin(v140);

	m_aTargetHistoryPosOne.x = TargetCoors.x - v144 * nextDistance;
	m_aTargetHistoryPosOne.y = TargetCoors.y - v145 * nextDistance;
	m_aTargetHistoryPosOne.z = TargetCoors.z - v146 * nextDistance;

	m_aTargetHistoryPosTwo.x = TargetCoors.x - v144 * newDistance;
	m_aTargetHistoryPosTwo.y = TargetCoors.y - v145 * newDistance;
	m_aTargetHistoryPosTwo.z = TargetCoors.z - v146 * newDistance;

	// SA runs SetColVarsVehicle here, which cannot be called safely from the mod,
	// so the LCS FollowCar / SA FollowPedWithMouse collision detection is used
	// instead. It also runs while the look (side view / look behind) owns the
	// camera, since a held look key clears nextDirectionIsForward; otherwise the
	// side view would clip straight through walls.
	if (nextDirectionIsForward || lookState != 0) {
		// Move the camera out of any collision.
		float v206 = powf(0.99, ms_fTimeStep);
		flt_9BF250 = (v206 * flt_9BF250) + ((1.0f - v206) * car->m_vecMoveSpeed.Magnitude());

		CColPoint foundCol;
		CEntity* foundEnt;
		pIgnoreEntity = (CEntity*)cam->CamTargetEntity;
		if (WorldClass::ProcessLineOfSight(TargetCoors, cam->Source, foundCol, foundEnt, true, flt_9BF250 < 0.1f, false, true, false, true, false))
		{
			float obstacleTargetDist = (TargetCoors - foundCol.point).Magnitude();
			float obstacleCamDist = newDistance - obstacleTargetDist;
			if (!foundEnt->IsPed() || obstacleCamDist <= 1.0f)
			{
				cam->Source = foundCol.point;
				cam->Distance = obstacleTargetDist;
				cam->DistanceSpeed = 0.0f;
				if (obstacleTargetDist < 1.2f)
				{
					RwCameraSetNearClipPlane(RwCamera, max(0.05f, obstacleTargetDist - 0.3f));
				}
			}
			else
			{
				if (!WorldClass::ProcessLineOfSight(foundCol.point, cam->Source, foundCol, foundEnt, true, flt_9BF250 < 0.1f, false, true, false, true, false))
				{
					float lessClip = obstacleCamDist - 0.35f;
					if (lessClip <= 0.9f)
						RwCameraSetNearClipPlane(RwCamera, lessClip);
					else
						RwCameraSetNearClipPlane(RwCamera, 0.9f);
				} else {
					obstacleTargetDist = (TargetCoors - foundCol.point).Magnitude();
					cam->Source = foundCol.point;
					cam->Distance = obstacleTargetDist;
					cam->DistanceSpeed = 0.0f;
					if (obstacleTargetDist < 1.2f)
					{
						float lessClip = obstacleTargetDist - 0.3f;
						if (lessClip >= 0.05f)
							RwCameraSetNearClipPlane(RwCamera, lessClip);
						else
							RwCameraSetNearClipPlane(RwCamera, 0.05f);
					}
				}
			}
		}
		pIgnoreEntity = nil;
		float nearClip = GetNearPlane();
		float radius = tanf(cam->FOV * 0.017453292f * 0.5f) * GetAspectRatio() * 1.1f;

		// If the camera intersects a surface the view can see through the world;
		// nudge the camera out. SA and LCS unroll this loop.
		for (int i = 0;
			i <= 5 && WorldClass::TestSphereAgainstWorld((nearClip * cam->Front) + cam->Source, radius * nearClip, nil, true, true, false, true, false, false);
			i++) {

			CVector surfaceCamDist = ms_testSpherePoint.point - cam->Source;
			CVector v52 = DotProduct(surfaceCamDist, cam->Front) * cam->Front;
			float v86 = (surfaceCamDist - v52).Magnitude() / radius;

			if (v86 > nearClip)
				v86 = nearClip;
			if (v86 < 0.1)
				v86 = 0.1;
			if (nearClip > v86)
				RwCameraSetNearClipPlane(RwCamera, v86);

			if (v86 == 0.1f)
				cam->Source += (TargetCoors - cam->Source) * 0.3f;

			nearClip = GetNearPlane();
			radius = tanf(cam->FOV * 0.017453292f * 0.5f) * GetAspectRatio() * 1.1f;
		}
	}

	// ---- LCS-specific clamps ----

	if (camSetArrPos == 5 && cam->Source.z < 1.0f) // RC Bandit and Baron
		cam->Source.z = 1.0f;

	// Clamp the camera out of the ground at a specific Liberty City location.
	if (isReLCS || isIII())
		if (cam->Source.x > 11.0f && cam->Source.x < 91.0f) {
			if (cam->Source.y > -680.0f && cam->Source.y < -600.0f && cam->Source.z < 24.4f)
				cam->Source.z = 24.4f;
		}

	if (!seeUnderwater) {
		// Emulates CCam::FixSourceAboveWaterLevel.
		if (CameraTarget.z >= -2.0f) {
			float level = -6000.0;
			// The +0.5 offset is needed in GTA III.
			if (CWaterLevel::GetWaterLevelNoWaves(cam->Source.x, cam->Source.y, cam->Source.z, &level)) {
				if (cam->Source.z < level + 0.5f)
					cam->Source.z = level + 0.5f;
			}
		}
	}

	cam->Front = TargetCoors - cam->Source;

	cam->GetVectorsReadyForRW();

	// VCS camera shake. Ported from ThirteenAG's WidescreenFixesPack
	// (MIT licensed, see licenses/WidescreenFixesPack.txt). Applied to the final
	// camera position, after Front/Up are settled, so it is the positional jolt
	// the reference gives rather than a rotation of the look-at direction. Only
	// cars (not bikes/boats/air) get it, exactly like the reference. The 1.5x
	// boost is Enhanced-only on III/VC (see VCSCamShakeScale).
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
	// SA code from CAutomobile::TankControl/FireTruckControl. The turret angles,
	// vehicle component frames and audio entity live at III/VC offsets, so this
	// path is left out on SA (a documented limitation).
	if (!isSA() && modernTurretControl && (car->m_modelIndex == Tank || car->m_modelIndex == FireTruk)) {
		CVector hi = Multiply3x3(cam->Front, car->GetMatrix());

		// The III/VC fire truck turret angle is reversed.
		float angleToFace = (car->m_modelIndex == FireTruk ? -hi.Heading() : hi.Heading());

		if (angleToFace <= *GetDoomAnglePtrLR(car) + PI) {
			if (angleToFace < *GetDoomAnglePtrLR(car) - PI)
				angleToFace = angleToFace + TWOPI;
		} else {
			angleToFace = angleToFace - TWOPI;
		}

		float neededTurn = angleToFace - *GetDoomAnglePtrLR(car);
		float turnPerFrame = ms_fTimeStep * (car->m_modelIndex == FireTruk ? 0.05f : 0.015f);
		if (neededTurn <= turnPerFrame) {
			if (neededTurn < -turnPerFrame)
				angleToFace = *GetDoomAnglePtrLR(car) - turnPerFrame;
		} else {
			angleToFace = turnPerFrame + *GetDoomAnglePtrLR(car);
		}

		if (car->m_modelIndex == Tank && *GetDoomAnglePtrLR(car) != angleToFace) {
			DMAudio.PlayOneShot(car->m_audioEntityId, (isIII() ? 26 : 28), fabsf(angleToFace - *GetDoomAnglePtrLR(car)));
		}
		*GetDoomAnglePtrLR(car) = angleToFace;

		if (*GetDoomAnglePtrLR(car) < -PI) {
			*GetDoomAnglePtrLR(car) += TWOPI;
		} else if (*GetDoomAnglePtrLR(car) > PI) {
			*GetDoomAnglePtrLR(car) -= TWOPI;
		}

		// The fire truck turret also moves vertically.
		if (car->m_modelIndex == FireTruk) {
			float alphaToFace = atan2f(hi.z, hi.Magnitude2D()) + 0.2617994f;
			float neededAlphaTurn = alphaToFace - *GetDoomAnglePtrUD(car);
			float alphaTurnPerFrame = ms_fTimeStep * 0.02f;

			if (neededAlphaTurn > alphaTurnPerFrame) {
				neededTurn = alphaTurnPerFrame;
				*GetDoomAnglePtrUD(car) = neededTurn + *GetDoomAnglePtrUD(car);
			} else {
				if (neededAlphaTurn >= -alphaTurnPerFrame) {
					*GetDoomAnglePtrUD(car) = alphaToFace;
				} else {
					*GetDoomAnglePtrUD(car) = *GetDoomAnglePtrUD(car) - alphaTurnPerFrame;
				}
			}

			float turretMinY = -0.34906587f;
			float turretMaxY = 0.34906587f;
			if (turretMinY <= *GetDoomAnglePtrUD(car)) {
				if (*GetDoomAnglePtrUD(car) > turretMaxY)
					*GetDoomAnglePtrUD(car) = turretMaxY;
			} else {
				*GetDoomAnglePtrUD(car) = turretMinY;
			}

			if (isReLCS) {
				// Rotate the actual turret frame on Re:LCS. Component 8 is
				// CAR_BUMP_REAR, which the mod reuses for the fire truck turret.
				if (GetVehicleComponent(car, 8)) {
					CMatrix mat;
					CVector pos;

					mat.Attach(RwFrameGetMatrix(GetVehicleComponent(car, 8)));
					pos = mat.GetPosition();
					mat.SetRotateZ(-(*GetDoomAnglePtrLR(car)));
					mat.GetPosition() = pos;
					mat.UpdateRW();
				}
			}
		}
	}
	else if (!isSA() && modernDriveBy && !IsFireTruk(car) && (cam->Mode == MODE_BEHINDBOAT || cam->Mode == MODE_CAMONASTRING) && !isHeli)
	{
		CVector hi = Multiply3x3(cam->Front, car->GetMatrix());

		if (hi.Heading() >= 0.5235987756 && hi.Heading() <= 2.617993878) // > 30 deg and < 150 deg
			lookingRelativelyLeft = true;
		else if (hi.Heading() <= -0.5235987756 && hi.Heading() >= -2.617993878) // < -30 and > -150 deg
			lookingRelativelyRight = true;
	}

	// SA handles vehicle look left/right/behind in CCam::Process (0x526FC0), which
	// runs after this function (the mod only replaces Process_FollowCar_SA).
	// Look-behind builds Source from the global at 0xB6F018 plus
	// CA_MAX_DISTANCE * carForward; that global was written at the tail of the
	// original Process_FollowCar_SA (0x525E31), so replacing the function left it
	// stale and the game's look-behind threw the camera out of the world. Look
	// left/right use a different path, which is why only look-behind broke. Keep
	// the global in sync.
	if (isSA())
		*(CVector*)0xB6F018 = TargetCoors;

	// SA also runs its own look in CCam::Process right after this function
	// (LookBehind / sub_520E40) and derives the distance from cam+0xD0, which does
	// not match the mod camera. Since the mod handles look above, force the game's
	// look direction forward so it skips its own handling.
	if (isSA())
		*(uint32*)0x8CC384 = 3;

	previousMode = cam->Mode;
}

// ---------------------------------------------------------------------------
// Hook entry points. The game calls these CCam methods; they forward to the
// templated engine with this game's types and the entity the camera targets.
// ---------------------------------------------------------------------------
void
CCamIII::Process_FollowCar_SA_III(const CVector &CameraTarget, float TargetOrientation, float, float)
{
	Process_FollowCar_SA<CCamIII, CCameraIII, CVehicleIII, CWorldIII, CColModelIII>(
		TheCameraIII, this, (CVehicleIII*)this->CamTargetEntity, CameraTarget, TargetOrientation);
}

void
CCamVC::Process_FollowCar_SA_VC(const CVector &CameraTarget, float TargetOrientation, float, float)
{
	Process_FollowCar_SA<CCamVC, CCameraVC, CVehicleVC, CWorldVC, CColModelVC>(
		TheCameraVC, this, (CVehicleVC*)this->CamTargetEntity, CameraTarget, TargetOrientation);
}

// SA's CCam::Process_FollowCar_SA takes an extra trailing bool (sthForScript),
// which the hook ignores.
void
CCamSA::Process_FollowCar_SA_SA(const CVector &CameraTarget, float TargetOrientation, float, float, bool)
{
	Process_FollowCar_SA<CCamSA, CCameraSA, CVehicleSA, CWorldSA, CColModelSA>(
		TheCameraSA, this, (CVehicleSA*)this->CamTargetEntity, CameraTarget, TargetOrientation);
}
