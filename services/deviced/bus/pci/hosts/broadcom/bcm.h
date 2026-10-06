#ifndef __DEVICED_BCM_PCI_H__
#define __DEVICED_BCM_PCI_H__

#include <bus/pci/bus.h>
#include <firmware/bcm.h>
#include <threads.h>

/** Chip policy and register layout. Validate runs before any controller writes.
 * Prepare owns reset and PHY setup; Start owns link policy and quirks. Stop must
 * tolerate a partially completed Prepare. */
struct BcmPciVariant {
    enum FdtPciHostType Type;
    size_t RegisterLength;
    size_t ConfigIndex;
    size_t ConfigData;
    oserr_t (*Validate)(const struct FdtPciHost*, unsigned int*);
    oserr_t (*Prepare)(PciHost_t*, const struct FdtPciHost*);
    oserr_t (*ProgramInbound)(PciHost_t*, const struct FdtPciHost*, unsigned int);
    oserr_t (*Start)(PciHost_t*, const struct FdtPciHost*);
    void (*Stop)(PciHost_t*);
};

/** Per-host configuration lock and firmware resources. The caller owns the
 * controller mapping and keeps the firmware blob alive until after destroy. */
struct BcmPciHost {
    struct FdtPciHost Firmware;
    const struct BcmPciVariant* Variant;
    mtx_t ConfigLock;
    int Ready;
    // BCM2712's bridge reset belongs to a separate firmware-described block.
    DeviceIo_t BridgeReset;
    uint32_t BridgeResetId;
    int BridgeResetMapped;
};

extern const struct BcmPciVariant Bcm2711PciVariant;
extern const struct BcmPciVariant Bcm2712PciVariant;
extern const struct PciHostOperations PciBcmOperations;

/** @brief Returns the exact chip variant, or NULL for other host types. */
extern const struct BcmPciVariant*
BcmPciGetVariant(
        _In_ enum FdtPciHostType type);

/** @brief Initializes a mapped controller and publishes operations after link-up. */
extern oserr_t
BcmPciInitialize(
        _InOut_ PciHost_t* bus,
        _Out_ struct BcmPciHost* controller,
        _In_ const struct FdtPciHost* firmware);

/** @brief Quiesces the controller after clients stop; the caller releases its mapping. */
extern void
BcmPciDestroy(
        _InOut_ PciHost_t* bus,
        _InOut_ struct BcmPciHost* controller);

/** @brief Converts a complete CPU DMA buffer to its DT-declared PCI bus address. */
extern oserr_t
BcmPciDmaAddress(
        _In_ const struct BcmPciHost* controller,
        _In_ uint64_t physical,
        _In_ uint64_t length,
        _Out_ uint64_t* address);

/** @brief Waits for a controller reset or link transition. */
extern oserr_t
BcmPciDelay(
        _In_ long milliseconds);

/** @brief Updates selected controller register bits during initialization or shutdown. */
extern oserr_t
BcmPciUpdate(
        _In_ PciHost_t* bus,
        _In_ size_t reg,
        _In_ uint32_t mask,
        _In_ uint32_t value);

/** @brief Checks that the controller is a root port with an active link. */
extern int
BcmPciLinkUp(
        _In_ PciHost_t* bus);

/**
 * @brief Maps a complete firmware-described register block, checking host limits.
 * @param io Receives an acquired mapping; the caller releases and destroys it.
 */
extern oserr_t
BcmPciMapRegisters(
        _Out_ DeviceIo_t* io,
        _In_ uint64_t base,
        _In_ uint64_t length);

/**
 * @brief Runs the shared resistor calibration with a bounded completion wait.
 * @param dependencies Resolved firmware resources; an absent RESCAL is a no-op.
 * @param reuseCompleted Keep completed calibration when another Pi 5 port uses it.
 */
extern oserr_t
BcmPciRescal(
        _In_ const struct FdtPciDependencies* dependencies,
        _In_ int reuseCompleted);

#endif
