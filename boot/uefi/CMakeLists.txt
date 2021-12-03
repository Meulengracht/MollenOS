cmake_minimum_required(VERSION 3.8)
project(vali-boot-uefi C ASM_NASM)

set(OMVF_URL_BASE "https://github.com/retrage/edk2-nightly/raw/master/bin")
if ("${VALI_ARCH}" STREQUAL "amd64")
    set(OMVF_URL_BASE "${OMVF_URL_BASE}/RELEASEX64")
    set(OVMF_FILE_BASE "RELEASEX64")
elseif ("${VALI_ARCH}" STREQUAL "i386")
    set(OMVF_URL_BASE "${OMVF_URL_BASE}/RELEASEIA32")
    set(OVMF_FILE_BASE "RELEASEIA32")
elseif ("${VALI_ARCH}" STREQUAL "aarch64")
    set(OMVF_URL_BASE "${OMVF_URL_BASE}/RELEASEAARCH64")
    set(OVMF_FILE_BASE "RELEASEAARCH64")
elseif ("${VALI_ARCH}" STREQUAL "arm")
    set(OMVF_URL_BASE "${OMVF_URL_BASE}/RELEASEARM")
    set(OVMF_FILE_BASE "RELEASEARM")
elseif ("${VALI_ARCH}" STREQUAL "rv64")
    set(OMVF_URL_BASE "${OMVF_URL_BASE}/RELEASERISCV64")
    set(OVMF_FILE_BASE "RELEASERISCV64")
else ()
    message(FATAL_ERROR "Unsupported architecture: ${VALI_ARCH}")
endif ()

# download OVMF content
if (NOT EXISTS "${CMAKE_BINARY_DIR}/${OVMF_FILE_BASE}_OVMF_CODE.fd")
    file(DOWNLOAD "${OMVF_URL_BASE}_OVMF_CODE.fd" "${CMAKE_BINARY_DIR}/${OVMF_FILE_BASE}_OVMF_CODE.fd")
endif()
if (NOT EXISTS "${CMAKE_BINARY_DIR}/${OVMF_FILE_BASE}_OVMF_VARS.fd")
    file(DOWNLOAD "${OMVF_URL_BASE}_OVMF_VARS.fd" "${CMAKE_BINARY_DIR}/${OVMF_FILE_BASE}_OVMF_VARS.fd")
endif()

# create test image directory and data
make_directory(${CMAKE_BINARY_DIR}/image)
file(WRITE ${CMAKE_BINARY_DIR}/image/startup.nsh "fs0:VBOOT.efi")

# initialize compiler environment
add_compile_definitions(__${VALI_ARCH}__)

# initialize Pcds
add_compile_definitions(_PCD_GET_MODE_32_PcdMaximumUnicodeStringLength=1000000)
add_compile_definitions(_PCD_GET_MODE_32_PcdMaximumAsciiStringLength=1000000)

## This flag is used to control the print out Debug message.<BR><BR>
#  BIT0  - Initialization message.<BR>
#  BIT1  - Warning message.<BR>
#  BIT2  - Load Event message.<BR>
#  BIT3  - File System message.<BR>
#  BIT4  - Allocate or Free Pool message.<BR>
#  BIT5  - Allocate or Free Page message.<BR>
#  BIT6  - Information message.<BR>
#  BIT7  - Dispatcher message.<BR>
#  BIT8  - Variable message.<BR>
#  BIT10 - Boot Manager message.<BR>
#  BIT12 - BlockIo Driver message.<BR>
#  BIT14 - Network Driver message.<BR>
#  BIT16 - UNDI Driver message.<BR>
#  BIT17 - LoadFile message.<BR>
#  BIT19 - Event message.<BR>
#  BIT20 - Global Coherency Database changes message.<BR>
#  BIT21 - Memory range cachability changes message.<BR>
#  BIT22 - Detailed debug message.<BR>
#  BIT31 - Error message.<BR>
# @Prompt Debug Message Print Level.
# @Expression  0x80000002 | (gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel & 0x7F84AA00) == 0
add_compile_definitions(_PCD_GET_MODE_32_PcdDebugPrintErrorLevel=0x80000000)

## This flag is used to control build time optimization based on debug print level.
#  Its default value is 0xFFFFFFFF to expose all debug print level.
#  BIT0  - Initialization message.<BR>
#  BIT1  - Warning message.<BR>
#  BIT2  - Load Event message.<BR>
#  BIT3  - File System message.<BR>
#  BIT4  - Allocate or Free Pool message.<BR>
#  BIT5  - Allocate or Free Page message.<BR>
#  BIT6  - Information message.<BR>
#  BIT7  - Dispatcher message.<BR>
#  BIT8  - Variable message.<BR>
#  BIT10 - Boot Manager message.<BR>
#  BIT12 - BlockIo Driver message.<BR>
#  BIT14 - Network Driver message.<BR>
#  BIT16 - UNDI Driver message.<BR>
#  BIT17 - LoadFile message.<BR>
#  BIT19 - Event message.<BR>
#  BIT20 - Global Coherency Database changes message.<BR>
#  BIT21 - Memory range cachability changes message.<BR>
#  BIT22 - Detailed debug message.<BR>
#  BIT31 - Error message.<BR>
# @Prompt Fixed Debug Message Print Level.
add_compile_definitions(_PCD_GET_MODE_32_PcdFixedDebugPrintErrorLevel=0xFFFFFFFF)

## The mask is used to control DebugLib behavior.<BR><BR>
#  BIT0 - Enable Debug Assert.<BR>
#  BIT1 - Enable Debug Print.<BR>
#  BIT2 - Enable Debug Code.<BR>
#  BIT3 - Enable Clear Memory.<BR>
#  BIT4 - Enable BreakPoint as ASSERT.<BR>
#  BIT5 - Enable DeadLoop as ASSERT.<BR>
# @Prompt Debug Property.
# @Expression  0x80000002 | (gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask & 0xC0) == 0
add_compile_definitions(_PCD_GET_MODE_8_PcdDebugPropertyMask=0)

## This value is used to fill a segment of memory when PcdDebugPropertyMask Clear Memory is enabled.
# @Prompt Value to Clear Memory.
add_compile_definitions(_PCD_GET_MODE_8_PcdDebugClearMemoryValue=0xAF)

## Indicates the control flow enforcement enabling state.
#  If enabled, it uses control flow enforcement technology to prevent ROP or JOP.<BR><BR>
#   BIT0 - SMM CET Shadow Stack is enabled.<BR>
#   Other - reserved
# @Prompt Enable control flow enforcement.
add_compile_definitions(_PCD_GET_MODE_32_PcdControlFlowEnforcementPropertyMask=0)

add_compile_options(-Wall -Werror -Wno-format -Wno-microsoft-static-assert)
add_compile_options(-mno-red-zone)
add_compile_options(-fshort-wchar)
add_compile_options(-fno-stack-protector)
add_compile_options(-ffreestanding)

include_directories(include)
include_directories(include/efi)

set(SRCS)
set(ASM_SRCS)

# initialize sources for specific platforms
if ("${VALI_ARCH}" STREQUAL "amd64")
    set (CMAKE_ASM_NASM_OBJECT_FORMAT win64)
    set (NASM_DEFAULT_FORMAT win64)
    add_compile_options(--target=x86_64-pc-win32-coff)

    include_directories(include/efi/X64)

    set(ASM_SRCS
        efi/BaseLib/X64/CpuId.nasm
        efi/BaseLib/X64/FlushCacheLine.nasm
        efi/BaseLib/X64/Invd.nasm
        efi/BaseLib/X64/LongJump.nasm
        efi/BaseLib/X64/SetJump.nasm
        efi/BaseLib/X64/Wbinvd.nasm
    )
    
    set(SRCS
        efi/BaseCacheMaintenanceLib/X86Cache.c
        efi/BaseIoLibIntrinsic/IoLibGcc.c
        efi/RegisterFilterLib/RegisterFilterLibNull.c
        efi/SerialPortLib/X86/SerialPortLib.c
    )
elseif ("${VALI_ARCH}" STREQUAL "i386")
    set (CMAKE_ASM_NASM_OBJECT_FORMAT win32)
    set (NASM_DEFAULT_FORMAT win32)
    add_compile_options(--target=i386-pc-win32-coff)

    include_directories(include/efi/Ia32)

    set(ASM_SRCS
            efi/BaseLib/Ia32/CpuId.nasm
            efi/BaseLib/Ia32/FlushCacheLine.nasm
            efi/BaseLib/Ia32/Invd.nasm
            efi/BaseLib/Ia32/LongJump.nasm
            efi/BaseLib/Ia32/SetJump.nasm
            efi/BaseLib/Ia32/Wbinvd.nasm

            efi/BaseLib/MsIntrinsincs/_aulldiv.nasm
            efi/BaseLib/MsIntrinsincs/_aullrem.nasm
            efi/BaseLib/MsIntrinsincs/_alldiv.nasm
            efi/BaseLib/MsIntrinsincs/_allrem.nasm
    )

    set(SRCS
        efi/BaseCacheMaintenanceLib/X86Cache.c
        efi/BaseIoLibIntrinsic/IoLibGcc.c
        efi/RegisterFilterLib/RegisterFilterLibNull.c
        efi/SerialPortLib/X86/SerialPortLib.c
    )
else ()
    message(FATAL_ERROR "Unsupported architecture: ${VALI_ARCH}")
endif ()

set (CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <INCLUDES> \
    <DEFINES> -f ${CMAKE_ASM_NASM_OBJECT_FORMAT} -o <OBJECT> <SOURCE>")

# initialize sources for the common platform
set(SRCS 
    ${SRCS}

    # efi library sources
    efi/BaseDebugLibSerialPort/DebugLib.c
    efi/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.c

    efi/BaseLib/BitField.c
    efi/BaseLib/DivU64x32Remainder.c
    efi/BaseLib/DivU64x32.c
    efi/BaseLib/Math64.c
    efi/BaseLib/MultU64x32.c
    efi/BaseLib/LShiftU64.c
    efi/BaseLib/LongJump.c
    efi/BaseLib/RShiftU64.c
    efi/BaseLib/String.c
    efi/BaseLib/SafeString.c
    efi/BaseLib/SetJump.c
    efi/BaseLib/SwapBytes16.c
    efi/BaseLib/SwapBytes32.c
    efi/BaseLib/Unaligned.c

    efi/BaseMemoryLib/CopyMem.c
    efi/BaseMemoryLib/CopyMemWrapper.c
    efi/BaseMemoryLib/MemLibGeneric.c
    efi/BaseMemoryLib/MemLibGuid.c
    efi/BaseMemoryLib/SetMem.c
    efi/BaseMemoryLib/SetMem32Wrapper.c
    efi/BaseMemoryLib/SetMem64Wrapper.c
    efi/BaseMemoryLib/SetMemWrapper.c
    efi/BaseMemoryLib/ZeroMemWrapper.c

    efi/BasePrintLib/PrintLib.c
    efi/BasePrintLib/PrintLibInternal.c

    efi/BasePeCoffLib/BasePeCoff.c
    efi/BasePeCoffLib/PeCoffLoaderEx.c
    efi/BasePeCoffLib/PeCoffExtraActionLib.c

    console.c
    depack.c
    library.c
    loader.c
    main.c
    video.c
)

# Set assembler flavor for nasm
set_source_files_properties(${ASM_SRCS}
    PROPERTIES 
        LANGUAGE ASM_NASM
)

add_executable(vboot ${SRCS} ${ASM_SRCS})
set_target_properties(vboot
    PROPERTIES
        OUTPUT_NAME "VBOOT.efi"
        LINK_FLAGS "-Xlinker -subsystem:efi_application -Xlinker -entry:EfiMain -nostdlib"
)

install(TARGETS vboot)

# initialize a qemu-target that helps us test faster
add_custom_target (test
    COMMAND ${CMAKE_COMMAND} -E copy VBOOT.efi image/VBOOT.efi
    COMMAND qemu-system-x86_64 
        -drive format=raw,file=fat:rw:image
        -drive if=pflash,format=raw,readonly,file=${OVMF_FILE_BASE}_OVMF_CODE.fd
        -drive if=pflash,format=raw,file=${OVMF_FILE_BASE}_OVMF_VARS.fd
        -nodefaults -serial stdio
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS vboot
    USES_TERMINAL
)

### Secure Boot
### 1. download shim (https://apps.fedoraproject.org/packages/shim)
### 2. extract SHIMX64.EFI and MMX64.EFI to EFI\BOOT (these are signed by MS)
### 3. rename EFI\BOOT\SHIMX64.EFI to EFI\BOOT\BOOTX64.EFI.
### 4. create a public-private key pair and x509 cert with openssl.
###    - openssl req -newkey rsa:4096 -nodes -keyout MOK.key -new -x509 -days 3650 -subj "/CN=BOOTBOOT/" -out MOK.crt
###    - openssl x509 -outform DER -in MOK.crt -out MOK.cer
### 
### 5. sign VBOOT.EFI using an SHA-256 hash with sbsign.
###    - sbsign --key MOK.key --cert MOK.crt --out EFI/BOOT/GRUBX64.EFI bootboot.efi
### 
### 6. copy MOK.cer to the ESP partition.
### 7. boot into UEFI, shim will call MOK Manager. There select "Enroll key from disk", find MOK.cer and add it. Then select "Enroll hash from disk", find GRUBX64.EFI and add it.
### 8. enable Secure Boot.
