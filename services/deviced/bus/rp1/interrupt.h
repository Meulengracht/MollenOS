/** Controls RP1 interrupt sources. The PCI parent allocates MSI-X vectors and
 * routes them to CPUs; this code only enables, disables, and acknowledges sources. */
#ifndef __DEVICED_RP1_INTERRUPT_H__
#define __DEVICED_RP1_INTERRUPT_H__

#include "rp1.h"
#include <ddk/io.h>

/** Vector number passed to the parent is the RP1 source number, not a CPU
 * interrupt number. MaskAndSynchronize must block new interrupts and wait for
 * any running handler to finish; Unmask may deliver an interrupt immediately.
 * The parent also programs the MSI-X table and owns the vectors. Allocate all
 * 61 vectors before initialization. Do not overlap control calls, and keep this
 * object and its mapped registers alive until Destroy succeeds.
 */
struct Rp1InterruptParent {
    void* Context;
    unsigned int VectorCount;
    void (*MaskAndSynchronize)(void*, unsigned int);
    void (*Unmask)(void*, unsigned int);
};

typedef void (*Rp1InterruptHandler)(void*, unsigned int);

/** Interrupt state for one RP1 device, kept separate from the PCI controller. */
struct Rp1InterruptController {
    DeviceIo_t* Registers;
    struct Rp1InterruptParent Parent;
    Rp1InterruptHandler Handler;
    void* Context;
    int Ready;
    uint8_t Types[FDT_RP1_INTERRUPT_COUNT];
    uint8_t Enabled[FDT_RP1_INTERRUPT_COUNT];
};

/** @brief Starts source control using an acquired APBS register mapping and
 * parent vectors that are allocated and masked. This does not allocate vectors
 * or enable sources. Start with a zero-initialized, inactive output object. If
 * initialization fails after setting Registers, call Destroy before releasing it. */
extern oserr_t
Rp1InterruptInitialize(
    _Out_ struct Rp1InterruptController* controller,
    _In_ DeviceIo_t* registers,
    _In_ const struct Rp1InterruptParent* parent,
    _In_ Rp1InterruptHandler handler,
    _In_ void* context);

/** @brief Selects whether the source signals on an edge or remains active as a level. */
extern oserr_t
Rp1InterruptConfigure(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source,
    _In_ unsigned int type);

/** @brief Enables a configured source after its child handler is ready. */
extern oserr_t
Rp1InterruptEnable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source);

/** @brief Blocks new parent interrupts and waits for any running handler before
 * disabling this source. */
extern oserr_t
Rp1InterruptDisable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source);

/** @brief Calls the child handler for a delivered source. For a level interrupt,
 * acknowledges it after the child handler clears the device's interrupt condition.
 * The PCI parent calls this function. */
extern oserr_t
Rp1InterruptHandle(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source);

/** @brief Blocks and drains parent interrupts before releasing this object's state.
 * If it fails, keep the object and mapped registers, and retry Destroy. */
extern oserr_t
Rp1InterruptDestroy(
    _InOut_ struct Rp1InterruptController* controller);

#endif
