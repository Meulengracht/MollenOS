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
#include <firmware/interrupt.h>

/** Supported controllers connecting the CPU to PCI buses.
 * ECAM is the standard memory-based interface to PCI configuration registers. */
enum FdtPciHostType {
    FdtPciHostEcam,
    FdtPciHostBcm2711,
    FdtPciHostBcm2712
};

#define FDT_PCI_PREFETCHABLE 0x40000000U

/** What a device can reach through a direct memory access (DMA) address range.
 * The purpose is determined from firmware resource descriptions. */
enum FdtDmaWindowKind {
    FdtDmaWindowUnknown, // Firmware does not provide enough information to classify it.
    FdtDmaWindowRam,     // System memory.
    FdtDmaWindowPeer,    // Resources of another PCI device.
    FdtDmaWindowMsi      // Address used to send a message-signaled interrupt (MSI).
};

/** An address range that connects PCI bus addresses to CPU physical addresses.
 * PhysicalBase already includes the address conversion through all parent buses.
 * Space identifies PCI I/O (1), 32-bit memory (2), or 64-bit memory (3). */
struct FdtPciWindow {
    uint32_t Space;
    // First PCI address cell (phys.hi), including the prefetch flag, which
    // allows reads ahead of time.
    uint32_t Attributes;
    // Purpose of a DmaWindows entry; unused for ordinary Windows entries.
    enum FdtDmaWindowKind Kind;
    uint64_t BusBase;
    uint64_t PhysicalBase;
    uint64_t Length;
};

/** Firmware settings for the PCI Express connection between host and device.
 * MaxSpeed is the PCIe generation number; Lanes is the number of data lanes.
 * Zero means firmware did not specify that setting. */
struct FdtPciLink {
    uint32_t MaxSpeed;
    uint32_t Lanes;
    int NoL0s; // Disable the L0s low-power state.
    int EnableSsc; // Vary the clock frequency slightly to reduce electrical interference.
    // Clock-request mode; points to a checked, zero-terminated firmware string.
    const char* ClkreqMode;
};

/** Describes the controller handling message-signaled interrupts (MSI).
 * A device raises an MSI by writing to an address instead of an interrupt pin.
 * RegisterBase describes controller registers; DoorbellBase is the address
 * devices write to trigger an interrupt when IsMip identifies a Broadcom MIP
 * controller. Addresses remain 64-bit even for callers with 32-bit pointers. */
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

/** Describes a PCI host's registers, address ranges, and interrupt connections.
 * Property pointers refer to Blob, the original firmware device-tree data.
 * Keep Blob mapped and unchanged until all copies of this description are unused.
 * Segment is the PCI domain number, which identifies a group of PCI buses.
 * Windows maps CPU accesses to PCI resources; DmaWindows maps device accesses
 * toward system memory or other resources. */
struct FdtPciHost {
    // Byte offset in the tree structure; identifies a node without a phandle ID.
    uint32_t NodeOffset;
    // CPU physical address of the host registers; for ECAM, starts at bus BusStart
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

/**
 * @brief Find a host interrupt by its name in "interrupt-names".
 *
 * Uses "interrupts-extended" when present, otherwise "interrupts" and its parent.
 *
 * @param host Host whose firmware properties describe the interrupt.
 * @param name Interrupt name to find.
 * @param interrupt Receives the controller, line, and settings on success.
 * @return OS_EOK on success, or an error if missing, malformed, or unsupported.
 */
extern oserr_t
FdtResolvePciNamedInterrupt(
        _In_ const struct FdtPciHost* host,
        _In_ const char* name,
        _Out_ struct FdtInterrupt* interrupt);

/**
 * @brief Call a function for each enabled, supported PCI host.
 *
 * First collect domain numbers set by "linux,pci-domain" on enabled, supported
 * hosts, including ones listed later in the tree. Hosts without that property
 * receive distinct numbers that skip all collected numbers. Repeated numbers
 * in firmware are kept; PCI host registration checks that their bus ranges
 * do not overlap. Disabled hosts and hosts with unusable resources are skipped.
 *
 * @param blob Firmware device-tree data, kept mapped and unchanged during use.
 * @param length Available size of blob in bytes.
 * @param callback Called with a temporary host description; copy it if needed
 *                 later. Its property pointers continue to refer to blob.
 * @param context Caller data passed to callback.
 * @return OS_EOK on success, or an error for invalid input, allocation failure,
 *         or exhausted domain numbers. Tree format and allocation errors are
 *         detected before any host callbacks.
 */
extern oserr_t
FdtEnumeratePciHosts(
        _In_ const void*  blob,
        _In_ size_t       length,
        _In_ FdtPciHostFn callback,
        _In_ void*        context);

/**
 * @brief Convert a PCI bus address range to a CPU physical address.
 *
 * @param host Host whose address mappings should be used.
 * @param space PCI I/O (1), 32-bit memory (2), or 64-bit memory (3).
 * @param address Starting PCI bus address.
 * @param length Size in bytes; the whole range must fit one host mapping.
 * @param physicalOut Receives the CPU physical address on success.
 * @return OS_EOK on success, OS_ENOENT if no mapping fits, or OS_EINVALPARAMS
 *         for invalid arguments or address overflow.
 */
extern oserr_t
FdtTranslatePciAddress(
        _In_ const struct FdtPciHost* host,
        _In_ uint32_t                space,
        _In_ uint64_t                address,
        _In_ uint64_t                length,
        _Out_ uint64_t*              physicalOut);

/**
 * @brief Find the controller interrupt connected to a PCI device's interrupt pin.
 *
 * Uses the firmware "interrupt-map" for traditional PCI pin interrupts (INTx).
 *
 * @param host Host containing the device and its interrupt map.
 * @param bus PCI bus number, 0 through 255.
 * @param slot Device slot on that bus, 0 through 31.
 * @param function Function within the device, 0 through 7.
 * @param pin Interrupt pin, 1 through 4 for INTA through INTD.
 * @param lineOut Receives the controller's interrupt line on success.
 * @param flagsOut Receives the interrupt trigger and polarity settings on success.
 * @return OS_EOK on success, or an error if missing, malformed, or unsupported.
 */
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
/**
 * @brief Check whether a node describes a supported PCI host controller.
 *
 * @param node Node whose "compatible" names should be checked.
 * @param type Receives the controller type when a match is found.
 * @return 1 for a supported host, otherwise 0.
 */
int
FdtPciHostType(
    _In_ const struct FdtNode* node,
    _Out_ enum FdtPciHostType* type);
#endif
