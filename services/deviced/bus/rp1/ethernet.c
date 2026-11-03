/**
 * MollenOS
 *
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

#include <bus/rp1/ethernet.h>
#include <bus/rp1/rp1.h>
#include <bus/rp1/runtime-private.h>
#include <bus/pci/device.h>
#include <firmware/ethernet.h>
#include <firmware/platform-runtime.h>
#include <core/io.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define RP1_CLOCK_ENABLE (1U << 11)

/**
 * @brief Ethernet setup for one driver, with saved registers for cleanup.
 * Io maps the clock controller, GPIO control and GPIO pads, in that order.
 * Original follows g_ethernetOffsets. Changed counts the entries from the start
 * that may need restoring, including writes whose verification failed.
 * Ready allows DMA and interrupt requests after setup completes.
 */
struct Rp1Ethernet {
    uuid_t                             Principal;
    struct DeviceEthernetConfiguration Config;
    DeviceIo_t                         Io[3];
    int                                Mapped[3];
    uint32_t                           Original[6];
    uint32_t                           Changed;
    int                                Ready;
};

// ETH and TSU divider/control pairs, followed by GPIO32 pad and control.
// GPIO32 is bank 1, pin 4. Output override avoids changing shared RIO registers.
static const uint32_t g_ethernetOffsets[] = { 0x68, 0x64, 0x138, 0x134, 0x4014, 0x4024 };
static const uint32_t g_ethernetMaps[] = { 0, 0, 0, 0, 2, 1 };

int
Rp1EthernetIsDevice(
    _In_ const struct Rp1Child* child)
{
    uint32_t offset = 0;

    // Check all zero-terminated strings in the firmware compatible list.
    // A board-specific name may come before the supported controller name.
    while (offset < child->Firmware.CompatibleLength) {
        if (!strcmp(child->Firmware.Compatible + offset, "raspberrypi,rp1-gem")) {
            return 1;
        }
        offset += strlen(child->Firmware.Compatible + offset) + 1;
    }
    return 0;
}

oserr_t
Rp1EthernetStatus(
    _In_ const struct Rp1Child* child,
    _In_ uuid_t                principal)
{
    // Other RP1 children do not use this Ethernet-specific preparation path.
    if (!Rp1EthernetIsDevice(child)) {
        return OS_EOK;
    }
    
    if (child->Bus->Runtime != NULL && child->Bus->Runtime->Quiescing) {
        return OS_EBUSY;
    }
    
    if (!child->Bus->Ready || child->Ethernet == NULL || !child->Ethernet->Ready) {
        return OS_EBUSY;
    }
    
    // Internal queries may omit the process ID. Requests from a driver must
    // come from the same process that prepared this MAC.
    if (principal != UUID_INVALID && principal != child->Ethernet->Principal) {
        return OS_EPERMISSIONS;
    }
    return OS_EOK;
}

static int
__EthernetInUse(
    _In_ const struct Rp1Child* child)
{
    // Before removing setup, all DMA references and interrupt reservations must
    // be released. Disabling an interrupt leaves its reservation in place.
    if (child->Resources.DmaReferences != 0) {
        return 1;
    }

    if (child->Bus->Runtime != NULL) {
        for (unsigned int i = 0; i < FDT_RP1_INTERRUPT_COUNT; i++) {
            if (child->Bus->Runtime->Sources[i].Owner == child) {
                return 1;
            }
        }
    }
    return 0;
}

static oserr_t
__EthernetWrite(
    _In_ struct Rp1Ethernet* state,
    _In_ uint32_t            index,
    _In_ uint32_t            value)
{
    DeviceIo_t* io = &state->Io[g_ethernetMaps[index]];
    uint32_t    offset = g_ethernetOffsets[index];
    oserr_t     status;

    status = WriteDeviceIo(io, offset, value, 4);
    if (status != OS_EOK) {
        return status;
    }
    // Verify the register through the same mapping before reporting success.
    // WriteDeviceIo returning OS_EOK only confirms the I/O write.
    return ReadDeviceIo(io, offset, 4) == value ? OS_EOK : OS_EUNKNOWN;
}

oserr_t
Rp1EthernetRelease(
    _InOut_ struct Rp1Child* child)
{
    struct Rp1Ethernet* state = child->Ethernet;
    oserr_t             status;
    unsigned int        i;

    if (state == NULL) {
        return OS_EOK;
    }

    if (__EthernetInUse(child)) {
        return OS_EBUSY;
    }

    // The caller must have stopped the MAC. Block further use before restoring
    // clocks and pins. This routine only undoes the bus setup.
    state->Ready = 0;
    child->Resources.Prepared = 0;
    
    // Restore saved registers in reverse order and verify each write.
    // On failure, keep the state and mappings so the next stop request can
    // continue from this entry.
    while (state->Changed) {
        i = state->Changed - 1;
        status = __EthernetWrite(state, i, state->Original[i]);
        if (status != OS_EOK) {
            return status;
        }
        state->Changed--;
    }
    
    // Register restoration still needs these mappings, so release them only
    // after every saved value has been written back.
    for (i = 3; i != 0; i--) {
        status = DmIoRelease(&state->Io[i - 1], &state->Mapped[i - 1]);
        if (status != OS_EOK) {
            return status;
        }
    }

    free(state);
    child->Ethernet = NULL;
    return OS_EOK;
}

static struct Rp1Child*
__EthernetFindChild(
    _In_ struct Rp1Bus* bus,
    _In_ uint32_t       node)
{
    struct Rp1Child* child;

    // The caller must hold Bus->Lock. Find sibling blocks, such as the clock
    // controller, by their firmware node offset in the child list.
    for (child = bus->Children; child != NULL; child = child->Next) {
        if (child->Firmware.NodeOffset == node) {
            return child;
        }
    }
    return NULL;
}

static oserr_t
__EthernetMap(
    _InOut_ struct Rp1Ethernet* state,
    _In_    unsigned int        index,
    _In_    struct Rp1Child*    child,
    _In_    unsigned int        reg,
    _In_    uint64_t            minimum)
{
    const struct FdtRp1Range* range;
    oserr_t                   status;

    // Register offsets below are specific to this block. Reject a missing or
    // short firmware range before mapping it or accessing those offsets.
    if (child == NULL || reg >= child->Firmware.RegisterCount) {
        return OS_ENOENT;
    }

    range = &child->Firmware.Registers[reg];
    if (range->Length < minimum) {
        return OS_EINVALPARAMS;
    }

    status = DmIoCreateMemory(&state->Io[index], range->Base, range->Length);
    if (status != OS_EOK) {
        return status;
    }

    // Keep the handle if mapping fails. Release uses the handle and Mapped
    // flag to determine which resources need cleanup.
    status = AcquireDeviceIo(&state->Io[index]);
    if (status == OS_EOK) {
        state->Mapped[index] = 1;
    }
    return status;
}

static oserr_t
__EthernetDependencies(
    _In_    struct Rp1Child*    child,
    _InOut_ struct Rp1Ethernet* state)
{
    static const uint32_t clocks[] = { 12, 12, 29, 16 };
    static const char     names[] = "pclk\0hclk\0tsu_clk\0tx_clk";
    
    const struct FdtTree*          tree = child->Bus->Host->Tree;
    const struct FdtResources*     node = FdtTreeNode(tree, child->Firmware.NodeOffset);
    const struct FdtResources*     clock;
    const struct FdtResources*     gpio;
    struct FdtPlatformDependencies dependencies;
    struct FdtResources            oscillator;
    uint32_t                       rate;
    struct Rp1Child*               clockChild;
    struct Rp1Child*               gpioChild;
    unsigned int                   i;
    oserr_t                        status;

    status = FdtPlatformLocalDependencies(tree, node->NodeOffset, &dependencies);
    if (status != OS_EOK) {
        return status;
    }

    // The register sequence requires the C0 clock layout below. Check the
    // clock names, order and IDs before changing any registers.
    if (dependencies.Count != 4 || node->ClockNamesLength != sizeof(names) ||
        memcmp(node->ClockNames, names, sizeof(names))) {
        return OS_ENOTSUPPORTED;
    }

    for (i = 0; i < 4; i++) {
        if (dependencies.Entries[i].Kind != FdtPlatformClock ||
            dependencies.Entries[i].ArgumentCount != 1 ||
            dependencies.Entries[i].Arguments[0] != clocks[i]) {
            return OS_ENOTSUPPORTED;
        }

        if (dependencies.Entries[i].NodeOffset != dependencies.Entries[0].NodeOffset) {
            return OS_ENOTSUPPORTED;
        }
    }

    clock = FdtTreeNode(tree, dependencies.Entries[0].NodeOffset);
    if (!FdtCompatible(&clock->View, "raspberrypi,rp1-clocks")) {
        return OS_ENOTSUPPORTED;
    }

    if (clock->ClocksLength != 4) {
        return OS_ENOTSUPPORTED;
    }

    // The divider values require a fixed 50 MHz input. A different source
    // needs a new rate calculation.
    status = FdtFindResources(tree, FdtReadBe32(clock->Clocks), &oscillator);
    if (status != OS_EOK) {
        return status;
    }

    status = FdtScalar(&oscillator.View, "clock-frequency", &rate);
    if (status != OS_EOK || rate != 50000000 ||
        !FdtCompatible(&oscillator.View, "fixed-clock")) {
        return OS_ENOTSUPPORTED;
    }

    // The sibling devices' translated ranges give the addresses mapped by the
    // PCI host. GPIO mappings are needed only when a reset line is configured.
    clockChild = __EthernetFindChild(child->Bus, clock->NodeOffset);
    status = __EthernetMap(state, 0, clockChild, 0, 0x8018);
    if (status != OS_EOK || !state->Config.Reset.GpioPresent) {
        return status;
    }

    // This reset sequence requires active-low GPIO32. Reject other pins and
    // polarities before accessing the GPIO registers.
    gpio = FdtTreeNode(tree, state->Config.Reset.Controller);
    if (!FdtCompatible(&gpio->View, "raspberrypi,rp1-gpio") ||
        state->Config.Reset.Pin != 32 || state->Config.Reset.Flags != 1) {
        return OS_ENOTSUPPORTED;
    }

    gpioChild = __EthernetFindChild(child->Bus, gpio->NodeOffset);
    status = __EthernetMap(state, 1, gpioChild, 0, 0x4028);
    if (status != OS_EOK) {
        return status;
    }
    return __EthernetMap(state, 2, gpioChild, 2, 0x4018);
}

static oserr_t
__EthernetClocks(
    _InOut_ struct Rp1Ethernet* state)
{
    DeviceIo_t* io = &state->Io[0];
    uint32_t    values[4];
    uint32_t    i;
    uint32_t    primary;
    oserr_t     status;

    // Firmware must have set up the shared 1 GHz SYS PLL and its 125 MHz
    // secondary output. Check both before using them for Ethernet, since
    // changing this PLL would also change clocks used by other children.
    if ((ReadDeviceIo(io, 0x8000, 4) & 0x8000003fU) != 0x80000001U ||
        (ReadDeviceIo(io, 0x8004, 4) & 0x29U) != 0) {
        return OS_ENOTSUPPORTED;
    }

    if (ReadDeviceIo(io, 0x8008, 4) != 20 || ReadDeviceIo(io, 0x800c, 4) != 0 ||
        (ReadDeviceIo(io, 0x8014, 4) & 0x11f00U) != 0x800U) {
        return OS_ENOTSUPPORTED;
    }

    primary = (uint32_t)ReadDeviceIo(io, 0x8010, 4);
    if (((primary >> 16) & 7U) * ((primary >> 12) & 7U) != 5) {
        return OS_ENOTSUPPORTED;
    }

    if ((ReadDeviceIo(io, 0x14, 4) & 3U) != 2 || ReadDeviceIo(io, 0x18, 4) != 1) {
        return OS_ENOTSUPPORTED;
    }

    // Save all affected values before the first change so stop and partial
    // setup failures can restore the firmware's clock configuration.
    for (i = 0; i < 4; i++) {
        state->Original[i] = (uint32_t)ReadDeviceIo(io, g_ethernetOffsets[i], 4);
    }

    // Both auxiliary muxes select input zero: SYS secondary for ETH, XOSC for TSU.
    values[0] = values[2] = 1;
    values[1] = (state->Original[1] & ~0x3e0U) | RP1_CLOCK_ENABLE;
    values[3] = (state->Original[3] & ~0x3e0U) | RP1_CLOCK_ENABLE;

    for (i = 0; i < 4; i++) {
        // Record the register before writing it. A failed readback can follow
        // a successful write, so cleanup must restore this register too.
        state->Changed = i + 1;
        status = __EthernetWrite(state, i, values[i]);
        if (status != OS_EOK) {
            return status;
        }
    }
    return OS_EOK;
}

static oserr_t
__EthernetDelay(
    _In_ uint32_t microseconds)
{
    struct timespec interval = {
        .tv_sec = microseconds / 1000000,
        .tv_nsec = (microseconds % 1000000) * 1000
    };
    struct timespec remaining;
    int             status;

    // If sleep is interrupted, wait the remaining interval so the PHY gets
    // the full reset pulse.
    do {
        status = thrd_sleep(&interval, &remaining);
        if (status == -1) {
            interval = remaining;
        }
    } while (status == -1);
    return status == 0 ? OS_EOK : OS_EUNKNOWN;
}

static oserr_t
__EthernetResetPhy(
    _InOut_ struct Rp1Ethernet* state)
{
    uint32_t control;
    uint32_t pad;
    oserr_t  status;

    if (!state->Config.Reset.GpioPresent) {
        return OS_EOK;
    }

    // Save pin settings only on the first reset. Later resets must not replace
    // the original board state with values from an earlier pulse.
    if (state->Changed == 4) {
        state->Original[4] = (uint32_t)ReadDeviceIo(&state->Io[2], 0x4014, 4);
        state->Original[5] = (uint32_t)ReadDeviceIo(&state->Io[1], 0x4024, 4);
    }

    // Enable the pad and force output low, then high. Preserve input/IRQ settings.
    // Output override avoids modifying shared GPIO output registers that other
    // children may be using. Set the low level before enabling the pad.
    pad = state->Original[4] & ~0x80U;
    control = (state->Original[5] & ~0xf01fU) | 0xe005U;
    
    // Include both pin registers before either write, since a failed write or
    // readback still requires restoring the saved values during stop.
    state->Changed = 6;
    
    status = __EthernetWrite(state, 5, control);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetWrite(state, 4, pad);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetDelay(state->Config.Reset.AssertUs);
    if (status != OS_EOK) {
        return status;
    }

    status = __EthernetWrite(state, 5, control | 0x1000U);
    if (status != OS_EOK) {
        return status;
    }

    // The PHY may need time after reset is released before it accepts MDIO.
    return __EthernetDelay(state->Config.Reset.DeassertUs);
}

oserr_t
Rp1EthernetControl(
    _InOut_ struct Rp1Child*    child,
    _In_    uuid_t              principal,
    _In_    enum OSIOCtlRequest request,
    _In_    const void*         buffer,
    _In_    size_t              length)
{
    struct Rp1Ethernet*   state = child->Ethernet;
    enum DeviceActivation stage;
    oserr_t               status;
    oserr_t               cleanup;

    // If setup state exists, requests must come from the process that prepared
    // the MAC. This also applies to retries after cleanup fails.
    if (principal == UUID_INVALID || (state != NULL && state->Principal != principal)) {
        return OS_EPERMISSIONS;
    }

    if (request == OSIOCTLREQUEST_ETHERNET_PHY_RESET) {
        if (child->Bus->Runtime != NULL && child->Bus->Runtime->Quiescing) {
            return OS_EBUSY;
        }

        if (buffer == NULL || length != sizeof(uint32_t) || *(const uint32_t*)buffer != 0) {
            return OS_EINVALPARAMS;
        }
        
        if (state == NULL || !state->Ready) {
            return OS_EBUSY;
        }

        status = __EthernetResetPhy(state);
        if (status != OS_EOK) {
            // A failed pulse leaves the PHY state uncertain. Block new DMA and
            // IRQ requests, but keep the saved registers for stop and retry.
            state->Ready = 0;
            child->Resources.Prepared = 0;
        }
        return status;
    }

    if (buffer == NULL || length != sizeof(stage)) {
        return OS_EINVALPARAMS;
    }

    stage = *(const enum DeviceActivation*)buffer;
    // Stop remains available while the bus is shutting down, so clients can
    // release their setup. New preparation and start requests are blocked below.
    if (stage == DeviceStop) {
        status = Rp1EthernetRelease(child);
        if (status == OS_EOK) {
            child->Resources.Prepared = 0;
        }
        return status;
    }

    if (child->Bus->Runtime != NULL && child->Bus->Runtime->Quiescing) {
        return OS_EBUSY;
    }

    // The driver starts the MAC engine. DeviceStart only checks that clock
    // setup has completed. PHY reset requires a separate request.
    if (stage == DeviceStart) {
        return state != NULL && state->Ready ? OS_EOK : OS_EBUSY;
    }

    if (stage != DevicePrepare) {
        return OS_EINVALPARAMS;
    }

    if (state != NULL && state->Ready) {
        return OS_EOK;
    }

    // Complete cleanup from an earlier setup attempt before starting again.
    // The saved register values must stay available until cleanup succeeds.
    status = Rp1EthernetRelease(child);
    if (status != OS_EOK) {
        return status;
    }

    // The register sequence and clock assumptions have only been defined for C0.
    if (child->Bus->Parent->Header->Revision != RP1_REVISION_C0) {
        return OS_ENOTSUPPORTED;
    }

    state = calloc(1, sizeof(struct Rp1Ethernet));
    if (state == NULL) {
        return OS_EOOM;
    }

    state->Principal = principal;
    // Save the state before setup so Rp1EthernetRelease can undo partial work.
    // Keep it here if cleanup fails, so a later request can retry.
    child->Ethernet = state;

    // Validate firmware first, map dependencies next, then change clocks.
    // PHY reset is a separate driver request after preparation and before MDIO.
    status = FdtEthernetConfiguration(child->Bus->Host->Tree, child->Firmware.NodeOffset, &state->Config);
    if (status != OS_EOK) {
        cleanup = Rp1EthernetRelease(child);
        return cleanup == OS_EOK ? status : cleanup;
    }

    status = __EthernetDependencies(child, state);
    if (status != OS_EOK) {
        cleanup = Rp1EthernetRelease(child);
        return cleanup == OS_EOK ? status : cleanup;
    }

    status = __EthernetClocks(state);
    if (status != OS_EOK) {
        cleanup = Rp1EthernetRelease(child);
        return cleanup == OS_EOK ? status : cleanup;
    }

    // Grant DMA and interrupt access only after all preparation steps succeed.
    state->Ready = 1;
    child->Resources.Prepared = 1;
    return OS_EOK;
}
