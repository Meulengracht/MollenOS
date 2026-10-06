/**
 * Copyright 2026, Philip Meulengracht
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

#ifndef DEVICED_FIRMWARE_PCI_H
#define DEVICED_FIRMWARE_PCI_H
#include <os/osdefs.h>
#include "interrupt.h"

enum FdtPciHostType {
    FdtPciHostEcam,
    FdtPciHostBcm2711,
    FdtPciHostBcm2712
};

#define FDT_PCI_PREFETCHABLE 0x40000000U

/** The purpose of an inbound aperture, established from firmware resources. */
enum FdtDmaWindowKind {
    FdtDmaWindowUnknown,
    FdtDmaWindowRam,
    FdtDmaWindowPeer,
    FdtDmaWindowMsi
};

/** Physical host windows translated through all ancestor buses. */
struct FdtPciWindow {
    uint32_t Space;
    uint32_t Attributes; // Complete PCI phys.hi cell, including prefetchability
    enum FdtDmaWindowKind Kind; // Applies to inbound DMA windows
    uint64_t BusBase;
    uint64_t PhysicalBase;
    uint64_t Length;
};

/** PCIe link policy. Zero speed/lanes means firmware did not specify a value. */
struct FdtPciLink {
    uint32_t MaxSpeed;
    uint32_t Lanes;
    int NoL0s;
    int EnableSsc;
    const char* ClkreqMode; // Borrowed, validated NUL-terminated string
};

/** MSI provider resources; addresses remain 64-bit even for 32-bit clients. */
struct FdtPciMsi {
    uint32_t Controller;
    int IsMip;
    uint64_t RegisterBase;
    uint64_t RegisterLength;
    uint64_t DoorbellBase;
    uint64_t DoorbellLength;
    struct FdtInterrupt Interrupt;
    uint32_t InterruptCount;
    uint32_t Offset; // Message n targets Interrupt.Line + Offset + n
};

/** Host resources and borrowed routing data, valid while the blob remains mapped. */
struct FdtPciHost {
    uint32_t NodeOffset; // Identity within Blob, independent of optional phandles
    // Physical address of the ECAM window for bus <BusStart>
    uint64_t EcamBase;
    uint64_t EcamLength;
    uint32_t Segment;
    uint8_t  BusStart;
    uint8_t  BusEnd;
    enum FdtPciHostType Type;
    uint32_t Phandle;
    struct FdtPciLink Link;
    const uint8_t* MsiParent;
    uint32_t MsiParentLength;
    uint32_t InterruptParent;
    const uint8_t* Interrupts;
    uint32_t InterruptsLength;
    const uint8_t* InterruptsExtended;
    uint32_t InterruptsExtendedLength;
    const uint8_t* InterruptNames;
    uint32_t InterruptNamesLength;
    struct FdtPciWindow Windows[16];
    uint32_t WindowCount;
    struct FdtPciWindow DmaWindows[16];
    uint32_t DmaWindowCount;
    uint64_t ScbSize;
    const uint8_t* Resets;
    uint32_t ResetsLength;
    const uint8_t* ResetNames;
    uint32_t ResetNamesLength;
    const uint8_t* Clocks;
    uint32_t ClocksLength;
    const uint8_t* ClockNames;
    uint32_t ClockNamesLength;
    const void* Blob;
    size_t BlobLength;
    const uint8_t* InterruptMap;
    uint32_t InterruptMapLength;
    const uint8_t* InterruptMask;
    uint32_t InterruptMaskLength;
};

typedef void (*FdtPciHostFn)(const struct FdtPciHost* host, void* context);

/** @brief Resolves a named host interrupt; extended specifiers take precedence. */
extern oserr_t
FdtResolvePciNamedInterrupt(
        _In_ const struct FdtPciHost* host,
        _In_ const char* name,
        _Out_ struct FdtInterrupt* interrupt);

/**
 * @brief Invokes <callback> for enabled PCI hosts, including non-ECAM Broadcom hosts.
 * Explicit domains on enabled supported hosts are reserved before assigning
 * missing domains, including declarations later in firmware order. Automatic
 * assignments are distinct and skip every reservation. Explicit duplicates
 * remain intact for PCI registration to validate their bus intervals.
 * @return OS_EINVALPARAMS if the blob is malformed, or OS_EOOM if domain
 *         reservations cannot be allocated. Both fail before host callbacks.
 */
extern oserr_t
FdtEnumeratePciHosts(
        _In_ const void*  blob,
        _In_ size_t       length,
        _In_ FdtPciHostFn callback,
        _In_ void*        context);

/** @brief Translates a complete PCI resource through a matching host window. */
extern oserr_t
FdtTranslatePciAddress(
        _In_ const struct FdtPciHost* host,
        _In_ uint32_t                space,
        _In_ uint64_t                address,
        _In_ uint64_t                length,
        _Out_ uint64_t*              physicalOut);

/** @brief Resolves a PCI INTx interrupt-map entry to a supported controller interrupt. */
extern oserr_t
FdtResolvePciInterrupt(
        _In_ const struct FdtPciHost* host,
        _In_ unsigned int            bus,
        _In_ unsigned int            slot,
        _In_ unsigned int            function,
        _In_ unsigned int            pin,
        _Out_ int*                   lineOut,
        _Out_ unsigned int*          flagsOut);


struct FdtNode;
/** @brief Recognize hosts in the PCI adapter, independently of generic nodes. */
int
FdtPciHostType(
    _In_ const struct FdtNode* node,
    _Out_ enum FdtPciHostType* type);
#endif
