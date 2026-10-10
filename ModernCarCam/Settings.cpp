#include "ModernCarCam.h"

#include <vector>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Settings.cpp - settings state and ini parsing.
//
// LoadSettings() reads the ini (next to the asi, or the shipped names), picks
// the user-facing profile, calls applyProfile() for the defaults, then applies
// the optional [General] table-profile overrides plus the [Features] switches
// and [Multipliers] strengths on top. A missing key, a blank value or "Auto"
// always leaves the profile's value untouched.
// ---------------------------------------------------------------------------

// User-facing profile and the resolved internal table selectors.
ModernProfile cameraProfile = PROFILE_GAME_MATCHED;
CameraProfileType masterProfile = PROFILE_VANILLA;
CameraProfileType distanceProfile = PROFILE_VANILLA;
CameraProfileType fovProfile = PROFILE_VANILLA;
CameraProfileType anglesProfile = PROFILE_VANILLA;

// Feature and tuning state. Strength values are multipliers: 1.0 = the profile
// default, 0.0 = off, other positive values scale the effect.
float cameraWobble = 1.0f;
float elasticStringPhysics = 0.0f;
float pitchTilt = 0.0f;
float pitchTiltUphill = -1.0f;
float pitchTiltDownhill = -1.0f;
float pitchTiltMinAngle = 0.0f;   // set by applyProfile (2 degrees)
float pitchTiltAirHoldTime = 0.0f;  // set by applyProfile
float pitchTiltAirBlendTime = 0.0f; // set by applyProfile
float maxPitchAngle = -1.0f;
float minPitchAngle = -1.0f;
float dynamicSpeedFOV = 0.0f;
float dynamicSpeedFOVStartSpeed = -1.0f;
float dynamicSpeedFOVMaxFOV = -1.0f;
float vcsCamShake = 0.0f;
float vcsCamShakeStartSpeed = -1.0f;
float cameraAnchoring = 1.0f;
float cameraStiffness = 1.0f;
float headingFollow = 1.0f;
float vehicleSpecificZoom = 1.0f;
float trafficCamWobble = 0.0f;
float trafficCamWobbleMinSpeed = 0.0f;    // set by applyProfile
float trafficCamWobbleFullSpeed = 0.0f;   // set by applyProfile
float trafficCamWobbleMaxMultiplier = 0.0f; // set by applyProfile
float reverseCamDelay = 0.0f;             // set by applyProfile
float dynamicSpeedFOVDecay = 0.0f;        // set by applyProfile
float cameraReturnSpeed = 1.0f;
float cameraReturnTime = 0.5f;
bool modernTurretControl = true;
bool modernDriveBy = true;
bool lockShootDirKBM = true;
bool lockShootDirJOY = true;
bool mouseFreeLook = true;
bool heightIncreaseOnBike = true;
bool fixTheBug = true;
bool reverseCam = true;
bool seeUnderwater = false;
bool enhancedVC = false;
// Smooth side view: true swings the look left/right/behind smoothly, false uses
// the vanilla instant change.
bool smoothSideView = false;
// Keyboard free-look: true lets the game's own keyboard look/turret keys drive
// the free camera; false leaves only the mouse and the analogue stick. Defaults
// to on in San Andreas (how SA behaves) and off in III/VC, where the keyboard
// keeps its vanilla look-left/right/behind and sub-mission meaning.
bool keyboardFreeLook = false;

// ---- GTA III top-down camera enhancements ----
bool topDownEnhance = false;
float topDownDistance = -1.0f;      // < 0: use the scale instead
float topDownDistanceScale = 1.0f;
float topDownClearance = 0.0f;      // height above the car to scan for a ceiling; 0 = off

// [Offsets] - applied independently of the selected profile.
float cameraHeightOffset = 0.0f;
float cameraLateralOffset = 0.0f;
float cameraDistanceOffsetNear = 0.0f;
float cameraDistanceOffsetMid = 0.0f;
float cameraDistanceOffsetFar = 0.0f;
float cameraMinDistance = -1.0f;           // <0 = profile default
float cameraDistanceScale = 1.0f;
CVector cameraDriverOffset = CVector(0.0f, 0.0f, 0.0f);

// Custom profile shape (loaded from [Custom] in the ini).
float customDistNear = 0.05f;
float customDistMid = 1.9f;
float customDistFar = 3.9f;

float customBaseFOV = 70.0f;

float customAngleNear = -0.01f;
float customAngleMid = 0.045f;
float customAngleFar = 0.005f;

// GInput state.
IGInputPad* ginputPad;
int ginputLoaded = 0; // 1: not installed 2: installed
GINPUT_PAD_SETTINGS padSettings = {};

void OnGInputSettingsReload()
{
	if (!ginputPad)
		return;
	padSettings.cbSize = sizeof(padSettings);
	ginputPad->SendConstEvent(GINPUT_EVENT_FETCH_PAD_SETTINGS, &padSettings);
}

// The Custom profile applies one distance/angle per zoom to every vehicle class.
// LoadSettings() builds the tables once; the debug menu rebuilds them when a
// Custom value is edited so the running camera picks the change up immediately.
void buildCustomCameraTables(void)
{
	for (int i = 0; i < 8; i++) {
		CARCAM_SET_CUSTOM[i][1] = 0.0f; // distance offset now lives in [Offsets]
		CARCAM_SET_CUSTOM[i][4] = 10.0f;
		CARCAM_SET_CUSTOM[i][13] = 0.785398f;
		CARCAM_SET_CUSTOM[i][14] = 1.5533431f;
	}
	for (int i = 0; i < 5; i++) {
		CarZoomModesCustom[i] = customDistNear;
		CarZoomModesCustom[i + 5] = customDistMid;
		CarZoomModesCustom[i + 10] = customDistFar;
		ZmOneAlphaOffsetCustom[i] = customAngleNear;
		ZmTwoAlphaOffsetCustom[i] = customAngleMid;
		ZmThreeAlphaOffsetCustom[i] = customAngleFar;
	}
}

// The running game's native vehicle-camera shape (its car row). A blank [Custom]
// key falls back to this, so Profile = Custom starts from the game's own camera
// and only the keys the user sets change it. Distances are the zoom offsets;
// angles are radians.
struct NativeCamShape {
	float distNear, distMid, distFar;
	float angleNear, angleMid, angleFar;
};

static NativeCamShape GetNativeCamShape(void)
{
	NativeCamShape s;
	if (isSA()) {
		s.distNear = CarZoomModesSA[0]; s.distMid = CarZoomModesSA[5]; s.distFar = CarZoomModesSA[10];
		s.angleNear = ZmOneAlphaOffset[0]; s.angleMid = ZmTwoAlphaOffset[0]; s.angleFar = ZmThreeAlphaOffset[0];
	} else if (isReLCS) {
		s.distNear = CarZoomModesLCS[0]; s.distMid = CarZoomModesLCS[5]; s.distFar = CarZoomModesLCS[10];
		s.angleNear = ZmOneAlphaOffsetLCS[0]; s.angleMid = ZmTwoAlphaOffsetLCS[0]; s.angleFar = ZmThreeAlphaOffsetLCS[0];
	} else if (isVC()) {
		s.distNear = CarZoomModesVC[0]; s.distMid = CarZoomModesVC[5]; s.distFar = CarZoomModesVC[10];
		s.angleNear = ZmOneAlphaOffsetVC[0]; s.angleMid = ZmTwoAlphaOffsetVC[0]; s.angleFar = ZmThreeAlphaOffsetVC[0];
	} else {
		s.distNear = CarZoomModesIII[0]; s.distMid = CarZoomModesIII[5]; s.distFar = CarZoomModesIII[10];
		s.angleNear = ZmOneAlphaOffsetIII[0]; s.angleMid = ZmTwoAlphaOffsetIII[0]; s.angleFar = ZmThreeAlphaOffsetIII[0];
	}
	return s;
}

// Maps a [General] table-profile selector string onto the internal enum.
// Blank/unknown falls back to def.
static CameraProfileType ParseTableProfileName(const char* str, CameraProfileType def)
{
	if (!str || !*str)
		return def;
	if (_stricmp(str, "SA") == 0 || _stricmp(str, "IV") == 0)
		return PROFILE_SA;
	if (_stricmp(str, "LCS") == 0 || _stricmp(str, "VCS") == 0)
		return PROFILE_LCS;
	if (_stricmp(str, "Custom") == 0)
		return PROFILE_CUSTOM;
	return PROFILE_VANILLA; // Game-Matched / III / VC / Original
}

// ---------------------------------------------------------------------------
// Per-car camera overrides ([Car<model id>] sections).
// ---------------------------------------------------------------------------

// Parsed overrides, one per [Car...] section, in file order. An entry is keyed
// either by a numeric model id (modelIndex >= 0) or by a model name (name set).
struct CarCameraEntry {
	int modelIndex;         // >= 0 for a numeric [Car400] key, -1 for a name key
	std::string name;       // model name for a name key, empty for a numeric key
	CarCamSettings settings;
};

static std::vector<CarCameraEntry> g_carCameraSettings;

static void LoadCarCameraSections(const char* iniPath);

// CRC32 (reflected, init 0xFFFFFFFF, no final invert) with each character
// upper-cased: the exact CKeyGen::GetUppercaseKey the game uses for model keys
// on San Andreas, where CBaseModelInfo stores the name hash (at +0x4) rather
// than the name itself.
static uint32 GtaUppercaseKey(const char* str)
{
	static uint32 table[256];
	static bool tableReady = false;
	if (!tableReady) {
		for (uint32 i = 0; i < 256; i++) {
			uint32 c = i;
			for (int k = 0; k < 8; k++)
				c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
			table[i] = c;
		}
		tableReady = true;
	}

	uint32 hash = 0xFFFFFFFFu;
	for (const unsigned char* p = (const unsigned char*)str; *p; ++p)
		hash = table[(hash ^ (unsigned char)toupper(*p)) & 0xFF] ^ (hash >> 8);
	return hash;
}

// True when the model at modelIndex carries the given name. San Andreas stores
// a name hash (compared with GtaUppercaseKey); III/VC store the name string.
static bool CarModelNameMatches(int modelIndex, const std::string& name)
{
	if (modelIndex < 0)
		return false;
	addr mi = CModelInfo::GetModelInfoPtr(modelIndex);
	if (!mi)
		return false;

	if (isSA())
		return *(uint32*)(mi + 0x4) == GtaUppercaseKey(name.c_str());

	char stored[24];
	memcpy(stored, (void*)(mi + 0x4), sizeof(stored));
	stored[sizeof(stored) - 1] = 0;
	return _stricmp(stored, name.c_str()) == 0;
}

void CaptureCarCamSettings(CarCamSettings& out)
{
	out.distanceProfile = distanceProfile;
	out.fovProfile = fovProfile;
	out.anglesProfile = anglesProfile;

	out.customDistNear = customDistNear;
	out.customDistMid = customDistMid;
	out.customDistFar = customDistFar;
	out.customBaseFOV = customBaseFOV;
	out.customAngleNear = customAngleNear;
	out.customAngleMid = customAngleMid;
	out.customAngleFar = customAngleFar;

	out.cameraHeightOffset = cameraHeightOffset;
	out.cameraLateralOffset = cameraLateralOffset;
	out.cameraDistanceOffsetNear = cameraDistanceOffsetNear;
	out.cameraDistanceOffsetMid = cameraDistanceOffsetMid;
	out.cameraDistanceOffsetFar = cameraDistanceOffsetFar;
	out.cameraMinDistance = cameraMinDistance;
	out.cameraDistanceScale = cameraDistanceScale;
	out.cameraDriverOffset = cameraDriverOffset;
}

void ApplyCarCamSettings(const CarCamSettings& s)
{
	distanceProfile = s.distanceProfile;
	fovProfile = s.fovProfile;
	anglesProfile = s.anglesProfile;

	customDistNear = s.customDistNear;
	customDistMid = s.customDistMid;
	customDistFar = s.customDistFar;
	customBaseFOV = s.customBaseFOV;
	customAngleNear = s.customAngleNear;
	customAngleMid = s.customAngleMid;
	customAngleFar = s.customAngleFar;

	cameraHeightOffset = s.cameraHeightOffset;
	cameraLateralOffset = s.cameraLateralOffset;
	cameraDistanceOffsetNear = s.cameraDistanceOffsetNear;
	cameraDistanceOffsetMid = s.cameraDistanceOffsetMid;
	cameraDistanceOffsetFar = s.cameraDistanceOffsetFar;
	cameraMinDistance = s.cameraMinDistance;
	cameraDistanceScale = s.cameraDistanceScale;
	cameraDriverOffset = s.cameraDriverOffset;

	// The Custom tables are rebuilt from the (now overridden) globals so a
	// per-car Custom shape reaches the camera.
	buildCustomCameraTables();
}

const CarCamSettings* FindCarCameraOverride(int modelIndex)
{
	for (size_t i = 0; i < g_carCameraSettings.size(); i++) {
		const CarCameraEntry& entry = g_carCameraSettings[i];
		if (entry.modelIndex >= 0) {
			if (entry.modelIndex == modelIndex)
				return &entry.settings;
		} else if (!entry.name.empty()) {
			if (CarModelNameMatches(modelIndex, entry.name))
				return &entry.settings;
		}
	}
	return nil;
}

// The resolved ini path, kept so the per-car sections can be re-read when the
// profile changes at runtime (the debug menu).
static char g_iniPath[MAX_PATH];

void ReloadPerCarCameraSections(void)
{
	if (g_iniPath[0])
		LoadCarCameraSections(g_iniPath);
}

// Reads every [Car<model id>] section. Each entry starts from the finished
// global settings and only overrides the keys it lists; a section with a
// non-numeric name (or no digits after "Car") is ignored. Any number of
// sections may be present.
static void LoadCarCameraSections(const char* iniPath)
{
	g_carCameraSettings.clear();

	CarCamSettings base;
	CaptureCarCamSettings(base);

	// Every section name, as a sequence of null-terminated strings.
	static char names[16384];
	DWORD len = GetPrivateProfileSectionNamesA(names, sizeof(names), iniPath);
	if (len == 0)
		return;

	// Trims trailing blanks and strips any inline "; comment".
	auto ReadTrimmed = [&](const char* sec, const char* key, char* out, size_t n) {
		out[0] = 0;
		GetPrivateProfileStringA(sec, key, "", out, (DWORD)n, iniPath);
		char* semi = strchr(out, ';');
		if (semi)
			*semi = 0;
		size_t l = strlen(out);
		while (l > 0 && (out[l - 1] == ' ' || out[l - 1] == '\t'))
			out[--l] = 0;
	};
	auto ReadFloatInto = [&](const char* sec, const char* key, float& target) {
		char buf[64];
		ReadTrimmed(sec, key, buf, sizeof(buf));
		if (buf[0])
			target = (float)atof(buf);
	};
	// The ini angles are degrees; the engine stores radians.
	auto ReadAngleDegInto = [&](const char* sec, const char* key, float& targetRad) {
		char buf[64];
		ReadTrimmed(sec, key, buf, sizeof(buf));
		if (buf[0])
			targetRad = DEGTORAD((float)atof(buf));
	};

	for (const char* p = names; *p; p += strlen(p) + 1) {
		const char* section = p;

		if (_strnicmp(section, "Car", 3) != 0)
			continue;
		const char* keyText = section + 3;
		while (*keyText == ' ' || *keyText == '\t')
			keyText++;
		if (!*keyText)
			continue; // a bare [Car] with no key
		if (_stricmp(keyText, "Cameras") == 0)
			continue; // the documented [CarCameras] section

		// The key is one model id or model name, or a comma-separated list of
		// them (e.g. [Car494,502,503] or [Carhotring,hotrina,hotrinb]). Every
		// key in the list shares this section's settings.
		CarCamSettings s = base;

		char tableBuf[32] = { 0 };
		ReadTrimmed(section, "DistanceProfile", tableBuf, sizeof(tableBuf));
		s.distanceProfile = ParseTableProfileName(tableBuf, base.distanceProfile);
		tableBuf[0] = 0;
		ReadTrimmed(section, "FOVProfile", tableBuf, sizeof(tableBuf));
		s.fovProfile = ParseTableProfileName(tableBuf, base.fovProfile);
		tableBuf[0] = 0;
		ReadTrimmed(section, "AnglesProfile", tableBuf, sizeof(tableBuf));
		s.anglesProfile = ParseTableProfileName(tableBuf, base.anglesProfile);

		ReadFloatInto(section, "CustomDistanceNear", s.customDistNear);
		ReadFloatInto(section, "CustomDistanceMid", s.customDistMid);
		ReadFloatInto(section, "CustomDistanceFar", s.customDistFar);
		ReadFloatInto(section, "CustomBaseFOV", s.customBaseFOV);
		ReadAngleDegInto(section, "CustomAngleNear", s.customAngleNear);
		ReadAngleDegInto(section, "CustomAngleMid", s.customAngleMid);
		ReadAngleDegInto(section, "CustomAngleFar", s.customAngleFar);

		ReadFloatInto(section, "CameraHeightOffset", s.cameraHeightOffset);
		ReadFloatInto(section, "CameraLateralOffset", s.cameraLateralOffset);
		ReadFloatInto(section, "CameraDistanceOffsetNear", s.cameraDistanceOffsetNear);
		ReadFloatInto(section, "CameraDistanceOffsetMid", s.cameraDistanceOffsetMid);
		ReadFloatInto(section, "CameraDistanceOffsetFar", s.cameraDistanceOffsetFar);
		ReadFloatInto(section, "CameraMinDistance", s.cameraMinDistance);
		ReadFloatInto(section, "CameraDistanceScale", s.cameraDistanceScale);
		ReadFloatInto(section, "CameraDriverOffsetX", s.cameraDriverOffset.x);
		ReadFloatInto(section, "CameraDriverOffsetY", s.cameraDriverOffset.y);
		ReadFloatInto(section, "CameraDriverOffsetZ", s.cameraDriverOffset.z);

		// Split the key on commas; each part is a numeric id or a model name.
		std::string keyList(keyText);
		size_t pos = 0;
		for (;;) {
			size_t comma = keyList.find(',', pos);
			std::string token = keyList.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);

			size_t first = token.find_first_not_of(" \t");
			size_t last = token.find_last_not_of(" \t");
			if (first == std::string::npos)
				token.clear();
			else
				token = token.substr(first, last - first + 1);

			if (!token.empty()) {
				bool allDigits = true;
				for (size_t c = 0; c < token.size(); c++) {
					if (token[c] < '0' || token[c] > '9') {
						allDigits = false;
						break;
					}
				}
				CarCameraEntry entry;
				entry.settings = s;
				if (allDigits) {
					entry.modelIndex = atoi(token.c_str());
					entry.name.clear();
				} else {
					entry.modelIndex = -1;
					entry.name = token;
				}
				g_carCameraSettings.push_back(entry);
			}

			if (comma == std::string::npos)
				break;
			pos = comma + 1;
		}
	}
}

void LoadSettings()
{
	char iniPath[MAX_PATH];
	GetModuleFileNameA(dllModule, iniPath, MAX_PATH);
	char* dot = strrchr(iniPath, '.');
	if (dot) strcpy(dot, ".ini");

	if (GetFileAttributesA(iniPath) == INVALID_FILE_ATTRIBUTES) {
		if (GetFileAttributesA(".\\III.VC.SA.ModernCarCam.ini") != INVALID_FILE_ATTRIBUTES) {
			strcpy(iniPath, ".\\III.VC.SA.ModernCarCam.ini");
		} else if (GetFileAttributesA(".\\scripts\\III.VC.SA.ModernCarCam.ini") != INVALID_FILE_ATTRIBUTES) {
			strcpy(iniPath, ".\\scripts\\III.VC.SA.ModernCarCam.ini");
		}
	}
	strcpy(g_iniPath, iniPath);

	char profile[32] = { 0 };
	GetPrivateProfileStringA("General", "Profile", "Game", profile, sizeof(profile), iniPath);
	if (_stricmp(profile, "Game") == 0 || _stricmp(profile, "Game-Matched") == 0) {
		cameraProfile = PROFILE_GAME_MATCHED;
	} else if (_stricmp(profile, "III") == 0) {
		cameraProfile = PROFILE_III;
	} else if (_stricmp(profile, "VC") == 0 || _stricmp(profile, "Vice City") == 0) {
		cameraProfile = PROFILE_VC;
	} else if (_stricmp(profile, "SA") == 0 || _stricmp(profile, "San Andreas") == 0) {
		cameraProfile = PROFILE_SA_CAM;
	} else if (_stricmp(profile, "Enhanced") == 0) {
		cameraProfile = PROFILE_ENHANCED;
	} else if (_stricmp(profile, "LCS") == 0) {
		cameraProfile = PROFILE_LCS_CAM;
	} else if (_stricmp(profile, "VCS") == 0) {
		cameraProfile = PROFILE_VCS;
	} else if (_stricmp(profile, "IV") == 0) {
		cameraProfile = PROFILE_IV;
	} else if (_stricmp(profile, "Custom") == 0) {
		cameraProfile = PROFILE_CUSTOM_CAM;
	} else {
		cameraProfile = PROFILE_GAME_MATCHED;
	}

	applyProfile(cameraProfile, isVC());

	// Individual table overrides. They follow the main Profile unless set.
	auto ParseTableProfile = [](const char* str, CameraProfileType def) -> CameraProfileType {
		return ParseTableProfileName(str, def);
	};
	char tableBuf[32] = { 0 };
	GetPrivateProfileStringA("General", "DistanceProfile", "", tableBuf, sizeof(tableBuf), iniPath);
	distanceProfile = ParseTableProfile(tableBuf, distanceProfile);
	GetPrivateProfileStringA("General", "FOVProfile", "", tableBuf, sizeof(tableBuf), iniPath);
	fovProfile = ParseTableProfile(tableBuf, fovProfile);
	GetPrivateProfileStringA("General", "AnglesProfile", "", tableBuf, sizeof(tableBuf), iniPath);
	anglesProfile = ParseTableProfile(tableBuf, anglesProfile);

	// The profile already chose the defaults. These ini keys are optional
	// overrides: omit a key (or set it to Auto) to keep the profile's value.
	// ReadTrimmed strips any inline "; comment" and trailing blanks, since the
	// Win32 profile API keeps them as part of the value otherwise.
	auto ReadTrimmed = [&](const char* sec, const char* key, char* out, size_t n) {
		out[0] = 0;
		GetPrivateProfileStringA(sec, key, "", out, (DWORD)n, iniPath);
		char* semi = strchr(out, ';');
		if (semi)
			*semi = 0;
		size_t len = strlen(out);
		while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t'))
			out[--len] = 0;
	};
	auto ReadFloat = [&](const char* sec, const char* key, float def) -> float {
		char buf[64];
		ReadTrimmed(sec, key, buf, sizeof(buf));
		return buf[0] ? (float)atof(buf) : def;
	};

	// [Features] tri-state switch: -1 = Auto/blank, 0 = off, 1 = on.
	auto ReadSwitch = [&](const char* key) -> int {
		char buf[64];
		ReadTrimmed("Features", key, buf, sizeof(buf));
		if (!buf[0] || _stricmp(buf, "auto") == 0)
			return -1;
		return atoi(buf) != 0 ? 1 : 0;
	};
	// [Multipliers] numeric value. Returns false for Auto / blank.
	auto ReadMultiplier = [&](const char* key, float& out) -> bool {
		char buf[64];
		ReadTrimmed("Multipliers", key, buf, sizeof(buf));
		if (!buf[0] || _stricmp(buf, "auto") == 0)
			return false;
		out = (float)atof(buf);
		return true;
	};
	// A switchable effect: the [Features] switch decides on/off, the
	// [Multipliers] value scales the strength. profileVal is what applyProfile
	// already set (0 = off, otherwise the profile's strength).
	auto CombineEffect = [&](const char* key, float profileVal) -> float {
		int sw = ReadSwitch(key);
		float mult = 0.0f;
		bool multSet = ReadMultiplier(key, mult);
		bool enabled = (sw == 1) ? true : (sw == 0 ? false : profileVal > 0.0f);
		float strength = multSet ? mult : (profileVal > 0.0f ? profileVal : 1.0f);
		return enabled ? strength : 0.0f;
	};
	// Plain on/off feature from [Features].
	auto ReadBool = [&](const char* key, bool& out) {
		int sw = ReadSwitch(key);
		if (sw != -1)
			out = (sw != 0);
	};

	// [Offsets] - applied on top of every profile, so they are additive (or
	// multiplicative, for the scale) deltas rather than replacements.
	cameraHeightOffset += ReadFloat("Offsets", "CameraHeightOffset", 0.0f);
	cameraLateralOffset += ReadFloat("Offsets", "CameraLateralOffset", 0.0f);
	cameraDistanceOffsetNear += ReadFloat("Offsets", "CameraDistanceOffsetNear", 0.0f);
	cameraDistanceOffsetMid += ReadFloat("Offsets", "CameraDistanceOffsetMid", 0.0f);
	cameraDistanceOffsetFar += ReadFloat("Offsets", "CameraDistanceOffsetFar", 0.0f);
	cameraDistanceScale *= ReadFloat("Offsets", "CameraDistanceScale", 1.0f);
	cameraDriverOffset += CVector(
		ReadFloat("Offsets", "CameraDriverOffsetX", 0.0f),
		ReadFloat("Offsets", "CameraDriverOffsetY", 0.0f),
		ReadFloat("Offsets", "CameraDriverOffsetZ", 0.0f));
	cameraMinDistance = ReadFloat("Offsets", "CameraMinDistance", -1.0f);

	// [Custom] profile shape. A blank key keeps the running game's own value, so
	// Profile = Custom starts as the game camera and only the keys the user sets
	// change it (the [Custom] section ships blank).
	NativeCamShape nativeShape = GetNativeCamShape();
	customDistNear = ReadFloat("Custom", "CustomDistanceNear", nativeShape.distNear);
	customDistMid  = ReadFloat("Custom", "CustomDistanceMid", nativeShape.distMid);
	customDistFar  = ReadFloat("Custom", "CustomDistanceFar", nativeShape.distFar);

	customBaseFOV = ReadFloat("Custom", "CustomBaseFOV", DefaultFOV);

	// The ini expresses the camera angles in degrees; the engine stores radians.
	auto ReadAngle = [&](const char* key, float nativeRad) -> float {
		char buf[64];
		ReadTrimmed("Custom", key, buf, sizeof(buf));
		return buf[0] ? DEGTORAD((float)atof(buf)) : nativeRad;
	};
	customAngleNear = ReadAngle("CustomAngleNear", nativeShape.angleNear);
	customAngleMid  = ReadAngle("CustomAngleMid", nativeShape.angleMid);
	customAngleFar  = ReadAngle("CustomAngleFar", nativeShape.angleFar);

	buildCustomCameraTables();

	// [Features] switches + [Multipliers] strengths. Switchable effects combine
	// the two: the switch decides on/off, the multiplier scales the strength.
	cameraWobble = CombineEffect("CameraWobble", cameraWobble);
	elasticStringPhysics = CombineEffect("ElasticStringPhysics", elasticStringPhysics);
	pitchTilt = CombineEffect("PitchTilt", pitchTilt);
	dynamicSpeedFOV = CombineEffect("DynamicSpeedFOV", dynamicSpeedFOV);
	vcsCamShake = CombineEffect("VCSCamShake", vcsCamShake);
	trafficCamWobble = CombineEffect("TrafficCamWobble", trafficCamWobble);
	cameraAnchoring = CombineEffect("CameraAnchoring", cameraAnchoring);
	vehicleSpecificZoom = CombineEffect("VehicleSpecificZoom", vehicleSpecificZoom);

	// [Multipliers]: Auto keeps the profile value. Speeds are km/h and angles
	// are degrees in the ini; convert to the engine's units here.
	float value = 0.0f;
	if (ReadMultiplier("PitchTiltUphill", value))       pitchTiltUphill = value;
	if (ReadMultiplier("PitchTiltDownhill", value))     pitchTiltDownhill = value;
	if (ReadMultiplier("PitchTiltMinAngle", value))     pitchTiltMinAngle = DEGTORAD(value);
	if (ReadMultiplier("PitchTiltAirHoldTime", value))  pitchTiltAirHoldTime = value;
	if (ReadMultiplier("PitchTiltAirBlendTime", value)) pitchTiltAirBlendTime = value;
	if (ReadMultiplier("MaxPitch", value))              maxPitchAngle = DEGTORAD(value);
	if (ReadMultiplier("MinPitch", value))              minPitchAngle = DEGTORAD(value);
	if (ReadMultiplier("DynamicSpeedFOVStartSpeed", value))  dynamicSpeedFOVStartSpeed = value / SpeedKphFactor;
	if (ReadMultiplier("DynamicSpeedFOVMaxFOV", value))      dynamicSpeedFOVMaxFOV = value;
	if (ReadMultiplier("DynamicSpeedFOVDecay", value))       dynamicSpeedFOVDecay = value;
	if (ReadMultiplier("VCSCamShakeStartSpeed", value))      vcsCamShakeStartSpeed = value / SpeedKphFactor;
	if (ReadMultiplier("TrafficCamWobbleMinSpeed", value))   trafficCamWobbleMinSpeed = value / SpeedKphFactor;
	if (ReadMultiplier("TrafficCamWobbleFullSpeed", value))  trafficCamWobbleFullSpeed = value / SpeedKphFactor;
	if (ReadMultiplier("TrafficCamWobbleMaxMultiplier", value)) trafficCamWobbleMaxMultiplier = value;
	if (ReadMultiplier("CameraStiffness", value))       cameraStiffness = value;
	if (ReadMultiplier("HeadingFollow", value))         headingFollow = value;
	if (ReadMultiplier("ReverseCameraDelay", value))    reverseCamDelay = value;
	if (ReadMultiplier("CameraReturnSpeed", value))     cameraReturnSpeed = value;
	if (ReadMultiplier("CameraReturnTime", value))      cameraReturnTime = value;

	// [Features] plain switches.
	ReadBool("ModernTurretControl", modernTurretControl);
	ReadBool("ModernDriveBy", modernDriveBy);
	ReadBool("LockShootDirectionKBM", lockShootDirKBM);
	ReadBool("LockShootDirectionJOY", lockShootDirJOY);
	ReadBool("MouseFreeLook", mouseFreeLook);
	ReadBool("FixCameraClip", fixTheBug);
	ReadBool("ReverseCamera", reverseCam);
	ReadBool("BikesHeightIncrease", heightIncreaseOnBike);
	ReadBool("SmoothSideView", smoothSideView);
	ReadBool("KeyboardFreeLook", keyboardFreeLook);

	// [TopDown] - GTA III top-down camera enhancements.
	{
		auto ReadTD = [&](const char* key, float def) -> float {
			char buf[64];
			ReadTrimmed("TopDown", key, buf, sizeof(buf));
			return buf[0] ? (float)atof(buf) : def;
		};
		topDownEnhance = ReadTD("Enhance", topDownEnhance ? 1.0f : 0.0f) != 0.0f;
		topDownDistance = ReadTD("Distance", topDownDistance);
		topDownDistanceScale = ReadTD("DistanceScale", topDownDistanceScale);
		topDownClearance = ReadTD("Clearance", topDownClearance);
	}

	// Inverted relative to the internal "see underwater" flag.
	int keepWater = ReadSwitch("KeepCameraOverWater");
	if (keepWater != -1)
		seeUnderwater = (keepWater == 0);

	// Enhanced on San Andreas keeps the SA anchor and stiffness even if the ini
	// overrides them. Its distance, FOV and angles default to SA too, but the
	// DistanceProfile / FOVProfile / AnglesProfile keys above override them. The
	// VC steering wobble and VCS camera shake are off by default on SA but can
	// still be enabled here.
	if (isSA() && cameraProfile == PROFILE_ENHANCED) {
		cameraAnchoring = 0.0f;
		cameraStiffness = 1.0f;
	}

	// Per-car overrides start from these finished global settings, so this must
	// run last. Each [Car<model id>] section changes only the keys it lists.
	LoadCarCameraSections(iniPath);
}
