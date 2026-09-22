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
    // The scheduler stores the selected start microframe(s) in FrameMask. EHCI
    // uses the lowest reserved bit as the first split, while any remaining
    // bits are either additional OUT start-splits or IN complete-splits.
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
    // Keep scheduler metadata separate from the hardware fields. The common
    // allocator initializes Object before this function runs, and clearing
    // the complete descriptor must not discard its pool links or reservation.
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

    // Preserve common scheduler ownership metadata while clearing hardware
    // fields. The split reservation itself is stored in EHCI-private fields.
    schedulerObject = siTD->Object;
    completionFrameOffset = siTD->CompletionFrameOffset;
    startBandwidth = siTD->StartBandwidth;
    completeBandwidth = siTD->CompleteBandwidth;
    completeMask = siTD->FrameCompletionMask;

    // A split isochronous TD carries one full/low-speed transaction and two
    // buffer pages. Refuse layouts that cannot be represented by the siTD
    // instead of truncating the physical buffer address.
    if (addresses == NULL || lengths == NULL || addresses[0] == 0 || lengths[0] == 0 ||
        lengths[0] > 0x3FF || frameMask == 0) {
        return false;
    }

    // EHCI stores the first page base separately from the byte offset. A
    // transfer may cross into exactly one following page, but a third page
    // would require another descriptor.
    page0 = addresses[0] & ~((uintptr_t)0xFFF);
    page1 = (addresses[0] + lengths[0] - 1) & ~((uintptr_t)0xFFF);
    if (page1 != page0 && page1 - page0 > 0x1000) {
        return false;
    }

    // OUT splits reserve one or more consecutive start-split microframes and
    // never use a complete-split; IN splits reserve a single start-split
    // followed by a complete-split window. Mixing the two here would make
    // the controller run complete-split transactions on an OUT endpoint or
    // silently truncate a multi-microframe OUT payload.
    startSubframe = __FirstSetSubframe(frameMask);
    startMask = (uint8_t)frameMask;
    if (isOut) {
        transactionCount = __PopCount8(startMask);
        completeMask = 0;
        // The hardware T-Count/Position fields only describe a single,
        // contiguous run of start-split microframes; reject anything else
        // rather than programming a schedule the TT cannot execute.
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

    // The first siTD word contains endpoint characteristics only. Transfer
    // length, active state, and IOC belong to the transfer-state word below.
    // Hub and port fields identify the transaction translator that converts
    // the high-speed split schedule into the full/low-speed bus transaction.
    siTD->Flags = EHCI_siTD_DEVADDR(transfer->Address.DeviceAddress) |
                  EHCI_siTD_EPADDR(transfer->Address.EndpointAddress) |
                  EHCI_siTD_HUBADDR(transfer->Address.HubAddress) |
                  EHCI_siTD_PORT(transfer->Address.PortAddress);
    if (pid == EHCI_siTD_IN) {
        siTD->Flags |= EHCI_siTD_IN;
    }

    // The active and IOC bits are published together only after all buffer
    // pointers have been populated. This prevents EHCI from fetching a
    // partially initialized descriptor.
    siTD->Status = EHCI_siTD_XFERLENGTH(lengths[0]) |
                   EHCI_siTD_ACTIVE |
                   (1u << 31);
    siTD->Bp0AndOffset = EHCI_siTD_BUFFER(page0) |
                         EHCI_siTD_OFFSET(addresses[0]);

    // T-Count and Transaction Position are only meaningful for OUT: they tell
    // the TT how many consecutive start-split bus transactions this single
    // descriptor represents. They are reserved for IN, where the transfer
    // length alone drives how many complete-splits are needed.
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

    // Keep an ownership-free copy of the initial state so periodic retries
    // can restore the descriptor without rebuilding the transfer metadata.
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

        // Resolve the DMA address through the scheduler rather than printing the
        // virtual descriptor address; EHCI consumes the physical link value.
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

    // The common manager may scan a descriptor more than once while processing
    // periodic transfers. Processed descriptors must not increment completion
    // counters a second time.
    if (siTD->Object.Flags & USB_ELEMENT_PROCESSED) {
        scanContext->ElementsExecuted++;
        return;
    }

    // EHCI reports the split transaction condition code in the low status
    // byte. An active bit means the controller still owns this descriptor.
    status = (uint8_t)(siTD->Status & 0xFF);
    if (status & EHCI_siTD_ACTIVE) {
        return;
    }

    // The status byte contains independent flags. Split-state and missed-
    // microframe bits are informational; only transaction, babble, and data
    // buffer errors change the transfer result.
    if (status & EHCI_TD_XACT) {
        scanContext->Result = USBTRANSFERCODE_NORESPONSE;
    } else if (status & EHCI_TD_BABBLE) {
        scanContext->Result = USBTRANSFERCODE_BABBLE;
    } else if (status & EHCI_TD_DATABUFERROR) {
        scanContext->Result = USBTRANSFERCODE_BUFFERERROR;
    }

    siTD->Object.Flags |= USB_ELEMENT_PROCESSED;
    scanContext->ElementsProcessed++;
    scanContext->ElementsExecuted++;
}

void
EHCISiTDRestart(
        _In_ EhciSplitIsochronousDescriptor_t* siTD)
{
    // Clear software completion state and restore every hardware-owned field
    // from the original snapshot before giving the siTD back to EHCI.
    siTD->Object.Flags &= ~USB_ELEMENT_PROCESSED;
    siTD->Flags = siTD->OriginalFlags;
    siTD->Status = siTD->OriginalStatus;
    siTD->Bp0AndOffset = siTD->OriginalBp0AndOffset;
    siTD->Bp1AndInfo = siTD->OriginalBp1AndInfo;
    dma_mb();
}
