#include <bus/pci/host-private.h>
#include <bus/pci/registers.h>
#include <bus/pci/hosts/broadcom/bcm.h>
#include <bus/pci/hosts/broadcom/registers.h>

/**
 * @brief Check optional Pi 4 calibration and clock resources before resetting the host.
 *
 * Firmware may describe separate calibration hardware or a fixed-frequency clock.
 * Check those descriptions first. Pi 4 resets the host through the PCI controller
 * itself, so a description requiring a separate reset controller is rejected.
 */
static oserr_t
__BcmDependencies(
    _In_ const struct FdtPciHost* firmware)
{
    struct FdtPciDependencies dependencies;
    oserr_t                   status;

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
 * @brief Check the device-to-RAM address range and calculate its size setting.
 *
 * BCM2711 maps device accesses to RAM starting at CPU physical address zero.
 * Hardware needs a power-of-two size, so a firmware range of 3 GiB needs a
 * 4 GiB hardware mapping.
 * Only the programmed size is rounded; the firmware length remains the limit
 * used by BcmPciDmaAddress when checking actual buffers.
 */
static oserr_t
__BcmValidateWindows(
    _In_  const struct FdtPciHost* firmware,
    _Out_ unsigned int*            dmaOrder)
{
    const struct FdtPciWindow* window;
    uint64_t size;
    unsigned int order;

    // The host's BAR2 register defines the address range devices use to reach
    // RAM. This Pi 4 setup supports one such range; it cannot also map other
    // devices' registers or interrupt destinations.
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

    // Hardware requires a power-of-two size and a starting address that is
    // a multiple of that size. Also check that the last byte fits in 64 bits
    // before changing any controller registers.
    if ((size & (size - 1)) || (window->BusBase & (size - 1)) ||
        size - 1 > UINT64_MAX - window->BusBase) {
        return OS_ENOTSUPPORTED;
    }

    // Follow the reference driver: reject starting addresses strictly between
    // 2 GiB and 4 GiB. This area also needs space for device registers, so
    // meeting the size and alignment requirements alone is not enough.
    if (window->BusBase > BCM2711_INBOUND_RESTRICTED_BASE_START &&
        window->BusBase < BCM2711_INBOUND_RESTRICTED_BASE_END) {
        return OS_ENOTSUPPORTED;
    }

    // Find the exponent for the size: for example, 1 GiB = 2^30 gives 30.
    // Both BAR2 and the system-memory setting use a code based on this number.
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
 * The host controller must be running for calibration, then reset once to
 * return its internal state to defaults. Keep the connected device in reset
 * until its address mappings and connection settings are ready.
 */
static oserr_t
__Bcm2711Prepare(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware)
{
    oserr_t status;

    // Let the host controller run for calibration, but hold the connected
    // device in reset so it cannot communicate while setup is incomplete.
    status = BcmPciUpdate(
        bus,
        BCM2711_SW_INIT,
        BCM2711_RESET_ASSERT_BOTH,
        BCM2711_PERST_ASSERT
    );
    if (status != OS_EOK) {
        return status;
    }

    status = __BcmDependencies(firmware);
    if (status != OS_EOK) {
        return status;
    }

    // Reset the host controller's internal state while keeping the device in reset.
    status = BcmPciUpdate(
        bus,
        BCM2711_SW_INIT,
        BCM2711_RESET_ASSERT_BOTH,
        BCM2711_RESET_ASSERT_BOTH
    );
    if (status != OS_EOK) {
        return status;
    }

    BcmPciDelay(BCM_PCIE_RESET_SETTLE_MS);

    // Let the host controller run again; its address mappings are still to be set up.
    status = BcmPciUpdate(
        bus,
        BCM2711_SW_INIT,
        BCM2711_BRIDGE_RESET_ASSERT,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        return status;
    }

    // Power up the high-speed electrical interface. Disable device-controlled
    // clock requests and deeper link sleep while initial setup is in progress.
    status = BcmPciUpdate(
        bus,
        BCM2711_HARD_DEBUG,
        BCM_PCIE_DEBUG_SERDES_POWERDOWN | BCM2711_DEBUG_CLOCK_POWER_MASK,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        return status;
    }

    BcmPciDelay(BCM_PCIE_PHY_SETTLE_MS);

    // Allow devices to access system memory and set how read replies are split.
    // Select transfers of up to 128 bytes by clearing the burst-size field.
    // BcmPciUpdate also sets the requested enable bits, leaving other bits alone.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_MISC_CONTROL,
        BCM_PCIE_MISC_BURST_MASK,
        BCM2711_MISC_BURST_128_BYTES | BCM_PCIE_MISC_MEMORY_ENABLES
    );
    if (status != OS_EOK) {
        return status;
    }

    // A failed read returns all bits set, including the CPU-side controller bit.
    // Reject that value before checking the controller type.
    if (ReadDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_STATUS, sizeof(uint32_t)) ==
            BCM_PCIE_READ_FAILED) {
        return OS_ENOTSUPPORTED;
    }

    // This driver requires the CPU side of the connection (a root port).
    // Reject device-side mode before programming the host's address mappings.
    if (!(ReadDeviceIo(&bus->IoSpace, BCM_PCIE_LINK_STATUS, sizeof(uint32_t)) &
            BCM_PCIE_STATUS_ROOT_PORT)) {
        return OS_ENOTSUPPORTED;
    }
    return OS_EOK;
}

/**
 * @brief Set the address range devices use to reach RAM, using the host's BAR2.
 *
 * Earlier checks confirmed that RAM starts at CPU physical address zero and
 * the PCI starting address is a multiple of the chosen size. Disable unused
 * mappings so devices cannot use extra address ranges left over from firmware.
 */
static oserr_t
__Bcm2711Inbound(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware,
    _In_ unsigned int             dmaOrder)
{
    const struct FdtPciWindow* window;
    uint32_t                   sizeCode;
    oserr_t                    status;

    window = &firmware->DmaWindows[0];
    sizeCode = dmaOrder - BCM_PCIE_INBOUND_LARGE_SIZE_BIAS;

    // BAR1 is not the RAM window owned by this variant. Clear its size to
    // remove any extra mapping left by firmware.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_INBOUND_BAR1,
        BCM_PCIE_INBOUND_DISABLED,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // BAR3 is also unused; leaving it enabled could provide another route to RAM.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_INBOUND_BAR3,
        BCM_PCIE_INBOUND_DISABLED,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // Message-signaled interrupts (MSI) are raised by writing to an address.
    // They are not set up yet, so disable the old interrupt destination that
    // firmware may have left enabled.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_MSI_BAR,
        BCM_PCIE_MSI_DISABLED,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // Earlier checks confirmed that the starting address leaves the size bits
    // zero. Combine the lower 32 address bits with the size code to enable access.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_INBOUND_BAR2,
        (uint32_t)window->BusBase | sizeCode,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // Preserve the upper PCI address half: device-visible RAM may start above
    // 4 GiB even though its CPU physical destination is fixed at zero.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_INBOUND_BAR2 + BCM_PCIE_REGISTER_HIGH_OFFSET,
        window->BusBase >> BCM_PCIE_ADDRESS_HIGH_SHIFT,
        sizeof(uint32_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // Tell the system-memory side the same size as BAR2, so both sides of the
    // controller agree about the size of the RAM address range.
    return BcmPciUpdate(
        bus,
        BCM_PCIE_MISC_CONTROL,
        BCM2711_MISC_SCB0_SIZE_MASK,
        sizeCode << BCM2711_MISC_SCB0_SIZE_SHIFT
    );
}

/**
 * @brief Set Pi 4 connection settings, then allow the connected device to start.
 *
 * Address mappings are already installed. Use PCI Express generation 2 speed
 * (Gen2) and keep connection power saving disabled. After releasing device
 * reset, wait 100 ms before the shared code starts checking connection status.
 */
static oserr_t
__Bcm2711Start(
    _In_ PciHost_t*               bus,
    _In_ const struct FdtPciHost* firmware)
{
    uint32_t buses;
    uint16_t linkControl;
    oserr_t  status;

    // Assign the root bus, its immediate child, and the highest permitted bus.
    // Validation guarantees that the firmware range has room for a child bus.
    buses = firmware->BusStart |
            ((uint32_t)(firmware->BusStart + 1) << BCM_PCIE_SECONDARY_BUS_SHIFT) |
            ((uint32_t)firmware->BusEnd << BCM_PCIE_SUBORDINATE_BUS_SHIFT);

    // Identify the controller as a bridge connecting PCI buses. This tells
    // device discovery to look for devices on the bus behind it.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_CLASS_CODE,
        BCM_PCIE_CLASS_CODE_MASK,
        BCM_PCIE_CLASS_CODE_BRIDGE
    );
    if (status != OS_EOK) {
        return status;
    }

    // Keep PCI's byte order when BAR2 delivers data to RAM: little-endian
    // stores the least significant byte first. Do not swap bytes in the controller.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_VENDOR_CONTROL,
        BCM_PCIE_VENDOR_BAR2_ENDIAN_MASK,
        BCM_PCIE_VENDOR_BAR2_LITTLE_ENDIAN
    );
    if (status != OS_EOK) {
        return status;
    }

    // Report Gen2 as the maximum speed and disable support for the L0s/L1
    // sleep modes. Host and device power saving are not coordinated during setup.
    status = BcmPciUpdate(
        bus,
        BCM_PCIE_LINK_CAPABILITY,
        BCM_PCIE_LINK_CAP_ASPM_MASK | BCM_PCIE_LINK_CAP_SPEED_MASK,
        BCM_PCIE_LINK_SPEED_GEN2
    );
    if (status != OS_EOK) {
        return status;
    }

    // Read the 16-bit link-control word to preserve unrelated controls. Avoid
    // a wider access here, which would also touch the adjacent status word.
    linkControl = (uint16_t)ReadDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_LINK_CONTROL2,
        sizeof(uint16_t)
    );

    // Request the same Gen2 speed that the capability above advertises.
    status = WriteDeviceIo(
        &bus->IoSpace,
        BCM_PCIE_LINK_CONTROL2,
        (linkControl & ~BCM_PCIE_LINK_CONTROL2_SPEED_MASK) | BCM_PCIE_LINK_SPEED_GEN2,
        sizeof(uint16_t)
    );
    if (status != OS_EOK) {
        return status;
    }

    // Set the bus numbers before letting the device start, so configuration
    // requests reach the buses that device discovery will scan.
    status = BcmPciUpdate(bus, BCM_PCIE_BUS_NUMBERS, BCM_PCIE_BUS_NUMBERS_MASK, buses);
    if (status != OS_EOK) {
        return status;
    }

    // Clear PERST, the bit that holds the connected device in reset, to let it start.
    status = BcmPciUpdate(
        bus,
        BCM2711_SW_INIT,
        BCM2711_PERST_ASSERT,
        BCM_PCIE_DISABLED
    );
    if (status != OS_EOK) {
        return status;
    }

    // A device needs recovery time after reset even if link bits become set
    // immediately. Configuration access becomes available only after a later
    // status check confirms the connection is ready.
    BcmPciDelay(BCM_PCIE_RESET_CONFIG_WAIT_MS);
    return OS_EOK;
}

/**
 * @brief Hold both the Pi 4 bridge and its connected device in reset.
 *
 * This also handles a failed Prepare: registers are still mapped, and resetting
 * both sides prevents use of incomplete address mappings. Pi 4 does not need
 * to keep the host running to preserve calibration shared with other ports.
 */
static void
__Bcm2711Stop(
    _In_ PciHost_t* bus)
{
    // Set both reset bits to 1 to hold the host and device in reset; keep other bits.
    // Shutdown is best effort because this callback has no error return.
    (void)BcmPciUpdate(
        bus,
        BCM2711_SW_INIT,
        BCM2711_RESET_ASSERT_BOTH,
        BCM2711_RESET_ASSERT_BOTH
    );
}

// These callbacks provide Pi 4's reset, RAM mapping, and connection setup.
// The shared code handles selecting devices and accessing their configuration.
const struct BcmPciVariant g_bcm2711PciVariant = {
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
