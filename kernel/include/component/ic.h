/**
 * Copyright 2018, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * MollenOS System Component Infrastructure
 * - The interrupt controller component connects interrupt requests to
 *   hardware controllers and routes them to the correct handler.
 */

#ifndef __COMPONENT_INTERRUPT_CONTROLLER__
#define __COMPONENT_INTERRUPT_CONTROLLER__

#include <os/osdefs.h>
#include <ddk/types/interrupt.h>
#include <interrupt_types.h>

// Maps from a source IRQ to its system line and flags.
typedef struct SystemInterruptOverride {
    unsigned int OverrideFlags;
    int          SourceLine;
    int          DestinationLine;
} SystemInterruptOverride_t;

typedef enum SystemInterruptControllerType {
    // Line controllers manage hardware interrupt lines and are usually found
    // through firmware.
    SystemInterruptControllerLine,
    // Message controllers provide MSI or MSI-X vectors that the kernel can
    // allocate to devices.
    SystemInterruptControllerMessage,
    // CPU-local and software interrupts can be registered before firmware
    // controllers are found.
    SystemInterruptControllerLocal
} SystemInterruptControllerType_t;

typedef struct SystemInterruptLineController {
    int       InterruptLineBase;
    int       NumberOfInterruptLines;
    uintptr_t MemoryAddress;
} SystemInterruptLineController_t;

// Message interrupts need a separately allocated message for each vector.
// These operations let each platform manage those messages in its own way.
struct InterruptMessageOperations {
    // Return the number of message slots this controller can provide.
    uint32_t (*Capacity)(void* context);
    
    // Reserve a message without enabling it or creating a handler record.
    // Fill in TableIndex, Token, and Message. The interrupt core assigns the
    // message to a handler slot.
    oserr_t (*AllocateOrdinal)(
        _In_    void*                          context,
        _In_    uint32_t                       ordinal,
        _InOut_ InterruptControllerResource_t* resource);
    
    // Enable or disable hardware delivery only. The interrupt core controls
    // which handler receives the message and synchronizes handler access.
    // Runs with local interrupts disabled while the interrupt core holds its
    // dispatch lock, which protects handler delivery. Do not block or call back
    // into the interrupt core. On failure, leave delivery off.
    oserr_t (*SetEnabled)(
        _In_ void*                                context,
        _In_ const InterruptControllerResource_t* resource,
        _In_ int                                  enable);
    
    // On failure, keep ownership so cleanup can be retried. Before a handler
    // or device can use a message, rollback must always succeed.
    // Retirement keeps the message reserved after its handler is removed, so
    // a late device message cannot reach another handler. Support a kernel
    // handler removing itself during an interrupt, and keep routing valid until
    // any in-progress interrupt has finished.
    oserr_t (*Release)(
        _In_ void*                                context,
        _In_ const InterruptControllerResource_t* resource,
        _In_ int                                  retire);
};

struct InterruptMessageBackend {
    struct InterruptMessageOperations Operations;
    void*                             Context;
};

typedef struct SystemInterruptMessageController {
    InterruptMessageControllerIdentity_t Source;
    struct InterruptMessageBackend     Backend;
} SystemInterruptMessageController_t;

typedef struct SystemInterruptController {
    uuid_t                          Id;
    SystemInterruptControllerType_t Type;
    union {
        SystemInterruptLineController_t    Line;
        SystemInterruptMessageController_t Message;
    };
    struct SystemInterruptController* Link;
} SystemInterruptController_t;

/**
 * @brief Initialize the system's interrupt infrastructure, including platform-specific setup.
 * This function must be called during kernel initialization before any interrupt handling occurs.
 *
 * @return OS_EOK on success, or an error code if initialization fails.
 */
KERNELAPI oserr_t KERNELABI
SystemInterruptsInitialize(void);

/**
 * @brief Registers a new line-based interrupt controller.
 *
 * @param id Hardware identity; zero is valid for a line controller.
 * @param interruptLineBase Base of the line interrupt range.
 * @param numberOfInterrupts Number of consecutive lines in the controller.
 * @param baseAddress Register address, which the architecture may map at boot.
 * @return OS_EOK or a validation/allocation error.
 */
KERNELAPI oserr_t KERNELABI
CreateInterruptController(
    _In_ uuid_t    id,
    _In_ int       interruptLineBase,
    _In_ int       numberOfInterrupts,
    _In_ uintptr_t baseAddress);

/**
 * @brief Iterate controllers of all types in discovery order.
 * 
 * @param previous Previous controller, or NULL to start iteration.
 * @return The next controller, or NULL.
 */
KERNELAPI SystemInterruptController_t* KERNELABI
GetNextInterruptController(
    _In_ SystemInterruptController_t* previous);

/**
 * @brief Initialize storage for firmware interrupt overrides.
 *
 * @param numberOfInterruptOverrides Number of entries to allocate.
 * @return OS_EOK or OS_EOOM.
 */
KERNELAPI oserr_t KERNELABI
CreateInterruptOverrides(
    _In_ int numberOfInterruptOverrides);

/**
 * @brief Register a firmware override in the next available entry.
 *
 * @param sourceInterruptLine Source IRQ.
 * @param destinationInterruptLine Routed system line.
 * @param interruptFlags Firmware polarity and trigger flags.
 * @return OS_EOK, OS_EUNKNOWN when full, or OS_ENOTSUPPORTED without ACPI.
 */
KERNELAPI oserr_t KERNELABI
RegisterInterruptOverride(
    _In_ int          sourceInterruptLine,
    _In_ int          destinationInterruptLine,
    _In_ unsigned int interruptFlags);

/**
 * @brief Resolve a system interrupt line to its controller's physical pin.
 *
 * @param interruptLine System line to resolve.
 * @return Controller-relative pin, or INTERRUPT_NONE if no line controller owns it.
 */
KERNELAPI int KERNELABI
GetPinOffsetByLine(
    _In_ int interruptLine);

/**
 * @brief Find the line controller that owns a system interrupt line.
 *
 * @param interruptLine System line to resolve.
 * @return Controller, or NULL. Message controllers are never returned.
 */
KERNELAPI SystemInterruptController_t* KERNELABI
GetInterruptControllerByLine(
    _In_ int interruptLine);

/**
 * @brief Register a message controller while the kernel discovers the platform.
 * The kernel copies the identity and operations. Keep Context alive until
 * shutdown. Undo allocations successfully before a handler or device can use
 * them. The interrupt core synchronizes handlers and keeps failed cleanup
 * available for retry. Callbacks run outside the component lock and must
 * protect their own state.
 *
 * @param identity Firmware identity for this message controller.
 * @param backend Initialized message operations supplied by kernel platform code.
 * @param idOut Receives a unique controller ID; unchanged on failure.
 * @return OS_EOK, OS_EEXISTS for duplicate identity, or an error.
 */
KERNELAPI oserr_t KERNELABI
CreateInterruptMessageController(
    _In_  const InterruptMessageControllerIdentity_t* identity,
    _In_  const struct InterruptMessageBackend*       backend,
    _Out_ uuid_t*                                     idOut);

/**
 * @brief Find a registered message controller without changing kernel state.
 * 
 * @param query Firmware identity to look up.
 * @param idOut Receives the controller ID on success.
 * @return OS_EOK, OS_ENOTSUPPORTED if absent, or OS_EINVALPARAMS.
 */
KERNELAPI oserr_t KERNELABI
InterruptMessageControllerResolve(
    _In_  const InterruptMessageControllerIdentity_t* query,
    _Out_ uuid_t*                                     idOut);

/**
 * @brief Find the message controller to use for an allocation.
 * 
 * @param controllerId Controller ID returned during platform discovery. Zero
 *        selects the platform controller for kernel handlers and no controller
 *        for user handlers.
 * @param kernelHandlers Set to nonzero when the handler runs in the kernel.
 * @param controllerOut Receives the controller, which remains valid until shutdown.
 * @return OS_EOK, OS_ENOTSUPPORTED for no controller, or OS_ENOENT for an invalid ID.
 */
KERNELAPI oserr_t KERNELABI
InterruptMessageControllerLookup(
    _In_  uuid_t                        controllerId,
    _In_  int                           kernelHandlers,
    _Out_ SystemInterruptController_t** controllerOut);

/**
 * @brief Return the number of message slots a controller can provide.
 * @param controller Message controller to query.
 * @return Number of slots, including slots that may already be reserved.
 */
KERNELAPI uint32_t KERNELABI
InterruptMessageControllerCapacity(
    _In_ SystemInterruptController_t* controller);

/**
 * @brief Find the controller and hardware route for a line or CPU-local interrupt.
 * @param descriptor Requested interrupt source. Firmware routing may update it.
 * @param flags Interrupt options. Use AllocateMessage for MSI or MSI-X requests.
 * @param resource Receives the route. This call does not reserve a handler slot.
 * @return OS_EOK or an error if the platform cannot route the interrupt.
 */
KERNELAPI oserr_t KERNELABI
InterruptControllerResolve(
    _InOut_ InterruptDescriptor_t*         descriptor,
    _In_    unsigned int                   flags,
    _Out_   InterruptControllerResource_t* resource);

/**
 * @brief Reserve a disabled message without creating a handler record.
 * @param controller Message controller to use.
 * @param ordinal Zero-based message slot, less than the controller's capacity.
 * @param flags Interrupt options.
 * @param resource Receives the message details. Release them if the interrupt
 *        core cannot reserve a handler slot.
 * @return OS_EOK or an allocation error. On failure, no resources remain reserved.
 */
KERNELAPI oserr_t KERNELABI
InterruptControllerAllocateMessage(
    _In_  SystemInterruptController_t*   controller,
    _In_  uint32_t                       ordinal,
    _In_  unsigned int                   flags,
    _Out_ InterruptControllerResource_t* resource);

/**
 * @brief Enable or disable hardware delivery for a reserved interrupt.
 * @param resource Routing details for the reserved interrupt.
 * @param enable Nonzero to enable delivery, or zero to disable it.
 * @return OS_EOK or a controller error. The interrupt core keeps ownership.
 */
KERNELAPI oserr_t KERNELABI
InterruptControllerSetEnabled(
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  enable);

/**
 * @brief Release a message or keep it reserved after removing its handler.
 * @param resource Routing details for the reserved message.
 * @param retire Nonzero keeps the message reserved so late device messages
 *        cannot reach another handler.
 * @return OS_EOK or an error. On failure, ownership stays in place for retry.
 */
KERNELAPI oserr_t KERNELABI
InterruptControllerRelease(
    _In_ const InterruptControllerResource_t* resource,
    _In_ int                                  retire);

#endif
