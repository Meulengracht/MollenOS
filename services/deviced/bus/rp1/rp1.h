/** RP1 PCI function and its firmware-described internal bus. */
#ifndef __DEVICED_RP1_H__
#define __DEVICED_RP1_H__

#include <firmware/rp1.h>

#define RP1_VENDOR_ID 0x1de4
#define RP1_DEVICE_ID 0x0001
#define RP1_REVISION_C0 2
#define RP1_PCIE_APBS_OFFSET 0x108000U
#define RP1_PCIE_APBS_LENGTH 0x1000U

/** A child remains owned by its PCI endpoint; firmware strings are borrowed. */
struct Rp1Child {
    struct Rp1Child* Next;
    uuid_t DeviceId;
    struct FdtRp1Device Firmware;
};

/** Inventory with no active DMA or interrupt mappings. The PCI host retains Blob. */
struct Rp1Bus {
    struct Rp1Child* Children;
    uuid_t DeviceId;
    unsigned int ChildCount;
    struct PciBar Bars[6];
    const struct FdtPciHost* Host;
};

struct PciDevice;

/** @brief Publishes owned parent/child descriptors, then enables compatible
 * matching. Idempotent; rolls back incomplete publication before any binding. */
extern oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus* bus,
    _In_ const struct PciDevice* endpoint);

/** @brief Removes children before their parent. The caller must first stop
 * clients and remove any descendants registered by peripheral drivers. */
extern void
Rp1BusUnpublish(
    _InOut_ struct Rp1Bus* bus);

/** @brief Builds the entire child inventory or frees it on failure. */
extern oserr_t
Rp1BusCreate(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar* bars,
    _Out_ struct Rp1Bus** busOut);

/** @brief Releases children before the host's firmware mapping is released. */
extern void
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus);

#endif
