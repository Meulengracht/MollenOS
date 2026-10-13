/**
 * Interrupt system call requests shared by libddk and the kernel.
 * These are separate from DeviceInterrupt_t: its pointers refer to library
 * bookkeeping, which the kernel must neither follow nor modify.
 */
#ifndef __DDK_INTERRUPT_REQUEST_H__
#define __DDK_INTERRUPT_REQUEST_H__

#include <ddk/interrupt.h>

/** Describes a reservation without supplying executable handler resources. */
typedef struct InterruptAllocationRequest {
    uuid_t                DeviceId;
    DeviceInterruptLine_t Line;
    unsigned int          Flags;
    uint32_t              Minimum;
    uint32_t              Optimal;
    uint32_t              Strategies;
} InterruptAllocationRequest_t;

/** Only these results are copied back after a successful reservation. */
typedef struct InterruptAllocationResult {
    uuid_t   Handle;
    uint32_t Count;
    uint32_t Strategy;
} InterruptAllocationResult_t;

/** Binds resources to one reserved vector without changing its routing. */
typedef struct InterruptBindRequest {
    uint32_t                 Index;
    InterruptResourceTable_t Resources;
} InterruptBindRequest_t;

#endif
