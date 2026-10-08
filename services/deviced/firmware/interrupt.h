#ifndef DEVICED_FIRMWARE_INTERRUPT_H
#define DEVICED_FIRMWARE_INTERRUPT_H
#include <os/osdefs.h>

/** An interrupt handled by an Arm Generic Interrupt Controller (GIC).
 * Controller is the controller's firmware ID (phandle), Line is the GIC
 * interrupt number, and Flags describes how the interrupt signal is triggered. */
struct FdtInterrupt {
    uint32_t Controller;
    int Line;
    unsigned int Flags;
};

struct FdtResources;

/**
 * @brief Read a shared peripheral interrupt (SPI) description for an Arm GIC.
 *
 * @param provider Resources for the interrupt controller referenced by firmware.
 * @param cells Three 32-bit device-tree values: interrupt type, number, and trigger
 *              settings. The caller must check that all 12 bytes are available.
 * @param interrupt Receives the controller ID, GIC line, and settings on success.
 * @return OS_EOK on success, or OS_ENOTSUPPORTED for an unsupported controller,
 *         interrupt type, number, or trigger setting.
 */
oserr_t
FdtGicInterrupt(
    _In_ const struct FdtResources* provider,
    _In_ const uint8_t* cells,
    _Out_ struct FdtInterrupt* interrupt);
#endif
