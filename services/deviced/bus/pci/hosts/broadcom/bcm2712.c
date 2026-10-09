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

#include <bus/pci/host-private.h>
#include <bus/pci/registers.h>
#include <bus/pci/hosts/broadcom/bcm.h>
#include <bus/pci/hosts/broadcom/registers.h>
#include <ddk/utils.h>
#include <stdlib.h>

/**
 * @brief Keep one register mapping for all hosts using the same reset hardware.
 *
 * The kernel rejects a second registration of an overlapping physical range,
 * even from this service. References counts hosts, not reset signals: each host
 * keeps its own signal number and writes only that signal's bit. The list and
 * reference counts are protected by g_bcm2712ResetLock.
 */
struct __Bcm2712ResetProvider {
    element_t    Header;
    DeviceIo_t   Io;
    unsigned int References;
};

static list_t g_bcm2712ResetProviders = LIST_INIT;
static mtx_t  g_bcm2712ResetLock = MUTEX_INIT(mtx_plain);

/**
 * @brief Retain an existing reset mapping instead of registering it twice.
 *
 * Physical address and length identify the actual registers, even if separate
 * firmware descriptions use different names. Hold a separate lock through
 * registration so simultaneous callers cannot both create the same mapping.
 * The PCI list lock cannot be used here because host teardown already holds it.
 * A failed first mapping leaves no record or reference for Stop to release.
 */
static oserr_t
__Bcm2712AcquireResetProvider(
    _In_  const struct FdtPciDependencies* dependencies,
    _Out_ struct __Bcm2712ResetProvider**  result)
{
    struct __Bcm2712ResetProvider* provider;
    oserr_t                        status;

    mtx_lock(&g_bcm2712ResetLock);
    foreach (element, &g_bcm2712ResetProviders) {
        provider = element->value;
        if (provider->Io.Access.Memory.PhysicalBase != dependencies->BridgeResetBase ||
            provider->Io.Access.Memory.Length != dependencies->BridgeResetLength) {
            continue;
        }
        if (provider->References == UINT_MAX) {
            mtx_unlock(&g_bcm2712ResetLock);
            return OS_EOVERFLOW;
        }
        
        provider->References++;
        *result = provider;
        
        mtx_unlock(&g_bcm2712ResetLock);
        return OS_EOK;
    }

    provider = calloc(1, sizeof(struct __Bcm2712ResetProvider));
    if (provider == NULL) {
        mtx_unlock(&g_bcm2712ResetLock);
        return OS_EOOM;
    }

    status = BcmPciMapRegisters(
        &provider->Io,
        dependencies->BridgeResetBase,
        dependencies->BridgeResetLength
    );
    if (status != OS_EOK) {
        free(provider);
        mtx_unlock(&g_bcm2712ResetLock);
        return status;
    }

    provider->References = 1;
    ELEMENT_INIT(&provider->Header, 0, provider);
    list_append(&g_bcm2712ResetProviders, &provider->Header);

    *result = provider;

    mtx_unlock(&g_bcm2712ResetLock);
    return OS_EOK;
}

/**
 * @brief Drop one host's reference without removing another host's registers.
 *
 * Stop calls this only after finishing its reset writes. Keep the mapping
 * acquired until the last host stops, including when a sibling fails setup.
 * Removing and destroying it under the same lock also prevents a new host
 * from trying to register the range before the old registration is gone.
 */
static void
__Bcm2712ReleaseResetProvider(
    _In_ struct __Bcm2712ResetProvider* provider)
{
    mtx_lock(&g_bcm2712ResetLock);
    if (--provider->References == 0) {
        list_remove(&g_bcm2712ResetProviders, &provider->Header);
        ReleaseDeviceIo(&provider->Io);
        DestroyDeviceIo(&provider->Io);
        free(provider);
    }
    mtx_unlock(&g_bcm2712ResetLock);
}

/**
 * @brief Keep each fixed electrical-interface setting beside its register number.
 *
 * The PHY is the electrical interface that sends and receives PCI Express
 * signals. Its registers are selected through a setup interface called MDIO.
 * These numbers select PHY registers, rather than byte offsets in CPU memory.
 */
struct __Bcm2712PhySetting {
    uint8_t Register;
    uint16_t Value;
};

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

    // Pi 4 uses this output for one shared size setting. Pi 5 gives each
    // address mapping its own size, so it does not use this output.
    *dmaOrder = 0;

    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status != OS_EOK) {
        return status;
    }

    // Both host reset and resistor-calibration hardware (RESCAL) are required.
    // The firmware helper checked controller types and reset IDs. The RESCAL
    // register range must also include the register reporting completion.
    if (dependencies.BridgeResetController == 0 ||
        dependencies.ResetLength < BCM_PCIE_RESCAL_MIN_LENGTH) {
        return OS_ENOTSUPPORTED;
    }

    // Reject reset blocks that this build cannot map as one complete region.
    // Checking the base first also makes the following subtraction safe.
    if (dependencies.BridgeResetBase > UINTPTR_MAX ||
        dependencies.BridgeResetLength - 1 > UINTPTR_MAX - dependencies.BridgeResetBase) {
        return OS_ENOTSUPPORTED;
    }
    if (dependencies.ResetBase > UINTPTR_MAX ||
        dependencies.ResetLength - 1 > UINTPTR_MAX - dependencies.ResetBase) {
        return OS_ENOTSUPPORTED;
    }

    // Hardware has ten address-mapping slots. At least one range is needed
    // to describe where device reads and writes can reach the system.
    if (firmware->DmaWindowCount == 0 || firmware->DmaWindowCount > BCM2712_INBOUND_COUNT) {
        return OS_ENOTSUPPORTED;
    }

    // Zero keeps the hardware defaults. A requested speed may be no faster
    // than PCI Express generation 3 (Gen3). The connection may use one, two,
    // or four lanes, the parallel signal paths that carry data.
    if (firmware->Link.MaxSpeed > BCM_PCIE_LINK_SPEED_GEN3) {
        return OS_ENOTSUPPORTED;
    }
    if (firmware->Link.Lanes != 0) {
        if (firmware->Link.Lanes != BCM_PCIE_LINK_LANES_X1 &&
            firmware->Link.Lanes != BCM_PCIE_LINK_LANES_X2 &&
            firmware->Link.Lanes != BCM_PCIE_LINK_LANES_X4) {
            return OS_ENOTSUPPORTED;
        }
    }

    for (index = 0; index < firmware->DmaWindowCount; index++) {
        window = &firmware->DmaWindows[index];

        // These address mappings support memory reads and writes. PCI I/O
        // ports use a separate address space and are not supported here.
        if (window->Space != BCM_PCIE_DT_MEMORY32 && window->Space != BCM_PCIE_DT_MEMORY64) {
            return OS_ENOTSUPPORTED;
        }

        // A device-to-system range must have a power-of-two size from 4 KiB
        // to 64 GiB. Rounding up would allow access beyond the declared range.
        if (window->Length < BCM_PCIE_INBOUND_MIN_SIZE ||
            window->Length > BCM_PCIE_INBOUND_MAX_SIZE ||
            (window->Length & (window->Length - 1))) {
            return OS_ENOTSUPPORTED;
        }

        // The PCI starting address must be a multiple of the range size.
        // The CPU physical destination must be a multiple of 4 KiB because
        // hardware does not store its lower 12 address bits.
        if ((window->BusBase & (window->Length - 1)) ||
            (window->PhysicalBase & BCM2712_INBOUND_PHYSICAL_ALIGN_MASK)) {
            return OS_ENOTSUPPORTED;
        }

        // Both complete ranges must fit: PCI addresses cannot wrap, and the
        // system connection accepts only the low 40 physical address bits.
        if (window->Length - 1 > UINT64_MAX - window->BusBase ||
            window->PhysicalBase >= BCM2712_SYSTEM_ADDRESS_LIMIT ||
            window->Length > BCM2712_SYSTEM_ADDRESS_LIMIT - window->PhysicalBase) {
            return OS_ENOTSUPPORTED;
        }
        for (other = 0; other < index; other++) {
            previous = &firmware->DmaWindows[other];

            // A device address must select only one destination. Both ranges
            // have already passed their overflow checks, including the ends.
            if (window->BusBase <= previous->BusBase + previous->Length - 1 &&
                previous->BusBase <= window->BusBase + window->Length - 1) {
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
    _In_ int assert)
{
    size_t       bank = (controller->BridgeResetId / BCM2712_RESET_LINES_PER_BANK) *
            BCM2712_RESET_BANK_STRIDE;
    uint32_t     bit = 1U << (controller->BridgeResetId % BCM2712_RESET_LINES_PER_BANK);
    uint32_t     value;
    oserr_t      status;
    unsigned int attempt;

    // SET and CLEAR act on bits written as one. Write only this host's bit:
    // reading and rewriting a bank could accidentally reset another live host.
    status = WriteDeviceIo(
        &controller->BridgeReset->Io,
        bank + (assert ? BCM2712_RESET_SET : BCM2712_RESET_CLEAR),
        bit,
        sizeof(uint32_t)
    );
    
    for (attempt = 0; status == OS_EOK && attempt < BCM2712_REGISTER_POLL_ATTEMPTS; attempt++) {
        // Reading STATUS waits for the earlier write to reach the controller
        // and reports whether reset is active. SET/CLEAR cannot report that state.
        value = (uint32_t)ReadDeviceIo(&controller->BridgeReset->Io,
                bank + BCM2712_RESET_STATUS, sizeof(uint32_t));

        // A failed read returns all bits set; that does not prove reset is active.
        // Compare just our bit; other hosts are allowed to remain in reset.
        if (value != BCM_PCIE_READ_FAILED && !!(value & bit) == !!assert) {
            BcmPciDelay(BCM_PCIE_RESET_SETTLE_MS);
            return OS_EOK;
        }
        BcmPciDelay(BCM2712_REGISTER_POLL_MS);
    }
    return status == OS_EOK ? OS_EUNKNOWN : status;
}

/**
 * @brief Write one electrical-interface register and wait for hardware to accept it.
 *
 * The PHY uses a separate setup interface named MDIO. Select its register,
 * start the write, and wait for hardware to clear the busy bit. Each setting
 * must complete before another starts; an unresponsive PHY must not hang boot.
 */
static oserr_t
__Bcm2712MdioWrite(
    _In_ PciHost_t* bus,
    _In_ uint8_t    reg,
    _In_ uint16_t   value)
{
    unsigned int attempt;
    oserr_t      status;

    // Select PHY port zero, the requested register, and the write command.
    // Port zero and write both encode as zero in the address/control word.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_MDIO_ADDRESS,
        BCM2712_MDIO_PORT0 | BCM2712_MDIO_COMMAND_WRITE | reg,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // Read back the selection so it reaches the controller before write data
    // starts the transfer. The returned selection is not completion status.
    (void)ReadDeviceIo(&bus->IoSpace, BCM2712_MDIO_ADDRESS, sizeof(uint32_t));

    // Bit 31 starts the transfer; the lower value is the complete PHY setting.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM2712_MDIO_WRITE,
        BCM2712_MDIO_BUSY | value,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    for (attempt = 0; attempt < BCM2712_REGISTER_POLL_ATTEMPTS; attempt++) {
        // Hardware clears BUSY after accepting the value. An all-ones read
        // keeps BUSY set, so a missing controller cannot look like a successful write.
        if (!(ReadDeviceIo(&bus->IoSpace, BCM2712_MDIO_WRITE, sizeof(uint32_t)) &
                BCM2712_MDIO_BUSY)) {
            return OS_EOK;
        }
        BcmPciDelay(BCM2712_REGISTER_POLL_MS);
    }
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
    unsigned int index;
    uint32_t     value;
    oserr_t      status;

    // Select the PHY's clock-generation register block. Subsequent register
    // numbers refer to that block, not to the controller's CPU-visible offsets.
    status = __Bcm2712MdioWrite(bus, BCM2712_PHY_BLOCK_SELECT, BCM2712_PHY_PLL_BLOCK);
    if (status != OS_EOK) {
        return status;
    }

    // Apply every 54 MHz setting in the reference order. The reference does
    // not document their individual bit fields, so keep each full value intact.
    // The helper waits for hardware to accept each write before the next setting.
    for (index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
        status = __Bcm2712MdioWrite(bus, settings[index].Register, settings[index].Value);
        if (status != OS_EOK) {
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
        return status;
    }

    // Read back the timing-fix bit. Older chips and single-lane ports leave
    // it clear because they do not implement that correction.
    value = (uint32_t)ReadDeviceIo(&bus->IoSpace, BCM2712_AXI_CONTROL, sizeof(uint32_t));
    if (!(value & BCM2712_AXI_PRIORITY_TIMING_FIX)) {
        // On those ports, limiting requests awaiting memory replies to 15
        // reduces the incorrect priority assignments that the missing fix avoids.
        return BcmPciUpdate(
            bus,
            BCM2712_AXI_CONTROL,
            BCM2712_AXI_OUTSTANDING_MASK,
            BCM2712_AXI_OUTSTANDING_FALLBACK
        );
    }
    return OS_EOK;
}

/**
 * @brief Map Pi 5 reset controls and establish a known controller state.
 *
 * Let the host run for calibration, then briefly reset it. Keep the connected
 * device in reset while later setup stages install address mappings. Record
 * the retained reset-provider reference immediately so Stop can drop it if
 * a later setup step fails, without unmapping another host's reset registers.
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

    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status != OS_EOK) {
        return status;
    }

    status = __Bcm2712AcquireResetProvider(&dependencies, &controller->BridgeReset);
    if (status != OS_EOK) {
        return status;
    }
    
    controller->BridgeResetMapped = 1;
    controller->BridgeResetId = dependencies.BridgeResetId;

    // The resistor-calibration block cannot be used with the bridge held in
    // reset. The reset helper checks completion and includes a settling delay.
    if (__Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Pi 5 hosts share calibration. Reuse an already-completed result so
    // initializing this host does not disturb another host that is already in use.
    if (BcmPciRescal(&dependencies, 1) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Reset this bridge's internal state after calibration, leaving the
    // shared calibration result intact. The helper waits until reset is active.
    if (__Bcm2712BridgeReset(controller, BCM2712_RESET_ASSERTED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Bring the bridge back so its own configuration registers can be used.
    if (__Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED) != OS_EOK) {
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
        return OS_ENOTSUPPORTED;
    }
    return __Bcm2712Setup(bus);
}

/**
 * @brief Find the lower 32-bit register for a device-to-system mapping slot.
 *
 * Slots are numbered from zero. The first three and the remaining seven use
 * separate register blocks. Select either the PCI address registers or the CPU
 * physical destination registers. Treating all ten as one continuous block
 * would write into unrelated registers, including interrupt settings.
 */
static size_t
__Bcm2712InboundRegister(
        _In_ unsigned int index,
        _In_ int remap)
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
 * Pi 5 describes RAM, other devices' registers, and the address used to send
 * interrupts to the Broadcom MIP controller separately. Each gets a PCI address,
 * CPU physical address, and size. BcmPciDmaAddress only accepts buffers in
 * ranges that the firmware reader identified as RAM.
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

    // Pi 4 uses this shared size parameter; Pi 5 sets a size for each mapping.
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
        return status;
    }

    for (index = 0; index < firmware->DmaWindowCount; index++) {
        window = &firmware->DmaWindows[index];
        bar = __Bcm2712InboundRegister(index, 0);
        remap = __Bcm2712InboundRegister(index, 1);

        // Earlier checks confirmed a power-of-two length. Here, order means
        // the exponent: 4 KiB = 2^12 gives 12. Sizes 4..32 KiB use codes
        // 0x1c..0x1f; sizes from 64 KiB use the exponent minus 15.
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
            return status;
        }

        // The physical destination has its own upper half; it need not match
        // the address the device uses for the same mapping.
        status = WriteDeviceIo(
            &bus->IoSpace,
            remap + BCM_PCIE_REGISTER_HIGH_OFFSET,
            window->PhysicalBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT,
            sizeof(uint32_t)
        );
        if (status != OS_EOK) {
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
            return status;
        }
    }
    return OS_EOK;
}

/**
 * @brief Set Pi 5 connection settings and let the device start after address setup.
 *
 * Honor firmware speed and lane limits. Keep clocks running and link sleep
 * disabled until drivers can coordinate power management. This function waits
 * for reset recovery; the shared initializer then checks whether the connection is ready.
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
        return status;
    }

    // Set the release bit to let the connected device start; Pi 4 clears a bit instead.
    status = BcmPciUpdate(
        bus,
        BCM2712_PCIE_CONTROL,
        BCM2712_PERST_RELEASE,
        BCM2712_PERST_RELEASE
    );
    if (status != OS_EOK) {
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
 * @brief Stop this Pi 5 connection and drop its reset-provider reference.
 *
 * Prepare may have failed before acquiring the mapping. If it was acquired,
 * hold the connected device in reset but let the host controller run. Keeping
 * a Pi 5 host in reset can disturb calibration used by another active host.
 */
static void
__Bcm2712Stop(
    _In_ PciHost_t* bus)
{
    struct BcmPciHost* controller = bus->OpContext;

    if (!controller->BridgeResetMapped) {
        return;
    }

    // Clear the release bit to keep the device from using incomplete address
    // mappings. Attempt shutdown even though this callback cannot report errors.
    (void)BcmPciUpdate(bus, BCM2712_PCIE_CONTROL,
            BCM2712_PERST_RELEASE, BCM_PCIE_DISABLED);

    // Let the host controller run even if Prepare failed while it was in reset.
    // Keeping it in reset could disturb calibration shared with another host.
    (void)__Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED);

    __Bcm2712ReleaseResetProvider(controller->BridgeReset);
    controller->BridgeReset = NULL;
    controller->BridgeResetMapped = 0;
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
