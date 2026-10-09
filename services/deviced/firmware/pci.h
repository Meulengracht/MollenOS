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

#ifndef __FIRMWARE_PCI_H__
#define __FIRMWARE_PCI_H__

#include <os/osdefs.h>
#include <firmware/interrupt.h>

// Forward declarations
struct FdtNode;

/** 
 * @brief PCI host controller types supported by the firmware reader.
 * The host controller acts as a proxy to one or more PCI buses.
 * ECAM places PCI device configuration registers in a
 * memory address range, where the processor can access them.
 */
enum FdtPciHostType {
    FdtPciHostEcam,
    FdtPciHostBcm2711,
    FdtPciHostBcm2712
};

// PCI memory window may be read ahead by the processor.
#define FDT_PCI_PREFETCHABLE 0x40000000U

/** 
 * @brief Describes what a PCI device can reach through one of its DMA address ranges.
 * Direct memory access (DMA) lets a device read or write memory without asking
 * the processor to copy each piece of data. Firmware descriptions help identify
 * the kind of range; unknown means that its purpose could not be determined.
 */
enum FdtDmaWindowKind {
    // Firmware does not provide enough information to identify its purpose.
    FdtDmaWindowUnknown,
    // Main system memory that the device may read or write.
    FdtDmaWindowRam,
    // Address space belonging to another PCI device.
    FdtDmaWindowPeer,
    // Address a device writes to raise a message-signaled interrupt (MSI).
    FdtDmaWindowMsi
};

/** Describes one continuous range of addresses visible to a PCI device.
 * PCI devices use bus addresses, which can differ from the physical addresses
 * used by the processor. PhysicalBase is the final processor-visible address,
 * after applying address changes made by parent buses. Space identifies the
 * PCI address kind: 1 is I/O, 2 is 32-bit memory, and 3 is 64-bit memory. */
struct FdtPciWindow {
    // Address kind: PCI I/O (1), 32-bit memory (2), or 64-bit memory (3).
    uint32_t Space;
    // Firmware attributes from the first PCI address cell. The prefetch flag
    // means the processor may read nearby addresses ahead of the request.
    uint32_t Attributes;
    // Describes a DMA range's purpose. This field is not used for host windows.
    enum FdtDmaWindowKind Kind;
    // First address in this range as seen by the PCI device.
    uint64_t BusBase;
    // Corresponding first address as seen by the processor.
    uint64_t PhysicalBase;
    // Number of bytes covered by the range.
    uint64_t Length;
};

/** 
 * @brief Firmware-provided settings for the PCI Express link.
 */
struct FdtPciLink {
    // Highest PCI Express generation specified by firmware, or zero if unknown.
    uint32_t MaxSpeed;
    // Number of parallel data paths specified by firmware, or zero if unknown.
    uint32_t Lanes;
    // Nonzero when the low-power L0s link state should be disabled.
    int NoL0s;
    // Nonzero when the link clock should vary slightly to reduce electrical interference.
    int EnableSsc;
    // Clock-request mode; this points to a validated, zero-terminated string
    // in the firmware data. Clock requests let a device ask for its link clock.
    const char* ClkreqMode;
};

/** 
 * @brief Describes the controller used to deliver message-signaled interrupts (MSIs).
 * An MSI is an interrupt raised when a device writes to a designated address,
 * rather than changing a physical interrupt pin. RegisterBase and
 * RegisterLength describe the controller's own registers. The Broadcom MIP
 * controller receives device writes that raise interrupts; DoorbellBase and
 * DoorbellLength describe the address range used for those writes. Addresses
 * remain 64-bit even when the program uses 32-bit pointers.
 */
struct FdtPciMsi {
    // Firmware identifier for the interrupt controller.
    uint32_t Controller;
    // Nonzero when this controller receives device writes that raise MSIs.
    int IsMip; 
    // First physical address of the controller's registers.
    uint64_t RegisterBase;
    // Size of the controller's register range in bytes.
    uint64_t RegisterLength;
    // First address devices write to when raising an MSI.
    uint64_t DoorbellBase;
    // Size of the MSI write-address range in bytes.
    uint64_t DoorbellLength;
    // Interrupt used by the controller to notify the processor.
    struct FdtInterrupt Interrupt;
    // Number of interrupt lines available for device messages.
    uint32_t InterruptCount;
    // Message number n uses interrupt line Interrupt.Line + Offset + n.
    uint32_t Offset;
};

/** 
 * @brief Firmware description of one PCI host controller and its connections.
 * It includes the controller's configuration-register range, the buses it
 * serves, address translations, and interrupt information. A PCI domain
 * (also called a segment) identifies a group of PCI buses. Windows translate
 * processor addresses when the processor accesses PCI resources; DmaWindows
 * describe where devices' direct memory access (DMA) requests go. Most property
 * pointers refer into Blob, the original firmware Device Tree data. Keep Blob
 * mapped and unchanged for as long as this description or any copy is in use.
 */
struct FdtPciHost {
    // Byte position of this node in the Device Tree structure.
    uint32_t NodeOffset;
    // Physical start of the configuration-register range; bus BusStart is first for ECAM.
    uint64_t EcamBase;
    // Size in bytes of the configuration-register range.
    uint64_t EcamLength;
    // PCI domain number identifying this group of buses.
    uint32_t Segment;
    // First PCI bus number handled by this host.
    uint8_t  BusStart;
    // Last PCI bus number handled by this host.
    uint8_t  BusEnd;
    // Kind of host controller.
    enum FdtPciHostType Type;
    // Device Tree reference number used to identify this node.
    uint32_t Phandle;
    // Optional firmware settings for the PCI Express link.
    struct FdtPciLink Link;
    // Firmware data naming the controller that handles this host's MSIs.
    const uint8_t* MsiParent;
    // Size of MsiParent in bytes.
    uint32_t MsiParentLength;
    // Device Tree reference number for the default interrupt controller.
    uint32_t InterruptParent;
    // Firmware interrupt descriptions using InterruptParent.
    const uint8_t* Interrupts;
    // Size of Interrupts in bytes.
    uint32_t InterruptsLength;
    // Interrupt descriptions that name a controller for each interrupt.
    const uint8_t* InterruptsExtended;
    // Size of InterruptsExtended in bytes.
    uint32_t InterruptsExtendedLength;
    // Firmware names corresponding to the interrupt descriptions.
    const uint8_t* InterruptNames;
    // Size of InterruptNames in bytes.
    uint32_t InterruptNamesLength;
    // Address ranges used by the processor to access PCI resources.
    struct FdtPciWindow Windows[16];
    // Number of valid entries in Windows.
    uint32_t WindowCount;
    // Address ranges describing where PCI device memory requests are sent.
    struct FdtPciWindow DmaWindows[16];
    // Number of valid entries in DmaWindows.
    uint32_t DmaWindowCount;
    // Firmware-selected size, in bytes, for the PCI address range to system RAM; zero if unspecified.
    uint64_t ScbSize;
    // Firmware descriptions of reset controls for this host.
    const uint8_t* Resets;
    // Size of Resets in bytes.
    uint32_t ResetsLength;
    // Names corresponding to the reset controls.
    const uint8_t* ResetNames;
    // Size of ResetNames in bytes.
    uint32_t ResetNamesLength;
    // Firmware descriptions of clocks used by this host.
    const uint8_t* Clocks;
    // Size of Clocks in bytes.
    uint32_t ClocksLength;
    // Names corresponding to the clocks.
    const uint8_t* ClockNames;
    // Size of ClockNames in bytes.
    uint32_t ClockNamesLength;
    // Original Device Tree data containing the property pointers above.
    const void* Blob;
    // Size of Blob in bytes.
    size_t BlobLength;
    // Rules mapping a PCI device's interrupt pin to a controller interrupt.
    const uint8_t* InterruptMap;
    // Size of InterruptMap in bytes.
    uint32_t InterruptMapLength;
    // Selects which parts of a PCI address and pin are compared by InterruptMap.
    const uint8_t* InterruptMask;
    // Size of InterruptMask in bytes.
    uint32_t InterruptMaskLength;
};

typedef void (*FdtPciHostFn)(const struct FdtPciHost* host, void* context);

/**
 * @brief Find an interrupt connection on the PCI host by its firmware name.
 *
 * The name is looked up in the host's "interrupt-names" property. When the
 * host has "interrupts-extended", each interrupt entry supplies its own
 * controller reference. Otherwise, entries in "interrupts" all use the
 * controller named by the host's "interrupt-parent" property.
 *
 * @param host PCI host whose firmware data describes the interrupt.
 * @param name Exact name of the interrupt to find.
 * @param interrupt Receives the controller, interrupt number, and trigger settings.
 * @return OS_EOK on success. Returns an error if the name or data is missing,
 *         malformed, or describes an interrupt type this code cannot handle.
 */
__EXTERN oserr_t
FdtResolvePciNamedInterrupt(
    _In_  const struct FdtPciHost* host,
    _In_  const char*              name,
    _Out_ struct FdtInterrupt*     interrupt);

/**
 * @brief Find supported PCI hosts and call a function once for each one.
 *
 * This reads the firmware Device Tree and reports each enabled host controller
 * whose description is supported and usable. A PCI domain number, also called
 * a segment number, distinguishes one group of PCI buses from another. The
 * function first gathers domain numbers explicitly supplied by firmware, so
 * hosts without a number can be given distinct values without taking one that
 * appears later in the tree. If firmware repeats a number, both hosts keep it;
 * later host registration is responsible for checking whether their bus
 * number ranges conflict. Disabled hosts and hosts with invalid or unsupported
 * resource descriptions are not reported.
 *
 * @param blob Firmware Device Tree data. It must stay mapped and unchanged
 *             while this function runs and while any callback data is used.
 * @param length Number of available bytes in blob.
 * @param callback Function called for each usable host. The host description
 *                 is temporary, so copy it if it must be kept after the call.
 *                 Its property pointers still refer to blob.
 * @param context Caller-owned value passed unchanged to each callback.
 * @return OS_EOK on success. Returns an error for invalid input, memory
 *         allocation failure, or when no unused domain number remains. Device
 *         Tree format errors and allocation failures are found before callbacks
 *         begin, so those failures do not result in partial host reporting.
 */
__EXTERN oserr_t
FdtEnumeratePciHosts(
    _In_ const void*  blob,
    _In_ size_t       length,
    _In_ FdtPciHostFn callback,
    _In_ void*        context);

/**
 * @brief Convert a PCI device address range to the processor's physical address.
 *
 * PCI devices use bus addresses. The processor may use a different physical
 * address for the same resource, so this function applies the host's firmware
 * address mappings. The entire requested range must fit within one mapping.
 *
 * @param host Host whose address mappings describe the conversion.
 * @param space Address kind: PCI I/O (1), 32-bit memory (2), or 64-bit memory (3).
 * @param address First address in the range as seen by the PCI device.
 * @param length Number of bytes to translate; zero-length ranges are invalid.
 * @param physicalOut Receives the first corresponding processor physical address.
 * @return OS_EOK on success, OS_ENOENT if no single mapping contains the full
 *         range, or OS_EINVALPARAMS for invalid arguments or an address overflow.
 */
__EXTERN oserr_t
FdtTranslatePciAddress(
    _In_  const struct FdtPciHost* host,
    _In_  uint32_t                 space,
    _In_  uint64_t                 address,
    _In_  uint64_t                 length,
    _Out_ uint64_t*                physicalOut);

/**
 * @brief Find which interrupt a PCI device's pin is connected to.
 *
 * Older PCI devices can signal an interrupt by asserting one of four pins,
 * named INTA through INTD. The host's firmware "interrupt-map" property says
 * which interrupt controller and interrupt number each pin reaches. The
 * device's bus, slot, and function identify it on the PCI bus.
 *
 * @param host Host whose firmware data contains the pin-to-interrupt mapping.
 * @param bus PCI bus number, from 0 through 255.
 * @param slot Device number on that bus, from 0 through 31.
 * @param function Function number within that device, from 0 through 7.
 * @param pin Pin number: 1 is INTA, 2 is INTB, 3 is INTC, and 4 is INTD.
 * @param lineOut Receives the interrupt number assigned by the controller.
 * @param flagsOut Receives settings that describe how the interrupt is signaled.
 * @return OS_EOK on success, or an error if the mapping is absent, malformed,
 *         or uses a controller format that this code cannot handle.
 */
__EXTERN oserr_t
FdtResolvePciInterrupt(
    _In_  const struct FdtPciHost* host,
    _In_  unsigned int             bus,
    _In_  unsigned int             slot,
    _In_  unsigned int             function,
    _In_  unsigned int             pin,
    _Out_ int*                     lineOut,
    _Out_ unsigned int*            flagsOut);


/**
 * @brief Check whether a Device Tree node describes a supported PCI host.
 *
 * Each node's "compatible" property names the hardware it describes. This
 * function checks those names against the host controllers understood here.
 *
 * @param node Device Tree node to inspect.
 * @param type Receives the matching controller type when one is supported.
 * @return 1 if the node names a supported host controller, otherwise 0.
 */
__EXTERN int
FdtPciHostType(
    _In_  const struct FdtNode* node,
    _Out_ enum FdtPciHostType*  type);

#endif //!__FIRMWARE_PCI_H__
