/**
 * Copyright 2026, Philip Meulengracht
 */

#include <os/handle.h>
#include <os/shm.h>
#include <stdlib.h>
#include <string.h>
#include "xhci.h"

oserr_t
XhciRingInitialize(
    _Out_ XhciRing_t* ring,
    _In_  uint16_t    trbCount)
{
    oserr_t oserr;

    memset(ring, 0, sizeof(XhciRing_t));
    oserr = SHMCreate(
            &(SHM_t) {
                .Flags = SHM_DEVICE | SHM_PRIVATE | SHM_CLEAN,
                .Conformity = OSMEMORYCONFORMITY_LOW,
                .Size = trbCount * sizeof(XhciTrb_t),
                .Access = SHM_ACCESS_READ | SHM_ACCESS_WRITE
            },
            &ring->BufferHandle
    );
    if (oserr != OS_EOK) {
        return oserr;
    }

    oserr = SHMGetSGTable(&ring->BufferHandle, &ring->BufferSGTable, -1);
    if (oserr != OS_EOK) {
        OSHandleDestroy(&ring->BufferHandle);
        return oserr;
    }

    ring->Trbs         = SHMBuffer(&ring->BufferHandle);
    ring->PhysicalBase = ring->BufferSGTable.Entries[0].Address;
    ring->TrbCount     = trbCount;
    ring->CycleState   = 1;
    memset(ring->Trbs, 0, trbCount * sizeof(XhciTrb_t));
    return OS_EOK;
}

void
XhciRingDestroy(
    _In_ XhciRing_t* ring)
{
    if (ring == NULL) {
        return;
    }

    OSHandleDestroy(&ring->BufferHandle);
    free(ring->BufferSGTable.Entries);
    memset(ring, 0, sizeof(XhciRing_t));
}

void
XhciRingReset(
    _In_ XhciRing_t* ring)
{
    if (ring == NULL || ring->Trbs == NULL) {
        return;
    }

    memset(ring->Trbs, 0, ring->TrbCount * sizeof(XhciTrb_t));
    ring->EnqueueIndex = 0;
    ring->DequeueIndex = 0;
    ring->CycleState   = 1;
}

oserr_t
XhciRingEnqueue(
    _In_  XhciRing_t*      ring,
    _In_  const XhciTrb_t* trb,
    _Out_ uint16_t*        trbIndexOut)
{
    if (ring == NULL || ring->Trbs == NULL || trb == NULL) {
        return OS_EINVALPARAMS;
    }

    if (ring->EnqueueIndex >= ring->TrbCount) {
        ring->EnqueueIndex = 0;
        ring->CycleState ^= 1;
    }

    ring->Trbs[ring->EnqueueIndex] = *trb;
    ring->Trbs[ring->EnqueueIndex].Control &= ~XHCI_TRB_CONTROL_CYCLE;
    ring->Trbs[ring->EnqueueIndex].Control |= ring->CycleState ? XHCI_TRB_CONTROL_CYCLE : 0;
    if (trbIndexOut != NULL) {
        *trbIndexOut = ring->EnqueueIndex;
    }
    ring->EnqueueIndex++;
    return OS_EOK;
}
