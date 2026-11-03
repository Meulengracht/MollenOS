/* MollenOS
 *
 * EHCI split-transaction isochronous descriptor support.
 */

#define __need_minmax
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "../ehci.h"
#include <string.h>

static uint8_t
__FirstSetSubframe(
        _In_ uint16_t frameMask)
{
    // FrameMask must contain the reserved start-split microframes.
    // The lowest set bit gives the first split.
    for (uint8_t i = 0; i < 8; i++) {
        if (frameMask & (1u << i)) {
            return i;
        }
    }
    return 0;
}

static uint8_t
__PopCount8(
        _In_ uint8_t mask)
{
    uint8_t count = 0;
    while (mask) {
        count += (uint8_t)(mask & 1);
        mask = (uint8_t)(mask >> 1);
    }
    return count;
}

bool
EHCISiTDInitialize(
        _In_ EhciController_t*                controller,
        _In_ UsbManagerTransfer_t*             transfer,
        _In_ EhciSplitIsochronousDescriptor_t* siTD,
        _In_ uint32_t                          pid,
        _In_ const uintptr_t*                  addresses,
        _In_ const uint32_t*                   lengths)
{
    // The allocator must initialize Object before this call. Save its pool
    // links and reservation before clearing the descriptor.
    UsbSchedulerObject_t schedulerObject;
    uintptr_t            page0;
    uintptr_t            page1;
    uint16_t             frameMask = siTD->Object.FrameMask;
    uint8_t              startSubframe;
    uint8_t              startMask;
    uint8_t              completeMask;
    uint8_t              transactionCount;
    bool                 isOut = (pid != EHCI_siTD_IN);
    uint8_t              completionFrameOffset;
    uint16_t             startBandwidth;
    uint16_t             completeBandwidth;

    // Save the scheduler data and the EHCI split reservation fields.
    schedulerObject = siTD->Object;
    completionFrameOffset = siTD->CompletionFrameOffset;
    startBandwidth = siTD->StartBandwidth;
    completeBandwidth = siTD->CompleteBandwidth;
    completeMask = siTD->FrameCompletionMask;

    // The siTD needs a nonempty buffer, a length that fits its 10-bit field,
    // and at least one reserved start-split microframe.
    if (addresses == NULL || lengths == NULL || addresses[0] == 0 || lengths[0] == 0 ||
        lengths[0] > 0x3FF || frameMask == 0) {
        return false;
    }

    // The buffer must fit in two pages. EHCI stores the first page base and
    // byte offset separately. A third page needs another descriptor.
    page0 = addresses[0] & ~((uintptr_t)0xFFF);
    page1 = (addresses[0] + lengths[0] - 1) & ~((uintptr_t)0xFFF);
    if (page1 != page0 && page1 - page0 > 0x1000) {
        return false;
    }

    // OUT needs consecutive start-split microframes and no complete-splits.
    // IN needs one start-split followed by a complete-split window.
    startSubframe = __FirstSetSubframe(frameMask);
    startMask = (uint8_t)frameMask;
    if (isOut) {
        transactionCount = __PopCount8(startMask);
        completeMask = 0;
        // T-Count/Position require consecutive start-split microframes.
        if (startMask != (uint8_t)(((1u << transactionCount) - 1) << startSubframe)) {
            return false;
        }
    } else {
        transactionCount = 1;
        if (completeMask == 0) {
            // A complete split must have at least one later microframe available.
            return false;
        }
    }

    memset(siTD, 0, sizeof(*siTD));
    siTD->Object = schedulerObject;
    siTD->CompletionFrameOffset = completionFrameOffset;
    siTD->StartBandwidth = startBandwidth;
    siTD->CompleteBandwidth = completeBandwidth;
    siTD->Object.Flags |= EHCI_LINK_siTD;
    siTD->FrameStartMask = startMask;
    siTD->FrameCompletionMask = completeMask;

    // Flags describes the endpoint. Hub and port select the transaction
    // translator. Transfer length, active state, and IOC go in Status below.
    siTD->Flags = EHCI_siTD_DEVADDR(transfer->Address.DeviceAddress) |
                  EHCI_siTD_EPADDR(transfer->Address.EndpointAddress) |
                  EHCI_siTD_HUBADDR(transfer->Address.HubAddress) |
                  EHCI_siTD_PORT(transfer->Address.PortAddress);
    if (pid == EHCI_siTD_IN) {
        siTD->Flags |= EHCI_siTD_IN;
    }

    // Finish setting the buffer pointers before linking this active siTD
    // into the schedule, so EHCI cannot read an incomplete descriptor.
    siTD->Status = EHCI_siTD_XFERLENGTH(lengths[0]) |
                   EHCI_siTD_ACTIVE |
                   (1u << 31);
    siTD->Bp0AndOffset = EHCI_siTD_BUFFER(page0) |
                         EHCI_siTD_OFFSET(addresses[0]);

    // For OUT, T-Count and Transaction Position describe the consecutive
    // start-splits. These fields are reserved for IN, where the transfer
    // length determines the number of complete-splits.
    siTD->Bp1AndInfo = EHCI_siTD_BUFFER(page1);
    if (isOut) {
        siTD->Bp1AndInfo |= EHCI_siTD_TCOUNT(transactionCount) | EHCI_siTD_POSITION_ALL;
    }
    siTD->BackPointer = EHCI_LINK_END;
    siTD->ExtBp0 = 0;
    siTD->ExtBp1 = 0;

#if __BITS == 64
    // EHCI keeps the low and high halves of the two buffer pointers in
    // separate fields when 64-bit addressing is enabled.
    if (controller->CParameters & EHCI_CPARAM_64BIT) {
        siTD->ExtBp0 = (reg32_t)(page0 >> 32);
        siTD->ExtBp1 = (reg32_t)(page1 >> 32);
    }
#endif

    // Save the initial hardware fields for periodic retries.
    // Scheduler data stays in Object.
    siTD->OriginalFlags = siTD->Flags;
    siTD->OriginalStatus = siTD->Status;
    siTD->OriginalBp0AndOffset = siTD->Bp0AndOffset;
    siTD->OriginalBp1AndInfo = siTD->Bp1AndInfo;
    dma_mb();
    return true;
}

    void
    EHCISiTDDump(
        _In_ EhciController_t*                 controller,
        _In_ EhciSplitIsochronousDescriptor_t* siTD)
    {
        uintptr_t physical = 0;

        // Get the physical descriptor address used by EHCI from the scheduler.
        UsbSchedulerGetPoolElement(
            controller->Base.Scheduler,
            EHCI_siTD_POOL,
            siTD->Object.Index & USB_ELEMENT_INDEX_MASK,
            NULL,
            &physical
        );
        WARNING("EHCI: siTD(0x%x), Link(0x%x), Flags(0x%x), SMask(0x%x), CMask(0x%x)",
            physical, siTD->Link, siTD->Flags,
            siTD->FrameStartMask, siTD->FrameCompletionMask);
        WARNING("      Status(0x%x), Bp0(0x%x), Bp1(0x%x), Back(0x%x)",
            siTD->Status, siTD->Bp0AndOffset, siTD->Bp1AndInfo,
            siTD->BackPointer);
    }

void
EHCISiTDVerify(
        _In_ struct HCIProcessReasonScanContext* scanContext,
        _In_ EhciSplitIsochronousDescriptor_t*   siTD)
{
    uint8_t status;

    // A periodic transfer may be scanned again after completion. Count the
    // descriptor as executed, but only add its bytes and process it once.
    if (siTD->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    // Wait for EHCI to clear the active bit before reading the result.
    // Split transaction status is in the low byte.
    status = (uint8_t)(siTD->Status & 0xFF);
    if (status & EHCI_siTD_ACTIVE) {
        return;
    }

    // Status bits are independent. Only transaction, babble, and data buffer
    // errors change the result here. Ignore split-state and missed-microframe.
    if (status & EHCI_TD_XACT) {
        scanContext->Result = USBTRANSFERCODE_NORESPONSE;
    } else if (status & EHCI_TD_BABBLE) {
        scanContext->Result = USBTRANSFERCODE_BABBLE;
    } else if (status & EHCI_TD_DATABUFERROR) {
        scanContext->Result = USBTRANSFERCODE_BUFFERERROR;
    }

    scanContext->BytesTransferred += ((siTD->OriginalStatus >> 16) & 0x3FF) -
        ((siTD->Status >> 16) & 0x3FF);
    siTD->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
EHCISiTDRestart(
        _In_ EhciSplitIsochronousDescriptor_t* siTD)
{
    // Clear the completion flag and restore the saved hardware fields
    // before scheduling the siTD again.
    siTD->Object.Flags &= ~USB_ELEMENT_PROCESSED;
    siTD->Flags = siTD->OriginalFlags;
    siTD->Status = siTD->OriginalStatus;
    siTD->Bp0AndOffset = siTD->OriginalBp0AndOffset;
    siTD->Bp1AndInfo = siTD->OriginalBp1AndInfo;
    dma_mb();
}
