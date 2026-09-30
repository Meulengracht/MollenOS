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

#include <vboot/vboot.h>

#define RPI_PAYLOAD_HEADER_SIZE 64u
#define RPI_PAYLOAD_MAX_SIZE (64u * 1024u * 1024u)

enum RpiBootStatus { 
    RpiBootOk,
    RpiBootInvalidPayload,
    RpiBootUnsupported
};

enum RpiBootStage {
    RpiBootValidatePayload = 1,
    RpiBootDiscoverPlatform,
    RpiBootLoadKernel,
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
};

#define SWAP16(val) ((((val) >> 8) & 0xff) | (((val) & 0xff) << 8))
#define SWAP32(val) ((((val) >> 24) & 0xff) | (((val) >> 8) & 0xff00) | (((val) & 0xff00) << 8) | (((val) & 0xff) << 24))
#define SWAP64(val) ((((val) >> 56) & 0xff) | (((val) >> 40) & 0xff00) | (((val) >> 24) & 0xff0000) | (((val) >> 8) & 0xff000000) | \
                     (((val) & 0xff000000) << 8) | (((val) & 0xff0000) << 24) | (((val) & 0xff00) << 40) | (((val) & 0xff) << 56))

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
 * Discover/reserve DTB resources before selecting image destinations. This
 * stage owns early UART policy and retains the wrapper, stack, DTB, resident
 * firmware, and external payloads. No allocator exists before this stage.
 */
enum RpiBootStatus 
RpiPlatformPrepare(
    struct RpiBootContext* context);

/**
 * Stage the PE in disjoint reserved RAM, including zero-fill and relocations.
 * A file pointer is never a loaded module. No kernel code runs at this stage.
 */
enum RpiBootStatus 
RpiLoadKernel(
    struct RpiBootContext* context);

/**
 * Publish a versioned native/FDT VBoot contract with loaded Phoenix and intact
 * initrd descriptors. Success requires all pointed-to storage to remain owned.
 */
enum RpiBootStatus 
RpiBuildContract(
    struct RpiBootContext* context);

/**
 * Establish kernel entry state and instruction visibility, then branch to the
 * loaded PE entry with x0 pointing to VBoot. Never returns.
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
