/**
 * Copyright, Philip Meulengracht
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
 */

#include <ddk/io.h>
#include <string.h>
#include "xhci.h"

oserr_t
XhciRingInitialize(
    _In_    struct DMAContext* context,
    _Out_   XhciRing_t*        ring,
    _In_    uint16_t           trbCount)
{
    oserr_t oserr;

    memset(ring, 0, sizeof(XhciRing_t));
    if (trbCount < 2) {
        return OS_EINVALPARAMS;
    }

    oserr = XhciDmaAllocate(context, trbCount * sizeof(XhciTrb_t),
        &ring->BufferHandle, &ring->DeviceBase, (void**)&ring->Trbs);
    if (oserr != OS_EOK) {
        return oserr;
    }

    ring->TrbCount     = trbCount;
    ring->CycleState   = 1;
    memset(ring->Trbs, 0, trbCount * sizeof(XhciTrb_t));
    XhciRingReset(ring);
    return OS_EOK;
}

oserr_t
XhciRingDestroy(
    _In_ XhciRing_t* ring)
{
    oserr_t status;

    if (ring == NULL) {
        return OS_EOK;
    }
    status = DMAFree(&ring->BufferHandle);
    if (status != OS_EOK) {
        return status;
    }
    memset(ring, 0, sizeof(XhciRing_t));
    return OS_EOK;
}

void
XhciRingReset(
    _In_ XhciRing_t* ring)
{
    if (ring == NULL || ring->Trbs == NULL) {
        return;
    }

    memset(ring->Trbs, 0, ring->TrbCount * sizeof(XhciTrb_t));
    ring->EnqueueIndex      = 0;
    ring->DequeueIndex      = 0;
    ring->Used              = 0;
    ring->CycleState        = 1;
    ring->DequeueCycleState = 1;

    ring->Trbs[ring->TrbCount - 1].Parameter = ring->DeviceBase;
    ring->Trbs[ring->TrbCount - 1].Control = XHCI_TRB_CONTROL_TYPE(XHCI_TRB_TYPE_LINK) |
            XHCI_TRB_CONTROL_TOGGLE_CYCLE | XHCI_TRB_CONTROL_CYCLE;
}

oserr_t
XhciRingEnqueue(
    _In_    XhciRing_t*      ring,
    _In_    const XhciTrb_t* trb,
    _Out_   uint16_t*        trbIndexOut)
{
    uint32_t control;

    if (ring == NULL || ring->Trbs == NULL || trb == NULL || ring->TrbCount < 2) {
        return OS_EINVALPARAMS;
    }

    if (ring->Used >= (uint16_t)(ring->TrbCount - 1)) {
        return OS_EBUSY;
    }

    if (ring->EnqueueIndex == (uint16_t)(ring->TrbCount - 1)) {
        control = ring->Trbs[ring->EnqueueIndex].Control & ~XHCI_TRB_CONTROL_CYCLE;
        control |= ring->CycleState ? XHCI_TRB_CONTROL_CYCLE : 0;
        dma_wmb();
        WRITE_VOLATILE(ring->Trbs[ring->EnqueueIndex].Control, control);
        ring->EnqueueIndex = 0;
        ring->CycleState ^= 1;
    }

    // Publish cycle last: an active controller may fetch without a new doorbell.
    control = (trb->Control & ~XHCI_TRB_CONTROL_CYCLE) |
        (ring->CycleState ? XHCI_TRB_CONTROL_CYCLE : 0);
    ring->Trbs[ring->EnqueueIndex].Parameter = trb->Parameter;
    ring->Trbs[ring->EnqueueIndex].Status = trb->Status;
    dma_wmb();
    WRITE_VOLATILE(ring->Trbs[ring->EnqueueIndex].Control, control);
    if (trbIndexOut != NULL) {
        *trbIndexOut = ring->EnqueueIndex;
    }
    ring->EnqueueIndex++;
    ring->Used++;
    return OS_EOK;
}

void
XhciRingRelease(
    _In_    XhciRing_t* ring,
    _In_    uint16_t    trbCount)
{
    if (ring == NULL || ring->TrbCount < 2) {
        return;
    }

    if (trbCount > ring->Used) {
        trbCount = ring->Used;
    }
    ring->Used -= trbCount;
    while (trbCount--) {
        ring->DequeueIndex++;
        if (ring->DequeueIndex == (uint16_t)(ring->TrbCount - 1)) {
            ring->DequeueIndex = 0;
            ring->DequeueCycleState ^= 1;
        }
    }
}
