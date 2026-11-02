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

//#define __TRACE

#include <bus/pci/host-private.h>
#include <bus/pci/registers.h>
#include <bus/pci/hosts/broadcom/bcm.h>
#include <bus/pci/hosts/broadcom/registers.h>
#include <ddk/utils.h>
#include <stdlib.h>
#include <core/io.h>

/**
 * @brief Keep each fixed electrical-interface setting beside its register number.
 *
 * The PHY is the electrical interface that sends and receives PCI Express
 * signals. Its registers are selected through a setup interface called MDIO.
 * These numbers select PHY registers, rather than byte offsets in CPU memory.
 */
struct __Bcm2712PhySetting {
    uint8_t  Register;
    uint16_t Value;
};

/**
 * @brief Keep one register mapping for all hosts using the same reset hardware.
 *
 * The kernel rejects a second registration of an overlapping physical range,
 * even from this service. References counts hosts using this mapping. Each
 * host keeps its own signal number and writes only that signal's bit.
 * Acquired records whether register access is available. Removing blocks new
 * users while cleanup is unfinished. g_bcm2712ResetLock protects the list and
 * reference counts.
 */
struct __Bcm2712ResetMapping {
    element_t    Header;
    DeviceIo_t   Io;
    unsigned int References;
    int          Acquired;
    int          Removing;
};

static list_t g_bcm2712ResetMappings = LIST_INIT;
static mtx_t  g_bcm2712ResetLock = MUTEX_INIT(mtx_plain);

static oserr_t
__Bcm2712AcquireResetMapping(
    _In_  const struct FdtPciDependencies* dependencies,
    _Out_ struct __Bcm2712ResetMapping**   result)
{
    struct __Bcm2712ResetMapping* resetMapping;
    oserr_t                       status;
    int                           lockStatus;

    TRACE("__Bcm2712AcquireResetMapping(base=0x%llx, id=%u)",
        (unsigned long long)dependencies->BridgeResetBase, dependencies->BridgeResetId);

    lockStatus = mtx_lock(&g_bcm2712ResetLock);
    if (lockStatus != thrd_success) {
        ERROR("__Bcm2712AcquireResetMapping failed to lock mappings: %i", lockStatus);
    }
    foreach (element, &g_bcm2712ResetMappings) {
        resetMapping = element->value;
        
        if (resetMapping->Io.Access.Memory.PhysicalBase != dependencies->BridgeResetBase ||
            resetMapping->Io.Access.Memory.Length != dependencies->BridgeResetLength) {
            continue;
        }
        
        if (resetMapping->Removing) {
            WARNING("__Bcm2712AcquireResetMapping: reset mapping at 0x%llx has unfinished cleanup",
                (unsigned long long)dependencies->BridgeResetBase);
            lockStatus = mtx_unlock(&g_bcm2712ResetLock);
            if (lockStatus != thrd_success) {
                ERROR("__Bcm2712AcquireResetMapping: failed to unlock mappings (%u)",
                    (unsigned int)lockStatus);
            }
            return OS_EBUSY;
        }

        if (resetMapping->References == UINT_MAX) {
            ERROR("__Bcm2712AcquireResetMapping reference count exhausted");
            lockStatus = mtx_unlock(&g_bcm2712ResetLock);
            if (lockStatus != thrd_success) {
                ERROR("__Bcm2712AcquireResetMapping failed to unlock mappings: %i", lockStatus);
            }
            return OS_EOVERFLOW;
        }
        
        resetMapping->References++;
        *result = resetMapping;
        
        TRACE("__Bcm2712AcquireResetMapping reused mapping, references=%u", resetMapping->References);
        lockStatus = mtx_unlock(&g_bcm2712ResetLock);
        if (lockStatus != thrd_success) {
            ERROR("__Bcm2712AcquireResetMapping failed to unlock mappings: %i", lockStatus);
        }
        return OS_EOK;
    }

    resetMapping = calloc(1, sizeof(struct __Bcm2712ResetMapping));
    if (resetMapping == NULL) {
        ERROR("__Bcm2712AcquireResetMapping allocation failed");
        lockStatus = mtx_unlock(&g_bcm2712ResetLock);
        if (lockStatus != thrd_success) {
            ERROR("__Bcm2712AcquireResetMapping failed to unlock mappings: %i", lockStatus);
        }
        return OS_EOOM;
    }

    status = BcmPciMapRegisters(
        &resetMapping->Io,
        &resetMapping->Acquired,
        dependencies->BridgeResetBase,
        dependencies->BridgeResetLength
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712AcquireResetMapping: failed to map reset registers at 0x%llx (%u)",
            (unsigned long long)dependencies->BridgeResetBase, status);
        if (resetMapping->Io.Type == DeviceIoInvalid) {
            free(resetMapping);
            lockStatus = mtx_unlock(&g_bcm2712ResetLock);
            if (lockStatus != thrd_success) {
                ERROR("__Bcm2712AcquireResetMapping failed to unlock mappings: %i", lockStatus);
            }
            return status;
        }
    }

    resetMapping->References = 1;
    resetMapping->Removing = status != OS_EOK;
    ELEMENT_INIT(&resetMapping->Header, 0, resetMapping);
    list_append(&g_bcm2712ResetMappings, &resetMapping->Header);

    *result = resetMapping;

    lockStatus = mtx_unlock(&g_bcm2712ResetLock);
    if (lockStatus != thrd_success) {
        ERROR("__Bcm2712AcquireResetMapping failed to unlock mappings: %i", lockStatus);
    }
    return status;
}

static oserr_t
__Bcm2712ReleaseResetMapping(
    _In_ struct __Bcm2712ResetMapping* mapping)
{
    int     lockStatus;
    oserr_t status = OS_EOK;

    TRACE("__Bcm2712ReleaseResetMapping(mapping=%p)", mapping);

    lockStatus = mtx_lock(&g_bcm2712ResetLock);
    if (lockStatus != thrd_success) {
        ERROR("__Bcm2712ReleaseResetMapping failed to lock mappings: %i", lockStatus);
    }

    if (mapping->References == 1) {
        mapping->Removing = 1;
        status = DmIoRelease(&mapping->Io, &mapping->Acquired);
        if (status != OS_EOK) {
            ERROR("__Bcm2712ReleaseResetMapping: failed to release reset registers at 0x%llx (%u)",
                (unsigned long long)mapping->Io.Access.Memory.PhysicalBase, status);
            goto unlock;
        }
        list_remove(&g_bcm2712ResetMappings, &mapping->Header);
        free(mapping);
    } else {
        mapping->References--;
    }

unlock:
    lockStatus = mtx_unlock(&g_bcm2712ResetLock);
    if (lockStatus != thrd_success) {
        ERROR("__Bcm2712ReleaseResetMapping failed to unlock mappings: %i", lockStatus);
    }
    return status;
}

/**
 * @brief Check that the driver can use the Pi 5 settings described by firmware.
 *
 * Run before writing to hardware. Pi 5 needs a separate controller to reset the
 * PCI host and supports several address ranges for device access to the system.
 * Reject ranges whose addresses are too large, overlap, or need rounding up
 * beyond the memory or registers declared by firmware.
 */
static oserr_t
__Bcm2712Validate(
    _In_  const struct FdtPciHost* firmware,
    _Out_ unsigned int*            dmaOrder)
{
    struct FdtPciDependencies  dependencies;
    const struct FdtPciWindow* window;
    const struct FdtPciWindow* previous;
    oserr_t                    status;
    uint32_t                   index;
    uint32_t                   other;

    TRACE("__Bcm2712Validate(segment=%u, windows=%u)",
        firmware->Segment, firmware->DmaWindowCount);

    // Pi 4 uses this output for one shared size setting. Pi 5 gives each
    // address mapping its own size, so it does not use this output.
    *dmaOrder = 0;

    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Validate FdtResolvePciDependencies failed: %u", status);
        return status;
    }

    // Both host reset and resistor-calibration hardware (RESCAL) are required.
    // The firmware helper checked controller types and reset IDs. The RESCAL
    // register range must also include the register reporting completion.
    if (dependencies.BridgeResetController == 0 ||
        dependencies.ResetLength < BCM_PCIE_RESCAL_MIN_LENGTH) {
        WARNING("__Bcm2712Validate requires bridge reset and resistor calibration resources");
        return OS_ENOTSUPPORTED;
    }

    // Reject reset blocks that this build cannot map as one complete region.
    // Checking the base first also makes the following subtraction safe.
    if (dependencies.BridgeResetBase > UINTPTR_MAX ||
        dependencies.BridgeResetLength - 1 > UINTPTR_MAX - dependencies.BridgeResetBase) {
        WARNING("__Bcm2712Validate bridge reset range is not CPU-addressable");
        return OS_ENOTSUPPORTED;
    }

    if (dependencies.ResetBase > UINTPTR_MAX ||
        dependencies.ResetLength - 1 > UINTPTR_MAX - dependencies.ResetBase) {
        WARNING("__Bcm2712Validate calibration range is not CPU-addressable");
        return OS_ENOTSUPPORTED;
    }

    // Hardware has ten address-mapping slots. At least one range is needed
    // to describe where device reads and writes can reach the system.
    if (firmware->DmaWindowCount == 0 || firmware->DmaWindowCount > BCM2712_INBOUND_COUNT) {
        WARNING("__Bcm2712Validate requires one to ten inbound windows");
        return OS_ENOTSUPPORTED;
    }

    // Zero keeps the hardware defaults. A requested speed may be no faster
    // than PCI Express generation 3 (Gen3). The connection may use one, two,
    // or four lanes, the parallel signal paths that carry data.
    if (firmware->Link.MaxSpeed > BCM_PCIE_LINK_SPEED_GEN3) {
        WARNING("__Bcm2712Validate link speed exceeds Gen3");
        return OS_ENOTSUPPORTED;
    }
    
    if (firmware->Link.Lanes != 0) {
        if (firmware->Link.Lanes != BCM_PCIE_LINK_LANES_X1 &&
            firmware->Link.Lanes != BCM_PCIE_LINK_LANES_X2 &&
            firmware->Link.Lanes != BCM_PCIE_LINK_LANES_X4) {
            WARNING("__Bcm2712Validate unsupported link lane count");
            return OS_ENOTSUPPORTED;
        }
    }

    for (index = 0; index < firmware->DmaWindowCount; index++) {
        window = &firmware->DmaWindows[index];

        // These address mappings support memory reads and writes. PCI I/O
        // ports use a separate address space and are not supported here.
        if (window->Space != BCM_PCIE_DT_MEMORY32 && window->Space != BCM_PCIE_DT_MEMORY64) {
            WARNING("__Bcm2712Validate inbound window is not memory space");
            return OS_ENOTSUPPORTED;
        }

        // A device-to-system range must have a power-of-two size from 4 KiB
        // to 64 GiB. Rounding up would allow access beyond the declared range.
        if (window->Length < BCM_PCIE_INBOUND_MIN_SIZE ||
            window->Length > BCM_PCIE_INBOUND_MAX_SIZE ||
            (window->Length & (window->Length - 1))) {
            WARNING("__Bcm2712Validate inbound window length must be a power of two from 4 KiB to 64 GiB");
            return OS_ENOTSUPPORTED;
        }

        // The PCI starting address must be a multiple of the range size.
        // The CPU physical destination must be a multiple of 4 KiB because
        // hardware does not store its lower 12 address bits.
        if ((window->BusBase & (window->Length - 1)) ||
            (window->PhysicalBase & BCM2712_INBOUND_PHYSICAL_ALIGN_MASK)) {
            WARNING("__Bcm2712Validate inbound PCI or CPU address is misaligned");
            return OS_ENOTSUPPORTED;
        }

        // Both complete ranges must fit: PCI addresses cannot wrap, and the
        // system connection accepts only the low 40 physical address bits.
        if (window->Length - 1 > UINT64_MAX - window->BusBase ||
            window->PhysicalBase >= BCM2712_SYSTEM_ADDRESS_LIMIT ||
            window->Length > BCM2712_SYSTEM_ADDRESS_LIMIT - window->PhysicalBase) {
            WARNING("__Bcm2712Validate inbound address range wraps or exceeds 40-bit CPU addressing");
            return OS_ENOTSUPPORTED;
        }
        for (other = 0; other < index; other++) {
            previous = &firmware->DmaWindows[other];

            // A device address must select only one destination. Both ranges
            // have already passed their overflow checks, including the ends.
            if (window->BusBase <= previous->BusBase + previous->Length - 1 &&
                previous->BusBase <= window->BusBase + window->Length - 1) {
                WARNING("__Bcm2712Validate inbound PCI windows overlap");
                return OS_ENOTSUPPORTED;
            }
        }
    }
    return OS_EOK;
}

/**
 * @brief Change this host's reset signal without resetting other hosts.
 *
 * The firmware reset ID selects a group of 32 signals and a bit within it.
 * Check repeatedly, up to the retry limit, until the status shows the requested
 * state, then pause to let the change settle. The caller must keep the reset
 * registers mapped and acquired throughout this operation.
 */
static oserr_t
__Bcm2712BridgeReset(
    _In_ struct BcmPciHost* controller,
    _In_ int                assert)
{
    size_t   bank = (controller->BridgeResetId / BCM2712_RESET_LINES_PER_BANK) *
            BCM2712_RESET_BANK_STRIDE;
    uint32_t bit = 1U << (controller->BridgeResetId % BCM2712_RESET_LINES_PER_BANK);
    uint32_t value;
    oserr_t  status;

    TRACE("__Bcm2712BridgeReset(id=%u, assert=%i)", controller->BridgeResetId, assert);

    // SET and CLEAR act on bits written as one. Write only this host's bit:
    // reading and rewriting a bank could accidentally reset another live host.
    status = WriteDeviceIo(
        &controller->BridgeReset->Io,
        bank + (assert ? BCM2712_RESET_SET : BCM2712_RESET_CLEAR),
        bit,
        sizeof(uint32_t)
    );
    
    if (status != OS_EOK) {
        ERROR("__Bcm2712BridgeReset failed to write reset %u (assert=%i): %u",
            controller->BridgeResetId, assert, status);
        return status;
    }

    for (unsigned int attempt = 0; attempt < BCM2712_REGISTER_POLL_ATTEMPTS; attempt++) {
        // Reading STATUS waits for the earlier write to reach the controller
        // and reports whether reset is active. SET/CLEAR cannot report that state.
        value = (uint32_t)ReadDeviceIo(&controller->BridgeReset->Io,
                bank + BCM2712_RESET_STATUS, sizeof(uint32_t));

        // An all-ones read can mean the access failed. Require a valid read
        // and compare only this host's bit, since other hosts may be in reset.
        if (value != BCM_PCIE_READ_FAILED && !!(value & bit) == !!assert) {
            BcmPciDelay(BCM_PCIE_RESET_SETTLE_MS);
            return OS_EOK;
        }
        BcmPciDelay(BCM2712_REGISTER_POLL_MS);
    }
    ERROR("__Bcm2712BridgeReset reset %u did not reach assert=%i, read back 0x%x",
        controller->BridgeResetId, assert, value);
    return OS_EUNKNOWN;
}

/**
 * @brief Write one electrical-interface register and wait for hardware to accept it.
 *
 * The PHY uses a separate setup interface named MDIO. Select its register,
 * start the write, and wait for hardware to clear the busy bit. Each setting
 * must complete before another starts. Limit the wait so an unresponsive PHY
 * cannot hang boot.
 */
static oserr_t
__Bcm2712MdioWrite(
    _In_ PciHost_t* bus,
    _In_ uint8_t    reg,
    _In_ uint16_t   value)
{
    oserr_t status;

    TRACE("__Bcm2712MdioWrite(reg=0x%x, value=0x%x)", reg, value);

    // Select PHY port zero, the requested register, and the write command.
    // Port zero and write both encode as zero in the address/control word.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_MDIO_ADDRESS,
        BCM2712_MDIO_PORT0 | BCM2712_MDIO_COMMAND_WRITE | reg,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712MdioWrite WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM2712_MDIO_ADDRESS), status);
        return status;
    }

    // Read back the selection so it reaches the controller before write data
    // starts the transfer. The returned selection is not completion status.
    (void)ReadDeviceIo(&bus->IoSpace, BCM2712_MDIO_ADDRESS, sizeof(uint32_t));

    // Bit 31 starts the transfer. The lower value is the complete PHY setting.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_MDIO_WRITE,
        BCM2712_MDIO_BUSY | value,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712MdioWrite WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM2712_MDIO_WRITE), status);
        return status;
    }

    for (unsigned int attempt = 0; attempt < BCM2712_REGISTER_POLL_ATTEMPTS; attempt++) {
        // Hardware clears BUSY after accepting the value. An all-ones read
        // keeps BUSY set, so a missing controller cannot look like a successful write.
        if (!(ReadDeviceIo(&bus->IoSpace, BCM2712_MDIO_WRITE, sizeof(uint32_t)) &
                BCM2712_MDIO_BUSY)) {
            return OS_EOK;
        }
        BcmPciDelay(BCM2712_REGISTER_POLL_MS);
    }
    ERROR("__Bcm2712MdioWrite timed out writing PHY register 0x%x", reg);
    return OS_EUNKNOWN;
}

/**
 * @brief Set up the Pi 5 connection clock and apply fixes for reads and memory requests.
 *
 * The board oscillator runs at 54 MHz. The PHY needs the reference driver's
 * complete fixed settings to produce the PCIe clock. The remaining writes
 * prevent missing devices or faulty priority handling from stalling discovery.
 */
static oserr_t
__Bcm2712Setup(
    _In_ PciHost_t* bus)
{
    static const struct __Bcm2712PhySetting settings[] = {
        { BCM2712_PHY_PLL_REG16, BCM2712_PHY_PLL_54MHZ_REG16 },
        { BCM2712_PHY_PLL_REG17, BCM2712_PHY_PLL_54MHZ_REG17 },
        { BCM2712_PHY_PLL_REG18, BCM2712_PHY_PLL_54MHZ_REG18 },
        { BCM2712_PHY_PLL_REG19, BCM2712_PHY_PLL_54MHZ_REG19 },
        { BCM2712_PHY_PLL_REG1B, BCM2712_PHY_PLL_54MHZ_REG1B },
        { BCM2712_PHY_PLL_REG1C, BCM2712_PHY_PLL_54MHZ_REG1C },
        { BCM2712_PHY_PLL_REG1E, BCM2712_PHY_PLL_54MHZ_REG1E }
    };
    uint32_t value;
    oserr_t  status;

    TRACE("__Bcm2712Setup(segment=%u)", bus->Identification.Segment);

    // Select the PHY's clock-generation register block. Subsequent register
    // numbers refer to that block, not to the controller's CPU-visible offsets.
    status = __Bcm2712MdioWrite(bus, BCM2712_PHY_BLOCK_SELECT, BCM2712_PHY_PLL_BLOCK);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup: segment %u failed to select PHY block 0x%x (%u)",
            bus->Identification.Segment, (unsigned int)BCM2712_PHY_PLL_BLOCK, status);
        return status;
    }

    // Apply every 54 MHz setting in the reference order. The reference does
    // not document their individual bit fields, so keep each full value intact.
    // The helper waits for hardware to accept each write before the next setting.
    for (unsigned int index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
        status = __Bcm2712MdioWrite(bus, settings[index].Register, settings[index].Value);
        if (status != OS_EOK) {
            ERROR("__Bcm2712Setup: segment %u PHY setting %u register 0x%x failed (%u)",
                bus->Identification.Segment, index, settings[index].Register, status);
            return status;
        }
    }
    BcmPciDelay(BCM_PCIE_PHY_SETTLE_MS);

    // Tell the internal power-state timers that each 54 MHz clock period is
    // about 18 ns. Leaving the reset default would give incorrect transition times.
    status = BcmPciUpdate(
        bus,
        BCM2712_PHY_TIMERS,
        BCM2712_PHY_PM_CLOCK_PERIOD_MASK,
        BCM2712_PHY_PM_CLOCK_54MHZ_NS
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_PHY_TIMERS), status);
        return status;
    }

    // Suppress both general and missing-address error replies on the system
    // bus. Probing an absent PCI device should return data, not fault the CPU.
    status = BcmPciUpdate(
        bus,
        BCM2712_UBUS_CONTROL,
        BCM2712_UBUS_ERROR_DISABLE_MASK,
        BCM2712_UBUS_ERROR_DISABLE_MASK
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_UBUS_CONTROL), status);
        return status;
    }

    // Return all ones on a failed read: the generic scanner recognizes an
    // all-ones vendor ID as an absent device and continues scanning.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_READ_ERROR,
        BCM2712_READ_ERROR_VALUE,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM2712_READ_ERROR), status);
        return status;
    }

    // Give a slow device roughly 250 ms before the system-bus request expires.
    // These ticks come from the controller clock, not the CPU clock.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_UBUS_TIMEOUT,
        BCM2712_UBUS_TIMEOUT_250MS,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM2712_UBUS_TIMEOUT), status);
        return status;
    }

    // A device may ask us to retry a configuration read while it is starting.
    // Stop retrying after about 240 ms, before the system-bus request times out.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_RETRY_TIMEOUT,
        BCM2712_RETRY_TIMEOUT_240MS,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM2712_RETRY_TIMEOUT), status);
        return status;
    }

    // Priority decides which memory request is handled first. Stop forwarding
    // queued priorities through the faulty hardware path and enable the fixes
    // for the priority table and when its values are updated.
    status = BcmPciUpdate(
        bus,
        BCM2712_AXI_CONTROL,
        BCM2712_AXI_PRIORITY_SETUP_MASK,
        BCM2712_AXI_PRIORITY_FIXES
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_AXI_CONTROL), status);
        return status;
    }

    // Do not let manufacturer-specific device messages change memory request
    // priorities while finding devices. That hardware mechanism has known faults.
    status = BcmPciUpdate(
        bus,
        BCM2712_MISC_CONTROL1,
        BCM2712_MISC_VDM_PRIORITY_ENABLE,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Setup BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_MISC_CONTROL1), status);
        return status;
    }

    // Read back the timing-fix bit. Older chips and single-lane ports leave
    // it clear because they do not implement that correction.
    value = (uint32_t)ReadDeviceIo(&bus->IoSpace, BCM2712_AXI_CONTROL, sizeof(uint32_t));
    if (!(value & BCM2712_AXI_PRIORITY_TIMING_FIX)) {
        // On those ports, limiting requests awaiting memory replies to 15
        // reduces the incorrect priority assignments that the missing fix avoids.
        status = BcmPciUpdate(
            bus,
            BCM2712_AXI_CONTROL,
            BCM2712_AXI_OUTSTANDING_MASK,
            BCM2712_AXI_OUTSTANDING_FALLBACK
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Setup: segment %u failed to set fallback at register 0x%x (%u)",
                bus->Identification.Segment, (unsigned int)BCM2712_AXI_CONTROL, status);
            return status;
        }
    }
    return OS_EOK;
}

/**
 * @brief Map Pi 5 reset controls and prepare the controller for address setup.
 *
 * Call after Validate succeeds and the main controller registers are mapped.
 * Let the host run for calibration, then briefly reset it. Keep the connected
 * device in reset while later setup stages install address mappings. Save the
 * shared reset mapping immediately so Stop can release this host's reference
 * if setup fails, while other hosts can continue using those registers.
 */
static oserr_t
__Bcm2712Prepare(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware)
{
    struct BcmPciHost*        controller = bus->OpContext;
    struct FdtPciDependencies dependencies;
    oserr_t                   status;
    uint32_t                  value;

    TRACE("__Bcm2712Prepare(segment=%u)", firmware->Segment);

    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare FdtResolvePciDependencies failed: %u", status);
        return status;
    }

    status = __Bcm2712AcquireResetMapping(&dependencies, &controller->BridgeReset);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare __Bcm2712AcquireResetMapping failed: %u", status);
        return status;
    }
    
    controller->BridgeResetMapped = 1;
    controller->BridgeResetId = dependencies.BridgeResetId;

    // The resistor-calibration block cannot be used with the bridge held in
    // reset. The reset helper checks completion and includes a settling delay.
    status = __Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare failed to release reset before calibration: %u", status);
        return OS_EUNKNOWN;
    }

    // Pi 5 hosts share calibration. Reuse an already-completed result so
    // initializing this host does not disturb another host that is already in use.
    status = BcmPciRescal(controller, &dependencies, 1);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare calibration failed: %u", status);
        return OS_EUNKNOWN;
    }

    // Reset this bridge's internal state after calibration, leaving the
    // shared calibration result intact. The helper waits until reset is active.
    status = __Bcm2712BridgeReset(controller, BCM2712_RESET_ASSERTED);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare failed to assert bridge reset: %u", status);
        return OS_EUNKNOWN;
    }

    // Bring the bridge back so its own configuration registers can be used.
    status = __Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED);
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare failed to release bridge reset after calibration: %u", status);
        return OS_EUNKNOWN;
    }

    // Clear the release bit to hold the connected device in reset. Pi 5 uses
    // this register instead of Pi 4's SW_INIT register for device reset.
    status = BcmPciUpdate(
        bus,
        BCM2712_PCIE_CONTROL,
        BCM2712_PERST_RELEASE,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_PCIE_CONTROL), status);
        return status;
    }

    // Power up the electrical interface and clear clock-request/sleep modes
    // inherited from firmware before applying the known PHY setup below.
    status = BcmPciUpdate(
        bus,
        BCM2712_HARD_DEBUG,
        BCM_PCIE_DEBUG_SERDES_POWERDOWN | BCM2712_DEBUG_CLOCK_MASK,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_HARD_DEBUG), status);
        return status;
    }

    BcmPciDelay(BCM_PCIE_PHY_SETTLE_MS);

    // Allow devices to access system memory, set how read replies are split,
    // and enable handling for unsupported reads. Select memory transfers of
    // up to 512 bytes for Pi 5.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_MISC_CONTROL,
        BCM_PCIE_MISC_BURST_MASK | BCM_PCIE_MISC_MEMORY_ENABLES,
        BCM2712_MISC_BURST_512_BYTES | BCM_PCIE_MISC_MEMORY_ENABLES
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_MISC_CONTROL), status);
        return status;
    }

    // Keep PCI's byte order for data entering the system through BAR2:
    // little-endian stores the least significant byte first. Keep other settings.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_VENDOR_CONTROL,
        BCM_PCIE_VENDOR_BAR2_ENDIAN_MASK,
        BCM_PCIE_VENDOR_BAR2_LITTLE_ENDIAN
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Prepare BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_VENDOR_CONTROL), status);
        return status;
    }

    // Verify that this is the CPU-side root port. An all-ones failed read
    // would also have the root bit set, so reject it explicitly before setup.
    value = (uint32_t)ReadDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_LINK_STATUS,
        sizeof(uint32_t)
    );
    if (value == BCM_PCIE_READ_FAILED || !(value & BCM_PCIE_STATUS_ROOT_PORT)) {
        ERROR("__Bcm2712Prepare invalid root-port status 0x%x", value);
        return OS_ENOTSUPPORTED;
    }
    return __Bcm2712Setup(bus);
}

/**
 * @brief Find the lower 32-bit register for a device-to-system mapping slot.
 *
 * The index must be below BCM2712_INBOUND_COUNT. Slots are numbered from zero.
 * The first three and the remaining seven use separate register blocks.
 * Select the PCI address registers or the CPU physical destination registers
 * according to remap. Treating all ten as one continuous block would write
 * into unrelated registers, including interrupt settings.
 */
static size_t
__Bcm2712InboundRegister(
    _In_ unsigned int index,
    _In_ int          remap)
{
    if (index < BCM2712_INBOUND_FIRST_BLOCK_COUNT) {
        return (remap ? BCM2712_INBOUND_REMAP1 : BCM_PCIE_INBOUND_BAR1) +
                index * BCM2712_INBOUND_REGISTER_STRIDE;
    }
    return (remap ? BCM2712_INBOUND_REMAP4 : BCM2712_INBOUND_BAR4) +
            (index - BCM2712_INBOUND_FIRST_BLOCK_COUNT) * BCM2712_INBOUND_REGISTER_STRIDE;
}

/**
 * @brief Install exactly the device-to-system mappings declared by firmware.
 *
 * Call after Validate accepts all ranges and Prepare holds the device in
 * reset. Pi 5 describes RAM, other devices' registers, and the interrupt
 * destination in the Broadcom MIP controller separately. Each range has a PCI
 * address, CPU physical address, and size. BcmPciDmaAddress only accepts buffers
 * in ranges that the firmware reader identified as RAM.
 */
static oserr_t
__Bcm2712Inbound(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware,
    _In_ unsigned int             dmaOrder)
{
    const struct FdtPciWindow* window;
    unsigned int               index;
    unsigned int               order;
    uint32_t                   sizeCode;
    size_t                     bar;
    size_t                     remap;
    oserr_t                    status;

    TRACE("__Bcm2712Inbound(segment=%u, windows=%u)",
        firmware->Segment, firmware->DmaWindowCount);

    // Pi 4 uses this shared size parameter. Pi 5 sets a size for each mapping.
    (void)dmaOrder;
    for (index = 0; index < BCM2712_INBOUND_COUNT; index++) {
        bar = __Bcm2712InboundRegister(index, 0);
        remap = __Bcm2712InboundRegister(index, 1);

        // A zero size code disables this address mapping. Clear unused slots
        // too, so no extra address ranges remain accessible from firmware setup.
        status = WriteDeviceIo(
            &bus->IoSpace,
            bar,
            BCM_PCIE_INBOUND_DISABLED,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(bar), status);
            return status;
        }

        // Also disable access to the CPU physical destination. Old upper
        // address bits cannot grant access while both enable settings are off.
        status = WriteDeviceIo(
            &bus->IoSpace,
            remap,
            BCM_PCIE_DISABLED,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(remap), status);
            return status;
        }
    }

    // Disable the older, separate message-signaled interrupt (MSI) destination.
    // Pi 5 sends interrupts by writing to its MIP controller through one of the
    // ordinary device-to-system address mappings installed below.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_MSI_BAR,
        BCM_PCIE_MSI_DISABLED,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_MSI_BAR), status);
        return status;
    }

    for (index = 0; index < firmware->DmaWindowCount; index++) {
        window = &firmware->DmaWindows[index];
        bar = __Bcm2712InboundRegister(index, 0);
        remap = __Bcm2712InboundRegister(index, 1);

        // Validate checked that the length is a power of two. The order is
        // its exponent: 4 KiB = 2^12 gives 12. Sizes 4..32 KiB use codes
        // 0x1c..0x1f. Sizes from 64 KiB use the exponent minus 15.
        order = BCM_PCIE_INBOUND_MIN_ORDER;
        while ((1ULL << order) < window->Length) {
            order++;
        }
        if (order < BCM_PCIE_INBOUND_LARGE_MIN_ORDER) {
            sizeCode = order - BCM_PCIE_INBOUND_MIN_ORDER + BCM_PCIE_INBOUND_SMALL_SIZE_BASE;
        } else {
            sizeCode = order - BCM_PCIE_INBOUND_LARGE_SIZE_BIAS;
        }

        // Install the upper PCI address half while this slot is still disabled.
        // Dropping this half would redirect high addresses into the low 4 GiB.
        status = WriteDeviceIo(
            &bus->IoSpace,
            bar + BCM_PCIE_REGISTER_HIGH_OFFSET,
            window->BusBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(bar + BCM_PCIE_REGISTER_HIGH_OFFSET), status);
            return status;
        }

        // The CPU physical address has its own upper half. It may differ
        // from the PCI address the device uses for the same mapping.
        status = WriteDeviceIo(
            &bus->IoSpace,
            remap + BCM_PCIE_REGISTER_HIGH_OFFSET,
            window->PhysicalBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(remap + BCM_PCIE_REGISTER_HIGH_OFFSET), status);
            return status;
        }

        // Set the lower physical address and allow access to that destination.
        // Validation proved its low 12 address bits are zero, leaving bit 0 free.
        status = WriteDeviceIo(
            &bus->IoSpace,
            remap,
            (uint32_t)window->PhysicalBase | BCM2712_INBOUND_REMAP_ENABLE,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(remap), status);
            return status;
        }

        // Enable the mapping last, after both halves of the destination address
        // are set. The starting address is a multiple of the range size, leaving
        // the lower bits free for the size code.
        status = WriteDeviceIo(
            &bus->IoSpace,
            bar,
            (uint32_t)window->BusBase | sizeCode,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Inbound WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(bar), status);
            return status;
        }
    }
    return OS_EOK;
}

/**
 * @brief Set Pi 5 connection settings and let the device start after address setup.
 *
 * Call after the address mappings are installed. Honor firmware speed and lane
 * limits. Keep clocks running and link sleep disabled until drivers can
 * coordinate power management. Wait for reset recovery before the shared
 * initializer checks whether the connection is ready.
 */
static oserr_t
__Bcm2712Start(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware)
{
    uint32_t buses = firmware->BusStart |
            ((uint32_t)(firmware->BusStart + 1) << BCM_PCIE_SECONDARY_BUS_SHIFT) |
            ((uint32_t)firmware->BusEnd << BCM_PCIE_SUBORDINATE_BUS_SHIFT);
    uint16_t linkControl;
    oserr_t  status;

    TRACE("__Bcm2712Start(segment=%u, speed=%u, lanes=%u)",
        firmware->Segment, firmware->Link.MaxSpeed, firmware->Link.Lanes);

    if (firmware->Link.MaxSpeed != 0) {
        // Report the maximum speed set by firmware, so later code cannot
        // request a faster connection than the board supports.
        status = BcmPciUpdate(
            bus,
            BCM_PCIE_LINK_CAPABILITY,
            BCM_PCIE_LINK_CAP_SPEED_MASK,
            firmware->Link.MaxSpeed
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
                (unsigned)(BCM_PCIE_LINK_CAPABILITY), status);
            return status;
        }

        // Preserve other controls with a 16-bit read. The following word is
        // status and must not be included in the matching write.
        linkControl = (uint16_t)ReadDeviceIo(&bus->IoSpace,
                BCM_PCIE_LINK_CONTROL2, sizeof(uint16_t));

        // Request that same speed for the upcoming connection attempt.
        status = WriteDeviceIo(
            &bus->IoSpace,
            BCM_PCIE_LINK_CONTROL2,
            (linkControl & ~BCM_PCIE_LINK_CONTROL2_SPEED_MASK) | firmware->Link.MaxSpeed,
            sizeof(uint16_t)
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Start WriteDeviceIo register 0x%x failed: %u",
                (unsigned)(BCM_PCIE_LINK_CONTROL2), status);
            return status;
        }
    }

    if (firmware->Link.Lanes != 0) {
        // Replace the default lane count with the firmware value. The board
        // may connect fewer data lanes than the controller can support.
        status = BcmPciUpdate(
            bus,
            BCM_PCIE_LINK_CAPABILITY,
            BCM_PCIE_LINK_CAP_WIDTH_MASK,
            firmware->Link.Lanes << BCM_PCIE_LINK_CAP_WIDTH_SHIFT
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
                (unsigned)(BCM_PCIE_LINK_CAPABILITY), status);
            return status;
        }

        // The reference driver's lane-count setup also enables the PHY's P2
        // low-power control. Enabling the control does not put the connection
        // into that power state immediately.
        status = BcmPciUpdate(
            bus,
            BCM2712_PHY_CONTROL,
            BCM2712_PHY_P2_POWERDOWN_ENABLE,
            BCM2712_PHY_P2_POWERDOWN_ENABLE
        );
        if (status != OS_EOK) {
            ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
                (unsigned)(BCM2712_PHY_CONTROL), status);
            return status;
        }
    }

    // Spread-spectrum clocking varies clock frequency slightly to reduce
    // electrical interference. Pi 5 cannot use the older chip's setup for this,
    // so keep the current clock settings and report the unsupported request.
    if (firmware->Link.EnableSsc) {
        WARNING("BCM2712 PCIe segment %u: spread-spectrum clocking is unsupported", firmware->Segment);
    }

    // Identify the controller as a bridge connecting PCI buses. Device discovery
    // will then scan the bus behind it for RP1 or an external device.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_CLASS_CODE,
        BCM_PCIE_CLASS_CODE_MASK,
        BCM_PCIE_CLASS_CODE_BRIDGE
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_CLASS_CODE), status);
        return status;
    }

    // Stop reporting support for the L0s/L1 sleep modes. Device drivers have
    // not started, so they cannot yet coordinate power saving with the host.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_LINK_CAPABILITY,
        BCM_PCIE_LINK_CAP_ASPM_MASK,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_LINK_CAPABILITY), status);
        return status;
    }

    // Stop reporting deeper L1 sleep modes too. They require the device to
    // request its clock, but device discovery currently keeps the clock running.
    status = BcmPciUpdate(
        bus,
        BCM2712_ROOT_CAPABILITY,
        BCM2712_ROOT_CAP_L1SS_MASK,
        BCM2712_ROOT_CAP_L1SS_DISABLED
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_ROOT_CAPABILITY), status);
        return status;
    }

    // Read the current 16-bit control word, preserving unrelated link controls
    // and avoiding the adjacent status word during the update below.
    linkControl = (uint16_t)ReadDeviceIo(&bus->IoSpace,
            BCM_PCIE_LINK_CONTROL, sizeof(uint16_t));

    // Disable any L0s/L1 selection firmware left active. Removing advertised
    // support alone does not clear an already-selected power-saving mode.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_LINK_CONTROL,
        linkControl & ~BCM_PCIE_LINK_CONTROL_ASPM_MASK,
        sizeof(uint16_t)
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start WriteDeviceIo register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_LINK_CONTROL), status);
        return status;
    }

    // Force the reference clock output on and disable device-controlled clock
    // requests. A device must have a stable clock as it comes out of reset.
    status = BcmPciUpdate(
        bus,
        BCM2712_HARD_DEBUG,
        BCM2712_DEBUG_CLOCK_MASK,
        BCM2712_DEBUG_CLOCK_ALWAYS_ON
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_HARD_DEBUG), status);
        return status;
    }

    // Set the host's bus number, the bus directly behind it, and the highest
    // allowed bus number so configuration requests reach the right devices.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_BUS_NUMBERS,
        BCM_PCIE_BUS_NUMBERS_MASK,
        buses
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM_PCIE_BUS_NUMBERS), status);
        return status;
    }

    // Set the release bit to let the connected device start. Pi 4 clears a bit instead.
    status = BcmPciUpdate(
        bus,
        BCM2712_PCIE_CONTROL,
        BCM2712_PERST_RELEASE,
        BCM2712_PERST_RELEASE
    );
    if (status != OS_EOK) {
        ERROR("__Bcm2712Start BcmPciUpdate register 0x%x failed: %u",
            (unsigned)(BCM2712_PCIE_CONTROL), status);
        return status;
    }

    // Allow 100 ms for reset recovery even if link status changes immediately.
    // The shared code then checks for up to another 100 ms. Configuration
    // access becomes available only when the electrical connection and data
    // transfer are both ready.
    BcmPciDelay(BCM_PCIE_RESET_CONFIG_WAIT_MS);
    return OS_EOK;
}

/**
 * @brief Stop this Pi 5 connection and release its shared reset mapping.
 *
 * Prepare may have failed before acquiring the mapping. If it was acquired,
 * hold the connected device in reset but let the host controller run. Keeping
 * a Pi 5 host in reset can disturb calibration used by another active host.
 */
static oserr_t
__Bcm2712Stop(
    _In_ PciHost_t* bus)
{
    struct BcmPciHost* controller = bus->OpContext;
    oserr_t            status;
    oserr_t            resetStatus;

    TRACE("__Bcm2712Stop(segment=%u)", bus->Identification.Segment);

    if (controller->BridgeReset == NULL) {
        return OS_EOK;
    }

    if (controller->BridgeResetMapped) {
        // Hold the device in reset before releasing the host reset signal.
        // Attempt both writes. On failure, keep the mapping for another attempt.
        status = BcmPciUpdate(
            bus,
            BCM2712_PCIE_CONTROL,
            BCM2712_PERST_RELEASE,
            BCM_PCIE_DISABLED
        );
        resetStatus = __Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED);
        if (status != OS_EOK) {
            ERROR("__Bcm2712Stop: segment %u failed to hold device in reset at register 0x%x (%u)",
                bus->Identification.Segment, (unsigned int)BCM2712_PCIE_CONTROL, status);
            return status;
        }
        if (resetStatus != OS_EOK) {
            return resetStatus;
        }
        controller->BridgeResetMapped = 0;
    }

    status = __Bcm2712ReleaseResetMapping(controller->BridgeReset);
    if (status != OS_EOK) {
        return status;
    }
    controller->BridgeReset = NULL;
    return OS_EOK;
}

// Use the shared code to select devices and access their configuration.
// These callbacks supply Pi 5's separate reset controller, electrical setup,
// and ten device-to-system address mappings.
const struct BcmPciVariant g_bcm2712PciVariant = {
    .Type = FdtPciHostBcm2712,
    .RegisterLength = BCM_PCIE_REGISTER_LENGTH,
    .ConfigIndex = BCM_PCIE_CONFIG_INDEX,
    .ConfigData = BCM_PCIE_CONFIG_DATA,
    .Validate = __Bcm2712Validate,
    .Prepare = __Bcm2712Prepare,
    .ProgramInbound = __Bcm2712Inbound,
    .Start = __Bcm2712Start,
    .Stop = __Bcm2712Stop
};
