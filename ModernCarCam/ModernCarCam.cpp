#include "common.h"
#include "MemoryMgr.h"
#include "GTA.h"
#include "Camera.h"
#include "Pad.h"

#include "ModuleList.hpp"
#include "GInputAPI.h"
#include "debugmenu_public.h"

#include "ModernCarCam.h"

// ---------------------------------------------------------------------------
// ModernCarCam - a universal vehicle camera for GTA III and GTA Vice City.
//
// With the shipped vanilla settings the vehicle camera is a 1:1 reproduction
// of the original camera of either game. Every additional behaviour is an
// ini option layered on top of that baseline.
//
// This file owns DllMain and the game hooks, the address wrappers for the
// engine calls, the shared buffer helper and the debug-menu hooks. The rest of
// the project is split by concern (see ModernCarCam.h):
//
//   Profiles.cpp  camera tables + applyProfile()
//   Settings.cpp  ini parsing + settings state
//   CamVanilla.cpp  authentic III/VC "on a string" and behind-boat cameras
//   CamSA.cpp       San Andreas follow-camera engine
//   DebugMenu.cpp   the ModernCarCam section of aap's debug menu
//
// The authentic camera is based on the reversed sources of re3 / reVC
// (https://github.com/Hezkore/hez-gta-re3); see licenses/re3.txt.
// The VCS camera shake is ported from ThirteenAG's WidescreenFixesPack
// (MIT licensed, see licenses/WidescreenFixesPack.txt).
// ---------------------------------------------------------------------------

HMODULE dllModule, hDummyHandle;
int gtaversion = -1;

// isVC() also returns true for Re:LCS.
bool isReLCS = false;

// ---- First-person mod compatibility ----
bool firstPersonModCompat = false;
// The 12 bytes SA's CCam::Process uses to clear Cams[ActiveCam].LookingBehind/
// Left/Right (normally NOPed so the mod's published drive-by flags survive).
static unsigned char saLookingClearBytes[12];
static bool saLookingClearSaved = false;

UltFP_IsFirstPerson_t pUltFP_IsFirstPerson = nullptr;

void CheckFirstPersonModCompat(void)
{
	static int checked = 0;
	if (checked)
		return;
	checked = 1;

	// ASIs load in an order we do not control, so this runs on the first camera
	// frame, once everything is in memory.
	HMODULE h = GetModuleHandleA("UltimateFP_SA.asi");
	if (!h) {
		ModuleList mods;
		const auto loaded = mods.GetAllByPrefix(L"Ultimate");
		if (!loaded.empty())
			h = loaded[0];
	}
	if (h != nullptr) {
		pUltFP_IsFirstPerson = (UltFP_IsFirstPerson_t)GetProcAddress(h, "UltimateFP_IsFirstPerson");
		if (pUltFP_IsFirstPerson != nullptr) {
			firstPersonModCompat = true;
			// Give the game's own drive-by look-flag clear back; UltimateFP has
			// its own drive-by support, so the mod does not need to hold them.
			if (saLookingClearSaved)
				memcpy((void*)0x527DC9, saLookingClearBytes, 12);
		}
	}
}

DebugMenuAPI gDebugMenuAPI;

// -----

// SA: points at CCamera::m_pRwCamera (0xB6F028 + 0x954). The RwCamera near
// plane lives at +0x80 in RenderWare (verified in rwcore.h).
void(*&RwCamera) = *AddressByVersion<void**>(0x72676C, 0, 0, 0x8100BC, 0, 0, 0xB6F97C);

CCameraIII *TheCameraIII = (CCameraIII*)0x6FACF8;
CCameraVC *TheCameraVC = (CCameraVC*)0x7E4688;
CCameraSA *TheCameraSA = (CCameraSA*)0xB6F028; // SA CCamera singleton

// Actually static member of CCamera
bool& m_bUseMouse3rdPerson = *AddressByVersion<bool*>(0x5F03D8, 0, 0, 0xA10B4C, 0, 0, 0xB6EC2E);

cDMAudio &DMAudio = *AddressByVersion<cDMAudio*>(0x95CDBE, 0, 0, 0xA10B8A, 0, 0);

// These are static members of CWorld. SA's TestSphereAgainstWorld writes into
// CWorld::gaTempSphereColPoints (0xB9B250); the engine only reads the point of
// the first entry, so we expose that as ms_testSpherePoint.
CColPoint& ms_testSpherePoint = *AddressByVersion<CColPoint*>(0x6E64C0, 0, 0, 0x7D18C0, 0, 0, 0xB9B250);
CEntity*& pIgnoreEntity = *AddressByVersion<CEntity **>(0x8F6494, 0, 0, 0x9B6E58, 0, 0, 0xB7CD68);

#pragma warning(push)
#pragma warning(disable: 4100) // the naked address wrappers forward every argument untouched

// SA addresses: CWorld::ProcessLineOfSight 0x56BA00, TestSphereAgainstWorld
// 0x569E20, FindGroundZFor3DCoord 0x5696C0, FindRoofZFor3DCoord 0x569750,
// ProcessVerticalLine 0x5674E0, GetIsLineOfSightClear 0x56A490. All verified
// against gta-sa.exe / gta-reversed.
addr plosAddress = AddressByVersion<addr>(0x4AF970, 0, 0, 0x4D92D0, 0, 0, 0x56BA00);
WRAPPER bool CWorldIII::ProcessLineOfSight(const CVector& point1, const CVector& point2, CColPoint& point, CEntity*& entity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, bool ignoreSomeObjects) { EAXJMP(plosAddress); }
WRAPPER bool CWorldVC::ProcessLineOfSight(const CVector& point1, const CVector& point2, CColPoint& point, CEntity*& entity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, bool ignoreSomeObjects, bool sth) { EAXJMP(plosAddress); }
WRAPPER bool CWorldSA::ProcessLineOfSight(const CVector& point1, const CVector& point2, CColPoint& point, CEntity*& entity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, bool ignoreCamera, bool shootThrough) { EAXJMP(plosAddress); }

addr tsawAddress = AddressByVersion<addr>(0x4B4710, 0, 0, 0x4D3F40, 0, 0, 0x569E20);
WRAPPER CEntity* CWorldIII::TestSphereAgainstWorld(CVector centre, float distance, CEntity* entityToIgnore, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSomeObjects) { EAXJMP(tsawAddress); }
WRAPPER CEntity* CWorldVC::TestSphereAgainstWorld(CVector centre, float distance, CEntity* entityToIgnore, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSomeObjects) { EAXJMP(tsawAddress); }
WRAPPER CEntity* CWorldSA::TestSphereAgainstWorld(CVector centre, float distance, CEntity* entityToIgnore, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreCamera) { EAXJMP(tsawAddress); }

addr fgz3dAddress = AddressByVersion<addr>(0x4B3AE0, 0, 0, 0x4D53A0, 0, 0, 0x5696C0);
WRAPPER float CWorldIII::FindGroundZFor3DCoord(float x, float y, float z, bool* found) { EAXJMP(fgz3dAddress); }
WRAPPER float CWorldVC::FindGroundZFor3DCoord(float x, float y, float z, bool* found) { EAXJMP(fgz3dAddress); }
WRAPPER float CWorldSA::FindGroundZFor3DCoord(float x, float y, float z, bool* found, CEntity** outEntity) { EAXJMP(fgz3dAddress); }

addr frz3dAddress = AddressByVersion<addr>(0x4B3B50, 0, 0, 0x4D51D0, 0, 0, 0x569750);
WRAPPER float CWorldIII::FindRoofZFor3DCoord(float x, float y, float z, bool* found) { EAXJMP(frz3dAddress); }
WRAPPER float CWorldVC::FindRoofZFor3DCoord(float x, float y, float z, bool* found) { EAXJMP(frz3dAddress); }
WRAPPER float CWorldSA::FindRoofZFor3DCoord(float x, float y, float z, bool* found) { EAXJMP(frz3dAddress); }

addr pvlAddress = AddressByVersion<addr>(0x4B0DE0, 0, 0, 0x4D8B00, 0, 0, 0x5674E0);
WRAPPER bool CWorldIII::ProcessVerticalLine(const CVector& origin, float distance, CColPoint& point, CEntity*& entity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, CStoredCollPoly* outCollPoly) { EAXJMP(pvlAddress); }
WRAPPER bool CWorldVC::ProcessVerticalLine(const CVector& origin, float distance, CColPoint& point, CEntity*& entity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, CStoredCollPoly* outCollPoly) { EAXJMP(pvlAddress); }
WRAPPER bool CWorldSA::ProcessVerticalLine(const CVector& origin, float distance, CColPoint& point, CEntity*& entity, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, CStoredCollPoly* outCollPoly) { EAXJMP(pvlAddress); }

addr loscAddress = AddressByVersion<addr>(0x4AEAA0, 0, 0, 0x4DA560, 0, 0, 0x56A490);
WRAPPER bool CWorldIII::GetIsLineOfSightClear(const CVector& origin, const CVector& target, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, bool ignoreSomeObjects) { EAXJMP(loscAddress); }
WRAPPER bool CWorldVC::GetIsLineOfSightClear(const CVector& origin, const CVector& target, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, bool ignoreSomeObjects) { EAXJMP(loscAddress); }
WRAPPER bool CWorldSA::GetIsLineOfSightClear(const CVector& origin, const CVector& target, bool checkBuildings, bool checkVehicles, bool checkPeds, bool checkObjects, bool checkDummies, bool ignoreSeeThrough, bool ignoreSomeObjects) { EAXJMP(loscAddress); }

// SA's CWaterLevel::GetWaterLevelNoWaves takes a CVector by value plus three
// out-pointers, so it cannot be forwarded through the shared 4-float wrapper.
// The III/VC functions are still called through a typed function pointer.
addr gwlnwAddress = AddressByVersion<addr>(0x555440, 0, 0, 0x5C2BE0, 0, 0, 0x6E8580);
bool CWaterLevel::GetWaterLevelNoWaves(float fX, float fY, float fZ, float* pfOutLevel) {
	if (isSA()) {
		typedef bool(__cdecl* tGetWaterLevelNoWaves)(CVector, float*, float*, float*);
		return ((tGetWaterLevelNoWaves)gwlnwAddress)(CVector(fX, fY, fZ), pfOutLevel, nil, nil);
	}
	typedef bool(__cdecl* tGetWaterLevelNoWavesIII)(float, float, float, float*);
	return ((tGetWaterLevelNoWavesIII)gwlnwAddress)(fX, fY, fZ, pfOutLevel);
}

// SA's RenderWare near-plane function is not hooked; writing the near plane
// (RwCamera + 0x80, verified from rwcore.h) directly is equivalent for the
// mod's purposes. III/VC still call the game's function.
addr rcsncpAddress = AddressByVersion<addr>(0x5A5070, 0, 0, 0x64A860, 0, 0, 0);
void* RwCameraSetNearClipPlane(void* camera, float nearClip) {
	if (isSA()) {
		if (nearClip < 0.0f)
			nearClip = 0.0f;
		*(float*)((addr)camera + 0x80) = nearClip;
		return camera;
	}
	typedef void*(__cdecl* tRwCameraSetNearClipPlane)(void*, float);
	return ((tRwCameraSetNearClipPlane)rcsncpAddress)(camera, nearClip);
}

addr posAddress = AddressByVersion<addr>(0x57C840, 0, 0, 0x5F9DA0, 0, 0);
WRAPPER void cDMAudio::PlayOneShot(int32 audioEntity, uint16 oneShot, float volume) { EAXJMP(posAddress); }

CBaseModelInfo** CModelInfo::ms_modelInfoPtrs = AddressByVersion<CBaseModelInfo **>(0x83D408, 0, 0, 0x92D4C8, 0, 0, 0xA9B0C8);

addr attachAddress = AddressByVersion<addr>(0x4B8DD0, 0, 0, 0x4DFA40, 0, 0);
addr updateRwAddress = AddressByVersion<addr>(0x4B8EC0, 0, 0, 0x4DF8F0, 0, 0);
WRAPPER void CMatrix::Attach(RwMatrix* matrix, bool owner) { EAXJMP(attachAddress); }
WRAPPER void CMatrix::UpdateRW(void) { EAXJMP(updateRwAddress); }
#pragma warning(pop)

// SA: the debug-menu intercept is only installed for III/VC. (gta-sa.exe's
// DebugInitTextBuffer call site is not required for the camera port.)
addr ditbAddress = AddressByVersion<addr>(0x48BFB0, 0, 0, 0x4A4C02, 0, 0, 0);
void (*DebugInitTextBuffer)();

// San Andreas has no reversed CDebug::DebugInitTextBuffer call site, so the
// menu is registered from the game's startup instead: the first call in WinMain
// (0x74872D, into CGame::IsAlreadyRunning at 0x7468E0, the single-instance
// check). It runs exactly once, before the game is initialised, which is where
// aap's own mods load and fill the debug menu too.
int (*SA_IsAlreadyRunning)();
static int RegisterDebugMenuOnSA(void) {
	registerDebugMenuEntries();
	return SA_IsAlreadyRunning();
}

// Actually static member of CVehicle. SA uses its inverse (m_bEnableMouseSteering
// at 0xC1CC02); see MouseSteeringDisabled() in CamSA.cpp.
bool &m_bDisableMouseSteering = *AddressByVersion<bool*>(0x60252C, 0, 0, 0x69C610, 0, 0);

uint32 &m_snTimeInMilliseconds = *AddressByVersion<uint32*>(0x885B48, 0, 0, 0x974B2C, 0, 0, 0xB7CB84);
float &ms_fTimeStep = *AddressByVersion<float*>(0x8E2CB4, 0, 0, 0x975424, 0, 0, 0xB7CB5C);

bool lookingRelativelyLeft = false;
bool lookingRelativelyRight = false;

void onMasterProfileChange(void) {
	applyProfile(cameraProfile, isVC());
	// The menu edits the engine variables in place; the unit shadows need to
	// follow the values applyProfile() just reset.
	syncDebugMenuShadows();
	// Per-car entries start from the global settings, so rebuild them against
	// the profile applyProfile() just applied.
	ReloadPerCarCameraSections();
}

// ---------------------------------------------------------------------------
// Debug menu
//
// registerDebugMenuEntries() (DebugMenu.cpp) does the actual registration; the
// game-specific hook below only decides when it runs. The III/VC hook sits on
// the game's debug-text-buffer init, so it must still call the original.
// ---------------------------------------------------------------------------
void registerDebugMenu() {
	registerDebugMenuEntries();
	DebugInitTextBuffer();
}

// ---------------------------------------------------------------------------
// 360-degree drive-by: let the game treat a turret-relative "looking left or
// right" as a real look direction.
// ---------------------------------------------------------------------------
#pragma warning(push)
#pragma warning(disable: 4740) // the naked asm thunks intentionally flow around inline asm
namespace BetterDriveBy {

	__declspec(naked) static void LookingLeftOrRightIII()
	{
		if (TheCameraIII->Cams[TheCameraIII->ActiveCam].LookingLeft || lookingRelativelyLeft)
		{
			EAXJMP(0x564099)
		}
		else if (TheCameraIII->Cams[TheCameraIII->ActiveCam].LookingRight || lookingRelativelyRight)
		{
			EAXJMP(0x5640A6)
		}
		else
		{
			EAXJMP(0x5641D1)
		}
	}

	__declspec(naked) static void LookingLeftOrRightVC()
	{
		if (TheCameraVC->Cams[TheCameraVC->ActiveCam].LookingLeft || lookingRelativelyLeft)
		{
			EAXJMP(0x5C9880)
		}
		else if (TheCameraVC->Cams[TheCameraVC->ActiveCam].LookingRight || lookingRelativelyRight)
		{
			EAXJMP(0x5C988E)
		}
		else
		{
			EAXJMP(0x5C9893)
		}
	}

	__declspec(naked) static void LookingLeftOrRightBikesVC()
	{
		if (TheCameraVC->Cams[TheCameraVC->ActiveCam].LookingLeft || lookingRelativelyLeft)
		{
			EAXJMP(0x5C92B9)
		}
		else if (TheCameraVC->Cams[TheCameraVC->ActiveCam].LookingRight || lookingRelativelyRight)
		{
			EAXJMP(0x5C92C7)
		}
		else
		{
			EAXJMP(0x5C92CB)
		}
	}

	__declspec(naked) static void LookingLeftOrRightBoatsVC()
	{
		if (TheCameraVC->Cams[TheCameraVC->ActiveCam].LookingLeft || lookingRelativelyLeft)
		{
			EAXJMP(0x5C9610)
		}
		else if (TheCameraVC->Cams[TheCameraVC->ActiveCam].LookingRight || lookingRelativelyRight)
		{
			EAXJMP(0x5C961E)
		}
		else
		{
			EAXJMP(0x5C9623)
		}
	}
}
#pragma warning(pop)

// Shared by all three cam classes: build the camera's Up vector from Front and
// the roll angle. The classes differ only in their memory layout, not this maths.
static void GetVectorsReadyForRW_Impl(CVector& Front, CVector& Up, float f_Roll)
{
	Front.Normalise();
	if (Front.x == 0.0f && Front.y == 0.0f) {
		Front.x = 0.0001f;
		Front.y = 0.0001f;
	}
	CVector right = CrossProduct(Front, CVector(0.0f, 0.0f, 1.0f));
	if (right.MagnitudeSqr() < 0.0001f)
		right = CVector(1.0f, 0.0f, 0.0f);
	else
		right.Normalise();

	CVector up0 = CrossProduct(right, Front);
	up0.Normalise();

	Up = up0 * cosf(f_Roll) - right * sinf(f_Roll);
	Up.Normalise();
}

void CCamVC::GetVectorsReadyForRW(void) { GetVectorsReadyForRW_Impl(Front, Up, f_Roll); }
void CCamIII::GetVectorsReadyForRW(void) { GetVectorsReadyForRW_Impl(Front, Up, f_Roll); }
void CCamSA::GetVectorsReadyForRW(void) { GetVectorsReadyForRW_Impl(Front, Up, f_Roll); }

// The active camera mode of the running game. Kept from the previous frame
// because the alpha-angle correction needs to know which mode it came from
// (reversed by The Hero - aap).
#define currentMode (isSA() ? TheCameraSA->Cams[TheCameraSA->ActiveCam].Mode : \
	(isIII() ? TheCameraIII->Cams[TheCameraIII->ActiveCam].Mode : TheCameraVC->Cams[TheCameraVC->ActiveCam].Mode))

int previousMode = 0;

void
WellBufferMe(float Target, float* CurrentValue, float* CurrentSpeed, float MaxSpeed, float Acceleration, bool IsAngle)
{
	// MaxSpeed is a limit of how fast the value is allowed to change. 1.0 = to Target in up to 1ms
	// Acceleration is how fast the speed will change to MaxSpeed. 1.0 = to MaxSpeed in 1ms

	float Delta = Target - *CurrentValue;

	if (IsAngle) {
		while (Delta >= PI) Delta -= 2 * PI;
		while (Delta < -PI) Delta += 2 * PI;
	}

	float TargetSpeed = Delta * MaxSpeed;
	*CurrentSpeed += Acceleration * (TargetSpeed - *CurrentSpeed) * ms_fTimeStep;

	// Clamp speed if we overshot
	if (TargetSpeed < 0.0f && *CurrentSpeed < TargetSpeed)
		* CurrentSpeed = TargetSpeed;
	else if (TargetSpeed > 0.0f && *CurrentSpeed > TargetSpeed)
		* CurrentSpeed = TargetSpeed;

	*CurrentValue += *CurrentSpeed * min(10.0f, ms_fTimeStep);

	previousMode = currentMode;
}

int16
CPad::FakeCarGunLeftRight(void)
{
	if (currentMode == MODE_CAMONASTRING)
		return 0;
	else
		return pad0.GetCarGunLeftRight();
}

int16
CPad::FakeCarGunUpDown(void)
{
	if (currentMode == MODE_CAMONASTRING)
		return 0;
	else
		return pad0.GetCarGunUpDown();
}
#undef currentMode

// ---------------------------------------------------------------------------
// Widescreen Fixes Pack compatibility
//
// WSF has its own "CarSpeedDependantFOV" and "VCSCamShake" options and installs
// hooks for them at game init. This mod owns both behaviours, so those options
// are forced off in the running game's WSF ini before WSF reads it, which makes
// WSF skip installing its hooks entirely. This is why enabling either feature in
// WSF has no effect while ModernCarCam is installed.
// ---------------------------------------------------------------------------
static void OverrideWidescreenFixOptions(void)
{
	const char* iniName = nil;
	if (isIII())
		iniName = "GTAIII.WidescreenFix.ini";
	else if (isVC())
		iniName = "GTAVC.WidescreenFix.ini";
	else if (isSA())
		iniName = "GTASA.WidescreenFix.ini";

	if (!iniName)
		return;

	// The ini sits either next to the game exe or in scripts\.
	const char* dirs[2] = { "", ".\\scripts\\" };
	char path[MAX_PATH];
	for (int i = 0; i < 2; i++) {
		strcpy(path, dirs[i]);
		strcat(path, iniName);
		if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
			WritePrivateProfileStringA("MISC", "CarSpeedDependantFOV", "0", path);
			WritePrivateProfileStringA("MISC", "VCSCamShake", "0", path);
		}
	}
}

// ---------------------------------------------------------------------------
// DllMain: detect the running game and install the hooks.
// ---------------------------------------------------------------------------
// GTA III top-down camera enhancement (stacking).
//
// Wraps the game's own CCam::Process and post-processes the pose its MODE_TOPDOWN
// / MODE_GTACLASSIC camera produced: the distance can be scaled or overridden, and
// a ceiling above the car drops the view to first person. The vanilla top-down
// logic stays in charge; nothing here reimplements it.
// ---------------------------------------------------------------------------
typedef int(__thiscall *CCamProcessFn)(void*);

static CCamProcessFn origCCamProcessIII = (CCamProcessFn)0x4596A0;

int __fastcall CCamProcessHookIII(void* thisptr, void*)
{
	const int result = origCCamProcessIII(thisptr);
	if (topDownEnhance) {
		CCamIII* cam = (CCamIII*)thisptr;
		if (cam->Mode == 1 || cam->Mode == 2) { // MODE_TOPDOWN / MODE_GTACLASSIC
			CVector target = cam->CamTargetEntity ? cam->CamTargetEntity->GetPosition() : cam->m_cvecTargetCoorsForFudgeInter;
			CVector delta = cam->Source - target;

			// Distance: scale the whole car-to-camera vector so the view direction is
			// untouched and the frame does not jump.
			if (topDownDistance > 0.0f) {
				CVector horiz(delta.x, delta.y, 0.0f);
				const float hl = horiz.Magnitude();
				if (hl > 0.001f)
					delta *= (topDownDistance / hl);
			} else {
				delta *= topDownDistanceScale;
			}
			cam->Source = target + delta;

			// A ceiling above the car drops the view to first person when Clearance > 0.
			if (topDownClearance > 0.0f) {
				const CVector above = target + CVector(0.0f, 0.0f, topDownClearance);
				if (!CWorldIII::GetIsLineOfSightClear(target, above, true, false, false, true, false, false, false)) {
					cam->Mode = 16; // MODE_1STPERSON
					cam->Source = target + CVector(0.0f, 0.0f, 0.6f);
					cam->Front = cam->CamTargetEntity ? cam->CamTargetEntity->GetForward() : CVector(0.0f, 1.0f, 0.0f);
					cam->Up = CVector(0.0f, 0.0f, 1.0f);
				}
			}
		}
	}
	return result;
}

void InitVanillaCameraEnhanceHooks(void)
{
	if (isIII()) {
		InjectHook(0x46d5ff, &CCamProcessHookIII, PATCH_NOTHING);
		InjectHook(0x46d7a0, &CCamProcessHookIII, PATCH_NOTHING);
	}
}

// ---------------------------------------------------------------------------
BOOL WINAPI
DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		dllModule = hInst;

		LoadSettings();

		GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)& DllMain, &hDummyHandle);

		// III
		if (*(DWORD*)0x5C1E70 == 0x53E58955) {
			InjectHook(0x456F40, &WellBufferMe, PATCH_JUMP);
			InjectHook(0x459A16, &CCamIII::Process_FollowCar_SA_III, PATCH_NOTHING);
			InjectHook(0x459A54, &CCamIII::Process_FollowCar_SA_III, PATCH_NOTHING);
			InjectHook(0x459B36, &CCamIII::Process_FollowCar_SA_III, PATCH_NOTHING);

			// Smooth the game's own look behind/left/right when SmoothSideView is on.
			InitVanillaLookHooks(false, true);

			// Prevent game from overwriting cam->FOV to 70.0f every frame (allows dynamic FOV and custom base FOV)
			Nop(0x45b52e, 10);

			if (modernTurretControl) {
				// To block original rhino-firetruck turret movement
				InjectHook(0x52260E, &CPad::FakeCarGunUpDown, PATCH_NOTHING);
				InjectHook(0x53D628, &CPad::FakeCarGunLeftRight, PATCH_NOTHING);
				InjectHook(0x5225D2, &CPad::FakeCarGunLeftRight, PATCH_NOTHING);
			}
			if (modernDriveBy) {
				InjectHook(0x56409D, 0x5640AB, PATCH_JUMP);
				InjectHook(0x564090, BetterDriveBy::LookingLeftOrRightIII, PATCH_JUMP);
			}
		// VC
		} else if (*(DWORD*)0x667BF5 == 0xB85548EC) {

			// checks for settings file name (gta_lcs.set in this case)
			isReLCS = *(DWORD*)0x68CFCC == 0x2E73636C;

			InjectHook(0x4864E3, &WellBufferMe, PATCH_JUMP);
			InjectHook(0x483B3B, &CCamVC::Process_FollowCar_SA_VC, PATCH_NOTHING);
			InjectHook(0x483B79, &CCamVC::Process_FollowCar_SA_VC, PATCH_NOTHING);
			InjectHook(0x483C3C, &CCamVC::Process_FollowCar_SA_VC, PATCH_NOTHING);

			// Smooth the game's own look behind/left/right when SmoothSideView is on.
			InitVanillaLookHooks(true, false);

			// Prevent game from overwriting cam->FOV to 70.0f every frame (allows dynamic FOV and custom base FOV)
			Nop(0x47fb22, 10);

			if (modernDriveBy) {
				InjectHook(0x5C9885, 0x5C9893, PATCH_JUMP);
				InjectHook(0x5C9877, BetterDriveBy::LookingLeftOrRightVC, PATCH_JUMP);
				InjectHook(0x5C92BE, 0x5C92CB, PATCH_JUMP);
				InjectHook(0x5C92B0, BetterDriveBy::LookingLeftOrRightBikesVC, PATCH_JUMP);
				InjectHook(0x5C9615, 0x5C9623, PATCH_JUMP);
				InjectHook(0x5C9607, BetterDriveBy::LookingLeftOrRightBoatsVC, PATCH_JUMP);
			}

			if (modernTurretControl) {
				// To block original rhino-firetruck turret movement
				InjectHook(0x57ABAE, &CPad::FakeCarGunUpDown, PATCH_NOTHING);
				InjectHook(0x57AB72, &CPad::FakeCarGunLeftRight, PATCH_NOTHING);
				InjectHook(0x5865B8, &CPad::FakeCarGunLeftRight, PATCH_NOTHING);
			}

			// Vehicle-specific zoom tables are only patched for non-vanilla
			// distance profiles. The vanilla profile leaves the game's table
			// untouched so the Widescreen Fix keeps control of it and the two
			// mods do not fight over the same values.
			if (distanceProfile != PROFILE_VANILLA) {
				const float* zoomTable = (distanceProfile == PROFILE_CUSTOM) ? CarZoomModesCustom :
					((distanceProfile == PROFILE_LCS) ? CarZoomModesLCS : CarZoomModesSA);
				for (int i = 0; i < 15; i++) {
					addr a = 0x68AB70 + i * sizeof(float);
					Patch(a, zoomTable[i]);
				}
			}
		// SA
		} else if (*(DWORD*)SA_10_US_SIGNATURE_ADDR == SA_10_US_SIGNATURE) {

			// CCam::WellBufferMe (0x509AE0)
			InjectHook(0x509AE0, &WellBufferMe, PATCH_JUMP);

			// CCam::Process_FollowCar_SA (0x5245B0). Replacing the whole
			// function also removes SA's per-frame "cam->FOV = 70.0f" write at
			// 0x524BE4, so dynamic/custom FOV works without a separate NOP.
			InjectHook(0x5245B0, &CCamSA::Process_FollowCar_SA_SA, PATCH_JUMP);

			// Drive-by: CCam::Process clears Cams[ActiveCam].LookingBehind/Left/
			// Right at 0x527DC9 (right after our engine runs) and SA's native look
			// -- which we suppress via gCameraDirection -- is what normally sets
			// them again. Our engine publishes those flags, so stop the clear. The
			// original bytes are saved so a first-person mod can have the clear back.
			memcpy(saLookingClearBytes, (void*)0x527DC9, 12);
			saLookingClearSaved = true;
			Nop(0x527DC9, 12);

			// Note: the III/VC BetterDriveBy and FakeCarGun(UpDown/LeftRight)
			// hooks are not installed on SA; those address/pad-internals differ.
		}
		else return FALSE;

		// Stack the GTA III top-down enhancement onto the game's own camera.
		InitVanillaCameraEnhanceHooks();

		// III/VC register the menu from their debug-text-buffer init. SA has no
		// reversed debug-menu call site, so its startup call in WinMain is used
		// instead (see RegisterDebugMenuOnSA above).
		if (ditbAddress)
			InterceptCall(&DebugInitTextBuffer, registerDebugMenu, ditbAddress);
		else if (isSA())
			InterceptCall(&SA_IsAlreadyRunning, RegisterDebugMenuOnSA, 0x74872D);

		// Make this mod's FOV / camera shake options win over the Widescreen
		// Fix's own copies (must run before WSF reads its ini at game init).
		OverrideWidescreenFixOptions();
	}
	return TRUE;
}
