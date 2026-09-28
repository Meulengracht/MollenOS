## @file
# Native EDK2 build description for the Vali UEFI boot application.
##

[Defines]
  PLATFORM_NAME                  = VBoot
  PLATFORM_GUID                  = FBC14DF7-BF8E-4A93-8B2E-85D144CB5EBB
  PLATFORM_VERSION               = 1.0
  DSC_SPECIFICATION              = 0x0001001B
  OUTPUT_DIRECTORY               = Build/VBoot
  SUPPORTED_ARCHITECTURES        = IA32|X64|AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE|NOOPT
  SKUID_IDENTIFIER               = DEFAULT

!include MdePkg/MdeLibs.dsc.inc

[LibraryClasses]
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  IoLib|MdePkg/Library/BaseIoLibIntrinsic/BaseIoLibIntrinsic.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  PeCoffExtraActionLib|MdePkg/Library/BasePeCoffExtraActionLibNull/BasePeCoffExtraActionLibNull.inf
  PeCoffLib|MdePkg/Library/BasePeCoffLib/BasePeCoffLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  SafeIntLib|MdePkg/Library/BaseSafeIntLib/BaseSafeIntLib.inf
  StackCheckLib|MdePkg/Library/StackCheckLibNull/StackCheckLibNull.inf
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf

[LibraryClasses.IA32, LibraryClasses.X64]
  CacheMaintenanceLib|MdePkg/Library/BaseCacheMaintenanceLib/BaseCacheMaintenanceLib.inf
  SerialPortLib|PcAtChipsetPkg/Library/SerialIoLib/SerialIoLib.inf

[LibraryClasses.AARCH64]
  ArmLib|MdePkg/Library/ArmLib/ArmBaseLib.inf
  CacheMaintenanceLib|ArmPkg/Library/ArmCacheMaintenanceLib/ArmCacheMaintenanceLib.inf
  PL011UartClockLib|ArmPlatformPkg/Library/PL011UartClockLib/PL011UartClockLib.inf
  PL011UartLib|ArmPlatformPkg/Library/PL011UartLib/PL011UartLib.inf
  SerialPortLib|ArmPlatformPkg/Library/PL011SerialPortLib/PL011SerialPortLib.inf

[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdMaximumUnicodeStringLength|1000000
  gEfiMdePkgTokenSpaceGuid.PcdMaximumAsciiStringLength|1000000
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x80000000
  gEfiMdePkgTokenSpaceGuid.PcdFixedDebugPrintErrorLevel|0xFFFFFFFF
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0
  gEfiMdePkgTokenSpaceGuid.PcdDebugClearMemoryValue|0xAF
  gEfiMdePkgTokenSpaceGuid.PcdControlFlowEnforcementPropertyMask|0

[PcdsFixedAtBuild.IA32, PcdsFixedAtBuild.X64]
  gPcAtChipsetPkgTokenSpaceGuid.PcdUartIoPortBaseAddress|0x3F8

[PcdsFixedAtBuild.AARCH64]
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultReceiveFifoDepth|1
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultBaudRate|115200
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultDataBits|8
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultParity|1
  gEfiMdePkgTokenSpaceGuid.PcdUartDefaultStopBits|1
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialRegisterBase|0x09000000
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialUseHardwareFlowControl|FALSE
  gEfiMdeModulePkgTokenSpaceGuid.PcdSerialBaudRate|115200
  gArmPlatformTokenSpaceGuid.PL011UartClkInHz|24000000
  gArmPlatformTokenSpaceGuid.PL011UartInteger|0
  gArmPlatformTokenSpaceGuid.PL011UartFractional|0
  gArmPlatformTokenSpaceGuid.PL011UartRegOffsetVariant|0

[BuildOptions.IA32]
  *_*_*_CC_FLAGS = -D__i386__ -Wno-format -Wno-address-of-packed-member

[BuildOptions.X64]
  *_*_*_CC_FLAGS = -D__amd64__ -Wno-format -Wno-address-of-packed-member

[BuildOptions.AARCH64]
  *_*_*_CC_FLAGS = -D__aarch64__ -Wno-format -Wno-address-of-packed-member

[Components]
  VBoot.inf
