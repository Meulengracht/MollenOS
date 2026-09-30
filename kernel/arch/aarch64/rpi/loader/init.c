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

#include "loader.h"

extern unsigned char __rpi_loader_start[];
extern unsigned char __rpi_loader_end[];
extern unsigned char __rpi_payload_header[];

volatile unsigned int g_rpi_boot_stage;
volatile unsigned int g_rpi_boot_status;

static struct RpiBootContext g_context;

__attribute__((noinline)) _Noreturn void 
RpiLoaderStop(
    enum RpiBootStatus status)
{
    // UART discovery is deliberately not a prerequisite for reporting a stop.
    g_rpi_boot_status = status;
    
    for (;;) {
        __asm__ volatile("wfe" ::: "memory");
    }
}

/**
 * @brief This is the common entry point for the RPi loader. This is called
 * by entry.S
 */
_Noreturn void
RpiLoaderInit(
    uintptr_t    dtbPhysical,
    unsigned int board)
{
    enum RpiBootStatus status;
    uint64_t           loaderLength;

    // Get the length of our loader
    loaderLength = (uintptr_t)__rpi_loader_end - (uintptr_t)__rpi_loader_start;
    
    g_context.DtbPhysical = dtbPhysical;
    g_context.Board = board;
    g_rpi_boot_stage = RpiBootValidatePayload;
    
    status = RpiParseHeader(
        __rpi_payload_header,
        RPI_PAYLOAD_HEADER_SIZE,
        loaderLength,
        loaderLength + RPI_PAYLOAD_MAX_SIZE, 
        &g_context.Payload
    );
    if (status != RpiBootOk) {
        RpiLoaderStop(status);
    }

    // No payload is dereferenced until discovery can establish memory ownership.
    g_rpi_boot_stage = RpiBootDiscoverPlatform;
    status = RpiPlatformPrepare(&g_context);
    if (status != RpiBootOk) {
        RpiLoaderStop(status);
    }
    
    g_rpi_boot_stage = RpiBootLoadKernel;
    status = RpiLoadKernel(&g_context);
    if (status != RpiBootOk) {
        RpiLoaderStop(status);
    }
    
    g_rpi_boot_stage = RpiBootBuildContract;
    status = RpiBuildContract(&g_context);
    if (status != RpiBootOk) {
        RpiLoaderStop(status);
    }
    
    g_rpi_boot_stage = RpiBootTransfer;
    RpiTransferToKernel(&g_context);
}
