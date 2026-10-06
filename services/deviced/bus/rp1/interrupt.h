/** RP1 source control. MSI-X allocation and CPU routing belong to the parent. */
#ifndef __DEVICED_RP1_INTERRUPT_H__
#define __DEVICED_RP1_INTERRUPT_H__

#include "rp1.h"
#include <ddk/io.h>

/** Parent vectors are indexed by RP1 source number, never by a GIC line.
 * Mask must also wait for an in-flight handler before returning. Unmask may
 * deliver immediately. Both callbacks must succeed for an allocated vector.
 * The parent owns MSI-X table masking, message programming and vector lifetime.
 * All 61 vectors must be allocated before initialization; callers serialize
 * control operations and keep this object and MMIO alive until Destroy returns.
 */
struct Rp1InterruptParent {
    void* Context;
    unsigned int VectorCount;
    void (*MaskAndSynchronize)(void*, unsigned int);
    void (*Unmask)(void*, unsigned int);
};

typedef void (*Rp1InterruptHandler)(void*, unsigned int);

/** Per-instance source state, separate from the PCI host interrupt controller. */
struct Rp1InterruptController {
    DeviceIo_t* Registers;
    struct Rp1InterruptParent Parent;
    Rp1InterruptHandler Handler;
    void* Context;
    int Ready;
    uint8_t Types[FDT_RP1_INTERRUPT_COUNT];
    uint8_t Enabled[FDT_RP1_INTERRUPT_COUNT];
};

/** @brief Initializes source control with an acquired APBS mapping and masked,
 * allocated parent vectors. Does not allocate vectors or enable any source.
 * The output must be zero-initialized and may not already be active. A failed
 * initialization with Registers set retains masked ownership; call Destroy. */
extern oserr_t
Rp1InterruptInitialize(
    _Out_ struct Rp1InterruptController* controller,
    _In_ DeviceIo_t* registers,
    _In_ const struct Rp1InterruptParent* parent,
    _In_ Rp1InterruptHandler handler,
    _In_ void* context);

/** @brief Sets edge/level mode while the source is disabled. */
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

/** @brief Masks and drains the parent before disabling this source. */
extern oserr_t
Rp1InterruptDisable(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source);

/** @brief Dispatches a delivered parent vector and acknowledges a level source
 * after the child handler has cleared its device condition. Called by parent. */
extern oserr_t
Rp1InterruptHandle(
    _InOut_ struct Rp1InterruptController* controller,
    _In_ unsigned int source);

/** @brief Masks and drains all parent vectors before forgetting MMIO and handlers.
 * On failure the object remains owned and masked; retry before releasing MMIO. */
extern oserr_t
Rp1InterruptDestroy(
    _InOut_ struct Rp1InterruptController* controller);

#endif
