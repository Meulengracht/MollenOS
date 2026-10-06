#include "bcm.h"
#include "registers.h"
#include <ddk/utils.h>

/**
 * @brief Keep each fixed electrical-interface setting beside its register number.
 *
 * These are PHY register numbers reached through MDIO, the controller's small
 * setup interface. They are not byte offsets in the CPU's controller mapping.
 */
struct __Bcm2712PhySetting {
    uint8_t Register;
    uint16_t Value;
};

/**
 * @brief Check that firmware describes a Pi 5 setup the driver can represent.
 *
 * Run before any hardware write. Unlike Pi 4, Pi 5 needs an external bridge
 * reset provider and can map several independent device-to-system ranges.
 * Reject descriptions which would wrap addresses, overlap, or need rounding
 * into memory or registers that firmware did not authorize.
 */
static oserr_t
__Bcm2712Validate(
        _In_ const struct FdtPciHost* firmware,
        _Out_ unsigned int* dmaOrder)
{
    struct FdtPciDependencies dependencies;
    const struct FdtPciWindow* window;
    const struct FdtPciWindow* previous;
    oserr_t status;
    uint32_t index;
    uint32_t other;

    // The common interface also serves Pi 4's single memory-size field. Pi 5
    // has a size per mapping, so it has no aggregate order to return here.
    *dmaOrder = 0;
    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status != OS_EOK) {
        return status;
    }

    // Both reset mechanisms are required. The resolver already checked bank
    // boundaries and provider types; RESCAL must include its completion word.
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

    // There are ten independent hardware slots. An empty list would leave the
    // host without any declared destination for device-to-system traffic.
    if (firmware->DmaWindowCount == 0 || firmware->DmaWindowCount > BCM2712_INBOUND_COUNT) {
        return OS_ENOTSUPPORTED;
    }

    // Zero keeps the hardware's speed/width choice. Explicit choices must fit
    // BCM2712: at most Gen3, and one, two, or four physical lanes.
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

        // These decoders accept memory transactions, not PCI I/O-port accesses.
        if (window->Space != BCM_PCIE_DT_MEMORY32 && window->Space != BCM_PCIE_DT_MEMORY64) {
            return OS_ENOTSUPPORTED;
        }

        // Inbound size codes represent powers of two from 4 KiB to 64 GiB.
        // Unlike an outbound limit, this size must not be rounded up.
        if (window->Length < BCM_PCIE_INBOUND_MIN_SIZE ||
            window->Length > BCM_PCIE_INBOUND_MAX_SIZE ||
            (window->Length & (window->Length - 1))) {
            return OS_ENOTSUPPORTED;
        }

        // The device-visible base aligns to the window size. The physical
        // destination has 4 KiB precision because its low bits are not stored.
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
 * @brief Change only this host's reset line in the shared bridge-reset block.
 *
 * The firmware selector identifies a bank and one bit within it. Poll the
 * reported state with a finite wait, then allow the reset change to settle.
 * The caller owns an acquired mapping for the entire operation.
 */
static oserr_t
__Bcm2712BridgeReset(
        _In_ struct BcmPciHost* controller,
        _In_ int assert)
{
    size_t bank = (controller->BridgeResetId / BCM2712_RESET_LINES_PER_BANK) *
            BCM2712_RESET_BANK_STRIDE;
    uint32_t bit = 1U << (controller->BridgeResetId % BCM2712_RESET_LINES_PER_BANK);
    uint32_t value;
    oserr_t status;
    unsigned int attempt;

    // SET and CLEAR act on bits written as one. Write only this host's bit:
    // reading and rewriting a bank could accidentally reset another live host.
    status = WriteDeviceIo(&controller->BridgeReset,
            bank + (assert ? BCM2712_RESET_SET : BCM2712_RESET_CLEAR), bit, sizeof(uint32_t));
    for (attempt = 0; status == OS_EOK && attempt < BCM2712_REGISTER_POLL_ATTEMPTS; attempt++) {
        // Reading STATUS orders the earlier write and reports whether the
        // selected line is now asserted; the SET/CLEAR words are not status.
        value = (uint32_t)ReadDeviceIo(&controller->BridgeReset,
                bank + BCM2712_RESET_STATUS, sizeof(uint32_t));

        // Do not treat an all-ones failed read as proof of asserted reset.
        // Compare just our bit; other hosts are allowed to remain in reset.
        if (value != BCM_PCIE_READ_FAILED && !!(value & bit) == !!assert) {
            return BcmPciDelay(BCM_PCIE_RESET_SETTLE_MS);
        }
        status = BcmPciDelay(BCM2712_REGISTER_POLL_MS);
    }
    return status == OS_EOK ? OS_EUNKNOWN : status;
}

/**
 * @brief Write one electrical-interface register and wait for acknowledgement.
 *
 * The PHY uses a separate setup interface named MDIO. Select its register,
 * start the write, and wait for hardware to clear the busy bit. Each setting
 * must complete before another starts; an unresponsive PHY must not hang boot.
 */
static oserr_t
__Bcm2712MdioWrite(
        _In_ PciHost_t* bus,
        _In_ uint8_t reg,
        _In_ uint16_t value)
{
    unsigned int attempt;

    // Select PHY port zero, the requested register, and the write command.
    // Port zero and write both encode as zero in the address/control word.
    if (WriteDeviceIo(&bus->IoSpace, BCM2712_MDIO_ADDRESS,
            BCM2712_MDIO_PORT0 | BCM2712_MDIO_COMMAND_WRITE | reg, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Read back the selection so it reaches the controller before write data
    // starts the transfer. The returned selection is not completion status.
    (void)ReadDeviceIo(&bus->IoSpace, BCM2712_MDIO_ADDRESS, sizeof(uint32_t));

    // Bit 31 starts the transfer; the lower value is the complete PHY setting.
    if (WriteDeviceIo(&bus->IoSpace, BCM2712_MDIO_WRITE,
            BCM2712_MDIO_BUSY | value, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    for (attempt = 0; attempt < BCM2712_REGISTER_POLL_ATTEMPTS; attempt++) {
        // Hardware clears BUSY after accepting the value. An all-ones read
        // keeps BUSY set, so it cannot falsely acknowledge a missing controller.
        if (!(ReadDeviceIo(&bus->IoSpace, BCM2712_MDIO_WRITE, sizeof(uint32_t)) &
                BCM2712_MDIO_BUSY)) {
            return OS_EOK;
        }
        if (BcmPciDelay(BCM2712_REGISTER_POLL_MS) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }
    return OS_EUNKNOWN;
}

/**
 * @brief Set the Pi 5 electrical clock, failed-read behavior, and memory fixes.
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
    uint32_t value;

    // Select the PHY's clock-generation register block. Subsequent register
    // numbers refer to that block, not to the controller's CPU-visible offsets.
    if (__Bcm2712MdioWrite(bus, BCM2712_PHY_BLOCK_SELECT, BCM2712_PHY_PLL_BLOCK) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Apply every 54 MHz setting in the reference order. The reference does
    // not document their individual bit fields, so keep each full value intact.
    // The helper acknowledges each write before we move to the next setting.
    for (index = 0; index < sizeof(settings) / sizeof(settings[0]); index++) {
        if (__Bcm2712MdioWrite(bus, settings[index].Register, settings[index].Value) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }
    if (BcmPciDelay(BCM_PCIE_PHY_SETTLE_MS) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Tell the internal power-state timers that each 54 MHz clock period is
    // about 18 ns. Leaving the reset default would give incorrect transition times.
    if (BcmPciUpdate(bus, BCM2712_PHY_TIMERS,
            BCM2712_PHY_PM_CLOCK_PERIOD_MASK, BCM2712_PHY_PM_CLOCK_54MHZ_NS) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Suppress both general and missing-address error replies on the system
    // bus. Probing an absent PCI device should return data, not fault the CPU.
    if (BcmPciUpdate(bus, BCM2712_UBUS_CONTROL,
            BCM2712_UBUS_ERROR_DISABLE_MASK, BCM2712_UBUS_ERROR_DISABLE_MASK) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Return all ones on a failed read: the generic scanner recognizes an
    // all-ones vendor ID as an absent device and continues scanning.
    if (WriteDeviceIo(&bus->IoSpace, BCM2712_READ_ERROR,
            BCM2712_READ_ERROR_VALUE, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Give a slow device roughly 250 ms before the system-bus request expires.
    // These ticks come from the controller clock, not the CPU clock.
    if (WriteDeviceIo(&bus->IoSpace, BCM2712_UBUS_TIMEOUT,
            BCM2712_UBUS_TIMEOUT_250MS, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // End repeated configuration-retry replies slightly earlier, at about
    // 240 ms, so they finish before the surrounding system-bus timeout.
    if (WriteDeviceIo(&bus->IoSpace, BCM2712_RETRY_TIMEOUT,
            BCM2712_RETRY_TIMEOUT_240MS, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Stop forwarding queued request priorities through the faulty path and
    // enable the priority-table, update-timing, and update-gating corrections.
    if (BcmPciUpdate(bus, BCM2712_AXI_CONTROL,
            BCM2712_AXI_PRIORITY_SETUP_MASK, BCM2712_AXI_PRIORITY_FIXES) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Vendor-specific device messages must not change memory priorities during
    // enumeration. That mechanism has known faults and is not configured here.
    if (BcmPciUpdate(bus, BCM2712_MISC_CONTROL1,
            BCM2712_MISC_VDM_PRIORITY_ENABLE, BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Read back the timing-fix bit. Older silicon and single-lane ports leave
    // it clear because they do not implement that correction.
    value = (uint32_t)ReadDeviceIo(&bus->IoSpace, BCM2712_AXI_CONTROL, sizeof(uint32_t));
    if (!(value & BCM2712_AXI_PRIORITY_TIMING_FIX)) {
        // On those ports, limiting requests awaiting memory replies to 15
        // reduces the incorrect priority assignments that the missing fix avoids.
        return BcmPciUpdate(bus, BCM2712_AXI_CONTROL,
                BCM2712_AXI_OUTSTANDING_MASK, BCM2712_AXI_OUTSTANDING_FALLBACK);
    }
    return OS_EOK;
}

/**
 * @brief Map Pi 5 reset controls and establish a known controller state.
 *
 * Calibration needs bridge reset released first. After calibration, pulse the
 * bridge reset and keep the endpoint held while later stages install windows.
 * Save mapping ownership immediately so Stop can clean up any partial failure.
 */
static oserr_t
__Bcm2712Prepare(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware)
{
    struct BcmPciHost* controller = bus->OpContext;
    struct FdtPciDependencies dependencies;
    oserr_t status;
    uint32_t value;

    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status != OS_EOK) {
        return status;
    }
    status = BcmPciMapRegisters(&controller->BridgeReset,
            dependencies.BridgeResetBase, dependencies.BridgeResetLength);
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
    // initializing this host does not restart calibration under a live sibling.
    if (BcmPciRescal(&dependencies, 1) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Reset this bridge's internal state after calibration, leaving the
    // shared calibration result intact. The helper waits for assertion.
    if (__Bcm2712BridgeReset(controller, BCM2712_RESET_ASSERTED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Bring the bridge back so its own configuration registers can be used.
    if (__Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Clearing BCM2712's active-low release bit holds the connected device
    // in reset. BCM2711's SW_INIT bits do not control this variant's endpoint.
    if (BcmPciUpdate(bus, BCM2712_PCIE_CONTROL,
            BCM2712_PERST_RELEASE, BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Power up the electrical interface and clear clock-request/sleep modes
    // inherited from firmware before applying the known PHY setup below.
    if (BcmPciUpdate(bus, BCM2712_HARD_DEBUG,
            BCM_PCIE_DEBUG_SERDES_POWERDOWN | BCM2712_DEBUG_CLOCK_MASK,
            BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    if (BcmPciDelay(BCM_PCIE_PHY_SETTLE_MS) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Permit system-memory accesses, enable the expected read-reply boundary
    // modes and unsupported-read handling, and choose Pi 5's 512-byte bursts.
    if (BcmPciUpdate(bus, BCM_PCIE_MISC_CONTROL,
            BCM_PCIE_MISC_BURST_MASK | BCM_PCIE_MISC_MEMORY_ENABLES,
            BCM2712_MISC_BURST_512_BYTES | BCM_PCIE_MISC_MEMORY_ENABLES) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Select little-endian data for the controller's BAR2 inbound path, matching
    // PCI memory byte order. Preserve all other vendor-control fields.
    if (BcmPciUpdate(bus, BCM_PCIE_VENDOR_CONTROL,
            BCM_PCIE_VENDOR_BAR2_ENDIAN_MASK, BCM_PCIE_VENDOR_BAR2_LITTLE_ENDIAN) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Verify that this is the CPU-side root port. An all-ones failed read
    // would also have the root bit set, so reject it explicitly before setup.
    value = (uint32_t)ReadDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_STATUS, sizeof(uint32_t));
    if (value == BCM_PCIE_READ_FAILED || !(value & BCM_PCIE_STATUS_ROOT_PORT)) {
        return OS_ENOTSUPPORTED;
    }
    return __Bcm2712Setup(bus);
}

/**
 * @brief Find the low register word for a zero-based inbound slot.
 *
 * The first three slots and the remaining seven slots occupy separate blocks.
 * Select either the PCI-address block or its physical-destination block. Using
 * one uninterrupted stride would write into unrelated registers, including MSI.
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
 * Pi 5 describes RAM, peer-device registers and the MIP interrupt destination
 * separately. Each receives its own address pair and size; none is implicitly
 * treated as RAM. The shared DMA-address helper separately restricts buffers
 * to mappings which firmware parsing classified as RAM.
 */
static oserr_t
__Bcm2712Inbound(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware,
        _In_ unsigned int dmaOrder)
{
    const struct FdtPciWindow* window;
    unsigned int index;
    unsigned int order;
    uint32_t sizeCode;
    size_t bar;
    size_t remap;

    // Pi 4 needs the shared callback's aggregate size; Pi 5 encodes each slot.
    (void)dmaOrder;
    for (index = 0; index < BCM2712_INBOUND_COUNT; index++) {
        bar = __Bcm2712InboundRegister(index, 0);
        remap = __Bcm2712InboundRegister(index, 1);

        // A zero size code disables this PCI-address decoder. Clear even unused
        // slots so an old firmware mapping cannot remain as an extra route.
        if (WriteDeviceIo(&bus->IoSpace, bar,
                BCM_PCIE_INBOUND_DISABLED, sizeof(uint32_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }

        // Clear physical-side access enable too. Stale high address words do
        // not grant access when both the size and access-enable fields are off.
        if (WriteDeviceIo(&bus->IoSpace, remap,
                BCM_PCIE_DISABLED, sizeof(uint32_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }

    // Disable the separate legacy MSI decoder. Pi 5's MIP message destination
    // is represented by one of the ordinary inbound mappings installed below.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_MSI_BAR,
            BCM_PCIE_MSI_DISABLED, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    for (index = 0; index < firmware->DmaWindowCount; index++) {
        window = &firmware->DmaWindows[index];
        bar = __Bcm2712InboundRegister(index, 0);
        remap = __Bcm2712InboundRegister(index, 1);

        // Validation established a power-of-two length. Small windows (4..32
        // KiB) use codes 0x1c..0x1f; sizes from 64 KiB use order minus 15.
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
        if (WriteDeviceIo(&bus->IoSpace, bar + BCM_PCIE_REGISTER_HIGH_OFFSET,
                window->BusBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT, sizeof(uint32_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }

        // The physical destination has its own upper half; it need not match
        // the address the device uses for the same mapping.
        if (WriteDeviceIo(&bus->IoSpace, remap + BCM_PCIE_REGISTER_HIGH_OFFSET,
                window->PhysicalBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT, sizeof(uint32_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }

        // Set the lower physical address and allow access to that destination.
        // Validation proved its low 12 address bits are zero, leaving bit 0 free.
        if (WriteDeviceIo(&bus->IoSpace, remap,
                (uint32_t)window->PhysicalBase | BCM2712_INBOUND_REMAP_ENABLE,
                sizeof(uint32_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }

        // Enable the PCI decoder last, after both destination halves are ready.
        // Size alignment guarantees that address bits do not overlap the code.
        if (WriteDeviceIo(&bus->IoSpace, bar,
                (uint32_t)window->BusBase | sizeCode, sizeof(uint32_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }
    return OS_EOK;
}

/**
 * @brief Set Pi 5 link policy and release the device after mappings are ready.
 *
 * Honor firmware speed and lane limits. Keep clocks running and link sleep
 * disabled until drivers can coordinate power management. This function waits
 * for reset recovery; the shared initializer then polls for actual link-up.
 */
static oserr_t
__Bcm2712Start(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware)
{
    uint32_t buses = firmware->BusStart |
            ((uint32_t)(firmware->BusStart + 1) << BCM_PCIE_SECONDARY_BUS_SHIFT) |
            ((uint32_t)firmware->BusEnd << BCM_PCIE_SUBORDINATE_BUS_SHIFT);
    uint16_t linkControl;

    if (firmware->Link.MaxSpeed != 0) {
        // Advertise firmware's speed ceiling so later configuration cannot
        // assume the root supports a faster link than the board permits.
        if (BcmPciUpdate(bus, BCM_PCIE_LINK_CAPABILITY,
                BCM_PCIE_LINK_CAP_SPEED_MASK, firmware->Link.MaxSpeed) != OS_EOK) {
            return OS_EUNKNOWN;
        }

        // Preserve other controls with a 16-bit read. The following word is
        // status and must not be included in the matching write.
        linkControl = (uint16_t)ReadDeviceIo(&bus->IoSpace,
                BCM_PCIE_LINK_CONTROL2, sizeof(uint16_t));

        // Request that same speed for the upcoming connection attempt.
        if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_CONTROL2,
                (linkControl & ~BCM_PCIE_LINK_CONTROL2_SPEED_MASK) | firmware->Link.MaxSpeed,
                sizeof(uint16_t)) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }
    if (firmware->Link.Lanes != 0) {
        // Replace the reset-default width with the number of lanes declared
        // by firmware; the connected board may use fewer lanes than the core.
        if (BcmPciUpdate(bus, BCM_PCIE_LINK_CAPABILITY, BCM_PCIE_LINK_CAP_WIDTH_MASK,
                firmware->Link.Lanes << BCM_PCIE_LINK_CAP_WIDTH_SHIFT) != OS_EOK) {
            return OS_EUNKNOWN;
        }

        // The reference width-override sequence also enables the PHY's P2
        // power-down control. This sets that control, not the active link state.
        if (BcmPciUpdate(bus, BCM2712_PHY_CONTROL,
                BCM2712_PHY_P2_POWERDOWN_ENABLE, BCM2712_PHY_P2_POWERDOWN_ENABLE) != OS_EOK) {
            return OS_EUNKNOWN;
        }
    }

    // Pi 5 does not support the older variant's spread-spectrum recipe. Keep
    // the working clock setup and make the unsupported request visible.
    if (firmware->Link.EnableSsc) {
        WARNING("BCM2712 PCIe segment %u: spread-spectrum clocking is unsupported", firmware->Segment);
    }

    // Identify the root as a PCI bridge so the generic scanner visits the
    // secondary bus and discovers RP1 or an external endpoint normally.
    if (BcmPciUpdate(bus, BCM_PCIE_CLASS_CODE,
            BCM_PCIE_CLASS_CODE_MASK, BCM_PCIE_CLASS_CODE_BRIDGE) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Remove advertised L0s/L1 automatic sleep support. Device drivers are
    // blocked, so there is no coordinated endpoint power-management policy yet.
    if (BcmPciUpdate(bus, BCM_PCIE_LINK_CAPABILITY,
            BCM_PCIE_LINK_CAP_ASPM_MASK, BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Hide the deeper L1 sleep modes as well. Their clock-request handshake
    // must not be selected while enumeration uses an always-running clock.
    if (BcmPciUpdate(bus, BCM2712_ROOT_CAPABILITY,
            BCM2712_ROOT_CAP_L1SS_MASK, BCM2712_ROOT_CAP_L1SS_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Read the current 16-bit control word, preserving unrelated link controls
    // and avoiding the adjacent status word during the update below.
    linkControl = (uint16_t)ReadDeviceIo(&bus->IoSpace,
            BCM_PCIE_LINK_CONTROL, sizeof(uint16_t));

    // Disable any L0s/L1 selection firmware left active. Removing advertised
    // support alone does not clear an already-selected power-saving mode.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_CONTROL,
            linkControl & ~BCM_PCIE_LINK_CONTROL_ASPM_MASK, sizeof(uint16_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Force the reference clock output on and disable device-controlled clock
    // requests. A device must have a stable clock as it comes out of reset.
    if (BcmPciUpdate(bus, BCM2712_HARD_DEBUG,
            BCM2712_DEBUG_CLOCK_MASK, BCM2712_DEBUG_CLOCK_ALWAYS_ON) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Route configuration requests to the root, immediate child and remaining
    // firmware bus range before allowing the connected device to start.
    if (BcmPciUpdate(bus, BCM_PCIE_BUS_NUMBERS, BCM_PCIE_BUS_NUMBERS_MASK, buses) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Set the release bit: unlike Pi 4, setting this bit removes endpoint reset.
    if (BcmPciUpdate(bus, BCM2712_PCIE_CONTROL,
            BCM2712_PERST_RELEASE, BCM2712_PERST_RELEASE) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Allow 100 ms for reset recovery even if link status changes immediately.
    // The shared code then polls for at most another 100 ms and publishes
    // configuration access only after both physical and data links are active.
    if (BcmPciDelay(BCM_PCIE_RESET_CONFIG_WAIT_MS) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

/**
 * @brief Stop this Pi 5 link and release its reset-provider mapping.
 *
 * A partial Prepare may have failed before acquiring the mapping. Once owned,
 * hold the endpoint in reset but leave bridge reset released: holding a Pi 5
 * bridge in reset can disturb calibration needed by a different active host.
 */
static void
__Bcm2712Stop(
        _In_ PciHost_t* bus)
{
    struct BcmPciHost* controller = bus->OpContext;

    if (!controller->BridgeResetMapped) {
        return;
    }

    // Clear the release bit to stop the endpoint using partially configured
    // windows. Shutdown is best effort because this callback cannot return errors.
    (void)BcmPciUpdate(bus, BCM2712_PCIE_CONTROL,
            BCM2712_PERST_RELEASE, BCM_PCIE_DISABLED);

    // Recover even if Prepare failed with bridge reset asserted. Do not undo
    // shared calibration or leave this bridge holding its reset line active.
    (void)__Bcm2712BridgeReset(controller, BCM2712_RESET_RELEASED);
    ReleaseDeviceIo(&controller->BridgeReset);
    DestroyDeviceIo(&controller->BridgeReset);
    controller->BridgeResetMapped = 0;
}

// Share configuration indexing and the generic scanner with Pi 4, but select
// Pi 5's external reset provider, PHY sequence and ten-window inbound layout.
const struct BcmPciVariant Bcm2712PciVariant = {
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
