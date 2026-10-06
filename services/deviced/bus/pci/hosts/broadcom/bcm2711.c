#include "bcm.h"
#include "registers.h"

/**
 * @brief Resolve optional Pi 4 dependencies before resetting the bridge.
 *
 * Some firmware trees supply an external calibration block or fixed clock.
 * The resolver checks those descriptions; this variant still owns its bridge
 * reset through the controller itself, so a separate bridge provider is rejected.
 */
static oserr_t
__BcmDependencies(
        _In_ const struct FdtPciHost* firmware)
{
    struct FdtPciDependencies dependencies;
    oserr_t status;

    status = FdtResolvePciDependencies(firmware, &dependencies);
    if (status == OS_EOK && dependencies.BridgeResetController != 0) {
        return OS_ENOTSUPPORTED;
    }
    if (status != OS_EOK || dependencies.ResetLength == 0) {
        return status;
    }

    // The shared helper starts resistor calibration, waits for completion,
    // and releases its temporary mapping. Pi 4 does not reuse Pi 5's shared
    // completion state; each requested calibration is performed here.
    return BcmPciRescal(&dependencies, 0);
}

/**
 * @brief Check the single RAM mapping and calculate its hardware size code input.
 *
 * BCM2711's main inbound window always starts at CPU physical zero. Hardware
 * needs a power-of-two size even when firmware describes, for example, 3 GiB.
 * Only the programmed size is rounded; the firmware length remains the limit
 * used by BcmPciDmaAddress when checking actual buffers.
 */
static oserr_t
__BcmValidateWindows(
        _In_ const struct FdtPciHost* firmware,
        _Out_ unsigned int* dmaOrder)
{
    const struct FdtPciWindow* window;
    uint64_t size;
    unsigned int order;

    // This variant programs BAR2 for RAM only. It cannot represent additional
    // peer-device or interrupt mappings using the same setup procedure.
    if (firmware->DmaWindowCount != 1) {
        return OS_ENOTSUPPORTED;
    }
    window = &firmware->DmaWindows[0];
    if (window->Kind != FdtDmaWindowRam || window->PhysicalBase != 0) {
        return OS_ENOTSUPPORTED;
    }
    if (window->Space != BCM_PCIE_DT_MEMORY32 && window->Space != BCM_PCIE_DT_MEMORY64) {
        return OS_ENOTSUPPORTED;
    }
    if (window->Length == 0 || window->Length > BCM_PCIE_INBOUND_MAX_SIZE) {
        return OS_ENOTSUPPORTED;
    }

    // Honor an explicit memory-controller size. Otherwise choose the smallest
    // power of two that contains firmware's RAM range, without changing it.
    size = firmware->ScbSize;
    if (size == 0) {
        size = 1;
        while (size < window->Length) {
            size <<= 1;
        }
    }
    if (size < window->Length || size < BCM2711_INBOUND_MIN_SIZE ||
        size > BCM_PCIE_INBOUND_MAX_SIZE) {
        return OS_ENOTSUPPORTED;
    }

    // The base and size must describe exactly one hardware-aligned window;
    // reject wrapping addresses before any controller registers are changed.
    if ((size & (size - 1)) || (window->BusBase & (size - 1)) ||
        size - 1 > UINT64_MAX - window->BusBase) {
        return OS_ENOTSUPPORTED;
    }

    // Preserve the reference driver's lower-4-GiB placement restriction.
    // That address space must also leave room for device register windows;
    // passing size alignment alone does not establish a supported Pi 4 layout.
    if (window->BusBase > BCM2711_INBOUND_RESTRICTED_BASE_START &&
        window->BusBase < BCM2711_INBOUND_RESTRICTED_BASE_END) {
        return OS_ENOTSUPPORTED;
    }

    // The register stores an encoding derived from log2(size), not a byte
    // count. Return that order for both BAR2 and the matching system-bus field.
    order = 0;
    while ((1ULL << order) < size) {
        order++;
    }
    *dmaOrder = order;
    return OS_EOK;
}

/**
 * @brief Reset the Pi 4 host while keeping the connected device in reset.
 *
 * The bridge must be running for calibration, then reset once to establish
 * known controller state. The connected device is released only after address
 * windows and link policy are ready in the later initialization stages.
 */
static oserr_t
__Bcm2711Prepare(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware)
{
    oserr_t status;

    // Release bridge reset for calibration, but assert endpoint reset so the
    // device cannot communicate through a controller we are still configuring.
    status = BcmPciUpdate(bus, BCM2711_SW_INIT,
            BCM2711_RESET_ASSERT_BOTH, BCM2711_PERST_ASSERT);
    if (status != OS_EOK) {
        return status;
    }
    status = __BcmDependencies(firmware);
    if (status != OS_EOK) {
        return status;
    }

    // Reset the bridge's internal state without releasing the endpoint.
    if (BcmPciUpdate(bus, BCM2711_SW_INIT,
            BCM2711_RESET_ASSERT_BOTH, BCM2711_RESET_ASSERT_BOTH) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    if (BcmPciDelay(BCM_PCIE_RESET_SETTLE_MS) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Bring only the bridge back; address translation is not configured yet.
    if (BcmPciUpdate(bus, BCM2711_SW_INIT,
            BCM2711_BRIDGE_RESET_ASSERT, BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Power up the high-speed electrical interface. Disable device-controlled
    // clock requests and deeper link sleep while initial setup is in progress.
    if (BcmPciUpdate(bus, BCM2711_HARD_DEBUG,
            BCM_PCIE_DEBUG_SERDES_POWERDOWN | BCM2711_DEBUG_CLOCK_POWER_MASK,
            BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    if (BcmPciDelay(BCM_PCIE_PHY_SETTLE_MS) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Allow device access to system memory and enable the expected read-reply
    // modes. Pi 4 uses the 128-byte burst encoding, which clears the burst field.
    // BcmPciUpdate also ORs in the named enable bits while preserving other bits.
    if (BcmPciUpdate(bus, BCM_PCIE_MISC_CONTROL, BCM_PCIE_MISC_BURST_MASK,
            BCM2711_MISC_BURST_128_BYTES | BCM_PCIE_MISC_MEMORY_ENABLES) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // All ones means the register read failed; it must not masquerade as a
    // valid root-port indication simply because that bit would also be set.
    if (ReadDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_STATUS, sizeof(uint32_t)) ==
            BCM_PCIE_READ_FAILED) {
        return OS_ENOTSUPPORTED;
    }

    // This driver controls the CPU side of the connection. Reject endpoint
    // mode before the shared initializer programs host address windows.
    if (!(ReadDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_STATUS, sizeof(uint32_t)) &
            BCM_PCIE_STATUS_ROOT_PORT)) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Configure the device-to-RAM view using BCM2711's BAR2 window.
 *
 * Validation already proved that the physical destination is zero and the
 * PCI base aligns to the encoded size. Disable unused decoders so firmware
 * leftovers cannot create additional device-to-system mappings.
 */
static oserr_t
__Bcm2711Inbound(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware,
        _In_ unsigned int dmaOrder)
{
    const struct FdtPciWindow* window;
    uint32_t sizeCode;

    window = &firmware->DmaWindows[0];
    sizeCode = dmaOrder - BCM_PCIE_INBOUND_LARGE_SIZE_BIAS;

    // BAR1 is not the RAM window owned by this variant. Clear its size to
    // remove any extra mapping left by firmware.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_INBOUND_BAR1,
            BCM_PCIE_INBOUND_DISABLED, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // BAR3 is also unused; leaving it enabled could provide a second RAM view.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_INBOUND_BAR3,
            BCM_PCIE_INBOUND_DISABLED, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Message interrupts are not configured yet. Do not accept writes through
    // a legacy MSI target inherited from firmware.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_MSI_BAR,
            BCM_PCIE_MSI_DISABLED, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // The validated base leaves the low size bits free. Combine its lower
    // address half with the nonzero size code to enable the RAM window.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_INBOUND_BAR2,
            (uint32_t)window->BusBase | sizeCode, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Preserve the upper PCI address half: device-visible RAM may start above
    // 4 GiB even though its CPU physical destination is fixed at zero.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_INBOUND_BAR2 + BCM_PCIE_REGISTER_HIGH_OFFSET,
            window->BusBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT, sizeof(uint32_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Tell the system-memory side the same size as BAR2, so both sides of the
    // controller agree about how much address space this RAM view covers.
    return BcmPciUpdate(bus, BCM_PCIE_MISC_CONTROL, BCM2711_MISC_SCB0_SIZE_MASK,
            sizeCode << BCM2711_MISC_SCB0_SIZE_SHIFT);
}

/**
 * @brief Establish bridge identity and link policy, then release the Pi 4 device.
 *
 * The shared initializer has installed the address windows already. Keep the
 * existing Gen2, no-link-power-saving policy and wait the required 100 ms after
 * endpoint reset before the shared code starts its bounded link-status polling.
 */
static oserr_t
__Bcm2711Start(
        _In_ PciHost_t* bus,
        _In_ const struct FdtPciHost* firmware)
{
    uint32_t buses;
    uint16_t linkControl;

    // Assign the root bus, its immediate child, and the highest permitted bus.
    // Validation guarantees that the firmware range has room for a child bus.
    buses = firmware->BusStart |
            ((uint32_t)(firmware->BusStart + 1) << BCM_PCIE_SECONDARY_BUS_SHIFT) |
            ((uint32_t)firmware->BusEnd << BCM_PCIE_SUBORDINATE_BUS_SHIFT);

    // Present the root as a PCI bridge so the generic scanner follows its
    // secondary bus instead of treating the controller as a normal endpoint.
    if (BcmPciUpdate(bus, BCM_PCIE_CLASS_CODE,
            BCM_PCIE_CLASS_CODE_MASK, BCM_PCIE_CLASS_CODE_BRIDGE) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Keep inbound BAR2 bytes in PCI's little-endian order rather than asking
    // the controller to swap the data delivered to RAM.
    if (BcmPciUpdate(bus, BCM_PCIE_VENDOR_CONTROL,
            BCM_PCIE_VENDOR_BAR2_ENDIAN_MASK, BCM_PCIE_VENDOR_BAR2_LITTLE_ENDIAN) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Advertise Gen2 as the maximum speed and remove automatic L0s/L1 sleep
    // support. Endpoint power saving is not coordinated at this bring-up stage.
    if (BcmPciUpdate(bus, BCM_PCIE_LINK_CAPABILITY,
            BCM_PCIE_LINK_CAP_ASPM_MASK | BCM_PCIE_LINK_CAP_SPEED_MASK,
            BCM_PCIE_LINK_SPEED_GEN2) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Read the 16-bit link-control word to preserve unrelated controls. Avoid
    // a wider access here, which would also touch the adjacent status word.
    linkControl = (uint16_t)ReadDeviceIo(&bus->IoSpace,
            BCM_PCIE_LINK_CONTROL2, sizeof(uint16_t));

    // Request the same Gen2 speed that the capability above advertises.
    if (WriteDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_CONTROL2,
            (linkControl & ~BCM_PCIE_LINK_CONTROL2_SPEED_MASK) | BCM_PCIE_LINK_SPEED_GEN2,
            sizeof(uint16_t)) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Publish bus routing before the device comes out of reset so subsequent
    // configuration requests reach the bus numbers used by the scanner.
    if (BcmPciUpdate(bus, BCM_PCIE_BUS_NUMBERS, BCM_PCIE_BUS_NUMBERS_MASK, buses) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // Clearing the active-high PERST bit lets the connected device start.
    if (BcmPciUpdate(bus, BCM2711_SW_INIT,
            BCM2711_PERST_ASSERT, BCM_PCIE_DISABLED) != OS_EOK) {
        return OS_EUNKNOWN;
    }

    // A device needs recovery time after reset even if link bits become set
    // immediately. Configuration access is not published until the later poll.
    if (BcmPciDelay(BCM_PCIE_RESET_CONFIG_WAIT_MS) != OS_EOK) {
        return OS_EUNKNOWN;
    }
    return OS_EOK;
}

/**
 * @brief Hold both the Pi 4 bridge and its connected device in reset.
 *
 * This also handles a failed Prepare: the mapping still exists, and resetting
 * both sides prevents use of partially configured address windows. Unlike Pi 5,
 * this variant does not need to leave a shared calibration bridge running.
 */
static void
__Bcm2711Stop(
        _In_ PciHost_t* bus)
{
    // Assert both active-high reset bits, preserving the rest of the register.
    // Shutdown is best effort because this callback has no error return.
    (void)BcmPciUpdate(bus, BCM2711_SW_INIT,
            BCM2711_RESET_ASSERT_BOTH, BCM2711_RESET_ASSERT_BOTH);
}

// The shared host code uses these callbacks for the Pi 4 register layout. Keep
// configuration indexing shared, while reset and inbound RAM setup stay local.
const struct BcmPciVariant Bcm2711PciVariant = {
    .Type = FdtPciHostBcm2711,
    .RegisterLength = BCM_PCIE_REGISTER_LENGTH,
    .ConfigIndex = BCM_PCIE_CONFIG_INDEX,
    .ConfigData = BCM_PCIE_CONFIG_DATA,
    .Validate = __BcmValidateWindows,
    .Prepare = __Bcm2711Prepare,
    .ProgramInbound = __Bcm2711Inbound,
    .Start = __Bcm2711Start,
    .Stop = __Bcm2711Stop
};
