/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __VALI_RPI_LOADER_H__
#define __VALI_RPI_LOADER_H__

#include <stddef.h>
#include <stdint.h>
#include <os/osdefs.h>

#include <vboot/vboot.h>

#define RPI_MEMORY_MAP_CAPACITY 256u
#define RPI_DTB_MAX_SIZE (2u * 1024u * 1024u)

#define RPI_PAYLOAD_HEADER_SIZE 64u
#define RPI_PAYLOAD_MAX_SIZE (64u * 1024u * 1024u)

enum RpiBootStatus { 
    RpiBootOk,
    RpiBootInvalidPayload,
    RpiBootUnsupported,
    RpiBootInvalidPlatform,
    RpiBootNoMemory
};

enum RpiBootStage {
    RpiBootValidatePayload = 1,
    RpiBootDiscoverPlatform,
    RpiBootLoadKernel,
    RpiBootLoadResources,
    RpiBootBuildContract,
    RpiBootTransfer
};

struct RpiKernelInformation {
    uint64_t Offset;
    uint64_t Length;
    uint64_t ImageLength;
};

struct RpiBootContext {
    uintptr_t                   DtbPhysical;
    unsigned int                Board;
    struct RpiKernelInformation Kernel;
    struct VBoot                BootInformation;
    
    // Storage lives inside the wrapper's reserved BSS,
    // since we have no allocator before this stage.
    struct VBootMemoryEntry     MemoryMap[RPI_MEMORY_MAP_CAPACITY];
    uint32_t                    MemoryMapCount;
    
    // Firmware transport containing the validated Phoenix/ramdisk bundle.
    uint64_t                    ExternalPayloadBase;
    uint64_t                    ExternalPayloadLength;
};

#define SWAP16(val) ((((val) >> 8) & 0xff) | (((val) & 0xff) << 8))
#define SWAP32(val) ((((val) >> 24) & 0xff) | (((val) >> 8) & 0xff00) | (((val) & 0xff00) << 8) | (((val) & 0xff) << 24))
#define SWAP64(val) ((((val) >> 56) & 0xff) | (((val) >> 40) & 0xff00) | (((val) >> 24) & 0xff0000) | (((val) >> 8) & 0xff000000) | \
                     (((val) & 0xff000000) << 8) | (((val) & 0xff0000) << 24) | (((val) & 0xff00) << 40) | (((val) & 0xff) << 56))

/**
 * @brief Builds the memory map from the device tree. This is meant to be called early
 * with pre-allocated storage for platforms where dynamic memory allocation is not available
 * prior to the initialization of the memory management subsystem.
 * The input and output must not overlap. The output count is zero on failure;
 * partial output must not be consumed. Dynamic and no-map reservations currently
 * return OS_ENOTSUPPORTED rather than publishing incomplete ownership data.
 * @param deviceTree The address of the device tree.
 * @param deviceTreeSize The independently accessible extent of the device tree.
 * @param context The boot context containing pre-allocated memory map and other platform-specific information.
 * @return An error code indicating the success or failure of the operation.
 */
__EXTERN oserr_t
DeviceTreeParseEarlyPlatform(
    _In_ const void*            deviceTree,
    _In_ uint32_t               deviceTreeSize,
    _In_ struct RpiBootContext* context);

/**
 * @brief Exclude a physical interval from the loader's normalized memory map.
 * The map must come from DeviceTreeParseEarlyPlatform. On failure the map
 * count is zero and the caller must discard the partial result.
 */
__EXTERN oserr_t
DeviceTreeReserveMemory(
    _In_ struct RpiBootContext* context,
    _In_ uint64_t physicalBase,
    _In_ uint64_t length);

/**
 * @brief Parse and validate the RPi loader header.
 * This essentially decodes the header that is defined by loader.ld and
 * extracts the kernel payload information. This is written into kernelInfo.
 */
enum RpiBootStatus RpiParseHeader(
    const unsigned char*         header,
    size_t                       headerLength,
    uint64_t                     loaderLength,
    uint64_t                     availableLength,
    struct RpiKernelInformation* kernelInfo);

/**
 * @brief Discover/reserve DTB resources before selecting image destinations. This
 * stage preserves the firmware-configured UART and retains the wrapper, stack, DTB, resident
 * firmware, and external payloads. No allocator exists before this stage.
 */
enum RpiBootStatus 
RpiPlatformPrepare(
    struct RpiBootContext* context);

/**
 * @brief Stage the PE in disjoint reserved RAM, including zero-fill and relocations.
 * Requires the ownership map from RpiPlatformPrepare. Supports the static ARM64
 * PE32+ kernel profile with DIR64/ABSOLUTE relocations; images without relocation
 * data require their preferred base. EntryPoint is the physical execution address
 * in this MMU-off profile. Failure leaves the kernel descriptor empty; a failed
 * reservation also invalidates the map, so callers must stop rather than retry.
 * A file pointer is never a loaded module. No kernel code runs at this stage.
 * Cache synchronization is deferred to RpiTransferToKernel.
 */
enum RpiBootStatus 
RpiLoadKernel(
    struct RpiBootContext* context);

/**
 * @brief Expand a static ARM64 Phoenix PE into reserved physical storage.
 * The image remains linked at its preferred userspace address. The module base
 * names staging storage, while its entry names the future userspace mapping.
 */
enum RpiBootStatus
RpiLoadPhoenix(
    struct RpiBootContext* context,
    const void*           file,
    size_t                length);

/**
 * @brief Validate the firmware initrd bundle and publish its boot resources.
 * Platform preparation must reserve the complete transport before this call.
 * An absent transport is supported for kernel-only diagnostic images.
 */
enum RpiBootStatus
RpiLoadResources(
    struct RpiBootContext* context);

/**
 * @brief Finalize the native bring-up contract from prepared platform and PE data.
 * Reuses the reserved wrapper stack and requires continuous reserved coverage
 * for all live storage. Phoenix and ramdisk must be absent until their separate
 * resource-loading stage exists; external payload storage stays reserved.
 * Success publishes Magic/Version. Failure clears both markers and callers must
 * stop. No allocation, DTB traversal, stack switch or kernel execution occurs.
 */
enum RpiBootStatus 
RpiBuildContract(
    struct RpiBootContext* context);

/**
 * @brief Enter the loaded physical PE entry at EL1h with x0 pointing to VBoot.
 * Requires a successful RpiBuildContract and unchanged ownership. The native
 * profile keeps MMU/data cache off throughout loading; EL1 firmware must allow
 * privileged register, counter/timer and cache-maintenance access from EL1.
 * The handoff disables instruction caching, synchronizes code visibility,
 * masks DAIF, selects the reserved stack top, and supplies x1-x3/x18/x29 as zero.
 * FP/SIMD stays disabled. Temporary vectors and a return stop remain in reserved
 * wrapper storage until the kernel replaces them. Never returns.
 */
_Noreturn void 
RpiTransferToKernel(
    struct RpiBootContext* context);

/**
 * @brief Halts the loader and enters an infinite wait state.
 * This stores the status provided in g_rpi_boot_status so it's observable by
 * a debugger.
 */
_Noreturn void 
RpiLoaderStop(
    enum RpiBootStatus status);

#endif
