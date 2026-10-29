#include "common.h"
#include "MemoryMgr.h"
#include "Camera.h"
#include "Pad.h"

// SA: CPad::Pads is at 0xB73458 and NewMouseControllerState at 0xB73418.
CPad &pad0 = *AddressByVersion<CPad*>(0x6F0360, 0, 0, 0x7DBCB0, 0, 0, 0xB73458);
CMouseControllerState &CPad::NewMouseControllerState = *AddressByVersion<CMouseControllerState*>(0x8809F0, 0, 0, 0x94D788, 0, 0, 0xB73418);

// SA CPad method addresses (gta-reversed / gta-sa.exe).
addr glcAddress = AddressByVersion<addr>(0x4932F0, 0, 0, 0x4AAC30, 0, 0, 0x53FE70);
addr glpAddress = AddressByVersion<addr>(0x493320, 0, 0, 0x4AAC00, 0, 0, 0x53FEC0);
addr glrAddress = AddressByVersion<addr>(0x4932C0, 0, 0, 0x4AAC60, 0, 0, 0x53FE10);
addr gllAddress = AddressByVersion<addr>(0x493290, 0, 0, 0x4AAC90, 0, 0, 0x53FDD0);
addr gcglrAddress = AddressByVersion<addr>(0x4930C0, 0, 0, 0x4AAEB0, 0, 0, 0x53FC50);
addr gcgudAddress = AddressByVersion<addr>(0x493070, 0, 0, 0x4AAF00, 0, 0, 0x53FC10);
// GetCarGunFired exists on GTA III, Vice City and San Andreas. III 1.0 is
// 0x493490; the other III builds are not hooked here (as with the other pad
// methods above).
addr gcgfAddress = AddressByVersion<addr>(0x493490, 0, 0, 0x4AAA60, 0, 0, 0x53FF90);
addr gsudAddress = AddressByVersion<addr>(0x492FF0, 0, 0, 0x4AAF50, 0, 0, 0x53FBD0);
addr gslrAddress = AddressByVersion<addr>(0x492F70, 0, 0, 0x4AAFD0, 0, 0, 0x53FB80);

WRAPPER bool CPad::GetLookBehindForCar(void) { EAXJMP(glcAddress); }
WRAPPER bool CPad::GetLookBehindForPed(void) { EAXJMP(glpAddress); }
WRAPPER bool CPad::GetLookRight(void) { EAXJMP(glrAddress); }
WRAPPER bool CPad::GetLookLeft(void) { EAXJMP(gllAddress); }
WRAPPER int16 CPad::GetCarGunLeftRight(void) { EAXJMP(gcglrAddress); }
WRAPPER int16 CPad::GetCarGunUpDown(void) { EAXJMP(gcgudAddress); }
WRAPPER bool CPad::GetCarGunFired(void) { EAXJMP(gcgfAddress); }
WRAPPER int16 CPad::GetSteeringUpDown(void) { EAXJMP(gsudAddress); }
WRAPPER int16 CPad::GetSteeringLeftRight(void) { EAXJMP(gslrAddress); }
