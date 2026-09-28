/**
  DO NOT EDIT
  FILE auto-generated
  Module name:
    AutoGen.h
  Abstract:       Auto-generated AutoGen.h for building module or library.
**/

#ifndef _AUTOGENH_36B8CF65_4E24_4B0A_AEEA_77A3AE06DF43
#define _AUTOGENH_36B8CF65_4E24_4B0A_AEEA_77A3AE06DF43

#ifdef __cplusplus
extern "C" {
#endif

#include <Uefi.h>
#include <Library/PcdLib.h>

extern GUID  gEfiCallerIdGuid;
extern GUID  gEdkiiDscPlatformGuid;
extern CHAR8 *gEfiCallerBaseName;

#define EFI_CALLER_ID_GUID \
  {0x36B8CF65, 0x4E24, 0x4B0A, {0xAE, 0xEA, 0x77, 0xA3, 0xAE, 0x06, 0xDF, 0x43}}
#define EDKII_DSC_PLATFORM_GUID \
  {0xFBC14DF7, 0xBF8E, 0x4A93, {0x8B, 0x2E, 0x85, 0xD1, 0x44, 0xCB, 0x5E, 0xBB}}
#define STACK_COOKIE_VALUE 0x38BE0BCB4C4BCE3BULL

// Guids
extern EFI_GUID gEfiMdePkgTokenSpaceGuid;
extern EFI_GUID gEfiMdeModulePkgTokenSpaceGuid;
extern EFI_GUID gArmPlatformTokenSpaceGuid;

// Definition of SkuId Array
extern UINT64 _gPcd_SkuId_Array[];

// Definition of PCDs used in libraries is in AutoGen.c


EFI_STATUS
EFIAPI
EfiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );





#ifdef __cplusplus
}
#endif

#endif
