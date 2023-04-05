/* MollenOS
 *
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
 *
 *  USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */
//#define __TRACE

#include <assert.h>
#include <ddk/barrier.h>
#include <usb/usb.h>
#include <ddk/utils.h>
#include <os/mollenos.h>
#include "scheduler.h"

oserr_t
UsbSchedulerChainElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ElementRootPool,
    _In_ uint8_t*        ElementRoot,
    _In_ int             ElementPool,
    _In_ uint8_t*        Element,
    _In_ uint16_t        Marker,
    _In_ int             Direction)
{
    UsbSchedulerObject_t* RootObject;
    UsbSchedulerObject_t* Object;
    UsbSchedulerPool_t*   RootPool;
    UsbSchedulerPool_t*   Pool;
    uintptr_t             PhysicalAddress;
    uint16_t              RootIndex;
    uint16_t              LinkIndex;

    assert(ElementRootPool < Scheduler->Settings.PoolCount);
    assert(ElementPool < Scheduler->Settings.PoolCount);

    // Debug
    TRACE("UsbSchedulerChainElement(Marker is 0x%x)", Marker);
    
    // Validate element and lookup pool
    RootPool        = &Scheduler->Settings.Pools[ElementRootPool];
    Pool            = &Scheduler->Settings.Pools[ElementPool];
    RootObject      = USB_ELEMENT_OBJECT(RootPool, ElementRoot);
    Object          = USB_ELEMENT_OBJECT(Pool, Element);
    PhysicalAddress = USB_ELEMENT_PHYSICAL(Pool, Object->Index);

    // Get indices
    RootIndex = RootObject->Index;
    LinkIndex = (Direction == USB_CHAIN_BREATH) ? RootObject->BreathIndex : RootObject->DepthIndex;
    TRACE(" > Root 0x%x, initial link of root is 0x%x. Index of new element is 0x%x", RootIndex, LinkIndex, Object->Index);

    // Iterate to marker, support cyclic queues
    while (LinkIndex != Marker && LinkIndex != RootIndex) {
        // Move to next object
        RootPool    = USB_ELEMENT_GET_POOL(Scheduler, LinkIndex);
        ElementRoot = USB_ELEMENT_INDEX(RootPool, LinkIndex);
        RootObject  = USB_ELEMENT_OBJECT(RootPool, ElementRoot);
        LinkIndex   = (Direction == USB_CHAIN_BREATH) ? RootObject->BreathIndex : RootObject->DepthIndex;
        TRACE(" > Current index is 0x%x, next index is 0x%x", RootObject->Index, LinkIndex);
    }
    
    // Append object
    // Set link of <Element> to be the link of <ElementRoot>
    // Set link of <ElementRoot> to be <Element>
    if (Direction == USB_CHAIN_BREATH)  Object->BreathIndex = RootObject->BreathIndex;
    else                                Object->DepthIndex  = RootObject->DepthIndex;
    USB_ELEMENT_LINK(Pool, Element, Direction) = USB_ELEMENT_LINK(RootPool, ElementRoot, Direction);
    dma_mb();
    if (Direction == USB_CHAIN_BREATH)  RootObject->BreathIndex = Object->Index;
    else                                RootObject->DepthIndex = Object->Index;
    USB_ELEMENT_LINK(RootPool, ElementRoot, Direction) = LODWORD(PhysicalAddress) | USB_ELEMENT_LINKFLAGS(Object->Flags);
    TRACE("Indices of existing element is (0x%x:0x%x), indices of new element are now (0x%x:0x%x)",
        RootObject->BreathIndex, RootObject->DepthIndex, Object->BreathIndex, Object->DepthIndex);

    return OS_EOK;
}

oserr_t
UsbSchedulerUnchainElement(
    _In_ UsbScheduler_t* Scheduler,
    _In_ int             ElementRootPool,
    _In_ uint8_t*        ElementRoot,
    _In_ int             ElementPool,
    _In_ uint8_t*        Element,
    _In_ int             Direction)
{
    UsbSchedulerObject_t* rootObject;
    UsbSchedulerObject_t* object;
    UsbSchedulerPool_t*   rootPool;
    UsbSchedulerPool_t*   pool;
    uint16_t rootIndex;
    uint16_t linkIndex;
    
    assert(ElementRootPool < Scheduler->Settings.PoolCount);
    assert(ElementPool < Scheduler->Settings.PoolCount);
    
    // Validate element and lookup pool
    rootPool   = &Scheduler->Settings.Pools[ElementRootPool];
    pool       = &Scheduler->Settings.Pools[ElementPool];
    rootObject = USB_ELEMENT_OBJECT(rootPool, ElementRoot);
    object     = USB_ELEMENT_OBJECT(pool, Element);

    // Get indices
    rootIndex = rootObject->Index;
    linkIndex = (Direction == USB_CHAIN_BREATH) ? rootObject->BreathIndex : rootObject->DepthIndex;

    // Iterate to end/start/object, support cyclic queues
    while (linkIndex != USB_ELEMENT_NO_INDEX && // Detect end of queue
           linkIndex != rootIndex &&    // Detect cyclic
           linkIndex != object->Index) // Detect object to unlink
    {
        rootPool    = USB_ELEMENT_GET_POOL(Scheduler, linkIndex);
        ElementRoot = USB_ELEMENT_INDEX(rootPool, linkIndex);
        rootObject  = USB_ELEMENT_OBJECT(rootPool, ElementRoot);
        linkIndex   = (Direction == USB_CHAIN_BREATH) ? rootObject->BreathIndex : rootObject->DepthIndex;
    }

    // Only one accepted case, LinkIndex == sObject->Index
    if (linkIndex == object->Index) {
        // Set link of <ElementRoot> to be the link of <Element>
        if (Direction == USB_CHAIN_BREATH) rootObject->BreathIndex = object->BreathIndex;
        else rootObject->DepthIndex = object->DepthIndex;
        USB_ELEMENT_LINK(rootPool, ElementRoot, Direction) = USB_ELEMENT_LINK(pool, Element, Direction);
        dma_wmb();
        return OS_EOK;
    }
    return OS_EUNKNOWN;
}
