#ifndef DEVICED_FIRMWARE_INTERRUPT_H
#define DEVICED_FIRMWARE_INTERRUPT_H
#include <os/osdefs.h>

/** A GIC interrupt resolved from a named firmware specifier. */
struct FdtInterrupt {
    uint32_t Controller;
    int Line;
    unsigned int Flags;
};

struct FdtResources;

/** @brief Interpret a GIC SPI specifier after resolving its provider. */
oserr_t
FdtGicInterrupt(
    _In_ const struct FdtResources* provider,
    _In_ const uint8_t* cells,
    _Out_ struct FdtInterrupt* interrupt);
#endif
