/** RP1 PCI function and its firmware-described internal bus. */
#ifndef __DEVICED_RP1_H__
#define __DEVICED_RP1_H__

#include <firmware/rp1.h>

#define RP1_VENDOR_ID 0x1de4
#define RP1_DEVICE_ID 0x0001
#define RP1_REVISION_C0 2
#define RP1_PCIE_APBS_OFFSET 0x108000U
#define RP1_PCIE_APBS_LENGTH 0x1000U

/** A child belongs to its RP1 PCI function. Its strings point into mapped
 * firmware data, which the PCI host keeps available for the child's lifetime. */
struct Rp1Child {
    struct Rp1Child* Next;
    uuid_t DeviceId;
    int BindingEnabled;
    struct FdtRp1Device Firmware;
};

/** Saved descriptions of RP1 child devices; no DMA or interrupt mappings exist
 * yet. The PCI host keeps the firmware data mapped while this object is alive. */
struct Rp1Bus {
    struct Rp1Child* Children;
    uuid_t DeviceId; // Borrowed endpoint ID; PCI owns its registry entry.
    unsigned int ChildCount;
    struct PciBar Bars[6];
    const struct FdtPciHost* Host;
};

struct PciDevice;

/** @brief Adds child device entries beneath the RP1 PCI function. If adding an
 * entry fails, PCI removes partial results before drivers are allowed to match.
 * Repeated calls keep the IDs already assigned to children. */
extern oserr_t
Rp1BusPublish(
    _InOut_ struct Rp1Bus* bus,
    _In_ const struct PciDevice* endpoint);

/** @brief Removes RP1 child entries without removing their PCI parent. First
 * stop their clients and remove any devices those clients added beneath them. */
extern oserr_t
Rp1BusUnpublish(
    _InOut_ struct Rp1Bus* bus);

/** @brief Allows drivers to match RP1 children after all PCI devices are listed. */
extern oserr_t
Rp1BusEnableBinding(
    _InOut_ struct Rp1Bus* bus);

/** @brief Reads all RP1 child descriptions from firmware, or frees partial results
 * and returns an error if any description cannot be read. */
extern oserr_t
Rp1BusCreate(
    _In_ const struct FdtPciHost* host,
    _In_ const struct PciBar* bars,
    _Out_ struct Rp1Bus** busOut);

/** @brief Frees child descriptions while the host's firmware data is still mapped. */
extern void
Rp1BusDestroy(
    _In_ struct Rp1Bus* bus);

#endif
