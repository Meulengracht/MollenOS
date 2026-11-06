/**
 * MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * Universal Host Controller Interface Driver
 * Todo:
 * Power Management
 */

#ifndef __USB_UHCI__
#define __USB_UHCI__

#include <os/osdefs.h>

#include "../common/manager.h"
#include "../common/hci.h"

/* Controller setup constants. */
#define UHCI_MAX_PORTS                  7
#define UHCI_NUM_FRAMES                 1024
#define UHCI_FRAME_MASK                 2047
#define UHCI_USBLEGEACY                 0xC0
#define UHCI_USBRES_INTEL               0xC4

/* Register offsets in the controller's I/O space. */
#define UHCI_REGISTER_COMMAND           0x00
#define UHCI_REGISTER_STATUS            0x02
#define UHCI_REGISTER_INTR              0x04
#define UHCI_REGISTER_FRNUM             0x06
#define UHCI_REGISTER_FRBASEADDR        0x08
#define UHCI_REGISTER_SOFMOD            0x0C
#define UHCI_REGISTER_PORT_BASE         0x10

/* Command register bits. */
#define UHCI_COMMAND_RUN                0x1
#define UHCI_COMMAND_HCRESET            0x2
#define UHCI_COMMAND_GRESET             0x4
#define UHCI_COMMAND_SUSPENDMODE        0x8
#define UHCI_COMMAND_GLBRESUME          0x10
#define UHCI_COMMAND_DEBUG              0x20
#define UHCI_COMMAND_CONFIGFLAG         0x40
#define UHCI_COMMAND_MAXPACKET64        0x80

/* Status register bits. */
#define UHCI_STATUS_USBINT              0x1
#define UHCI_STATUS_INTR_ERROR          0x2
#define UHCI_STATUS_RESUME_DETECT       0x4
#define UHCI_STATUS_HOST_SYSERR         0x8
#define UHCI_STATUS_PROCESS_ERR         0x10
#define UHCI_STATUS_HALTED              0x20
#define UHCI_STATUS_INTMASK             0x1F

/* Interrupt register bits. */
#define UHCI_INTR_TIMEOUT               0x1
#define UHCI_INTR_RESUME                0x2
#define UHCI_INTR_COMPLETION            0x4
#define UHCI_INTR_SHORT_PACKET          0x8

/* Port register bits. */
#define UHCI_PORT_CONNECT_STATUS        0x1
#define UHCI_PORT_CONNECT_EVENT         0x2
#define UHCI_PORT_ENABLED               0x4
#define UHCI_PORT_ENABLED_EVENT         0x8
#define UHCI_PORT_LINE_STATUS           0x30
#define UHCI_PORT_RESUME_DETECT         0x40
#define UHCI_PORT_RESERVED              0x80
#define UHCI_PORT_LOWSPEED              0x100
#define UHCI_PORT_RESET                 0x200
#define UHCI_PORT_RESERVED1             0x400
#define UHCI_PORT_RESERVED2             0x800
#define UHCI_PORT_SUSPEND               0x1000

/* Link bits for transfer descriptors and queue head Link and Child fields. */
#define UHCI_LINK_END                   0x1
#define UHCI_LINK_QH                    0x2        // 1 => Qh, 0 => Td
#define UHCI_LINK_DEPTH                 0x4        // 1 => Depth, 0 => Breadth

/**
 * @brief Hardware transfer descriptor with scheduler data and saved transfer fields.
 * Descriptors must be aligned to 16 bytes.
 */
PACKED_TYPESTRUCT(UhciTransferDescriptor, {
    reg32_t                 Link;
    reg32_t                 Flags;
    reg32_t                 Header;
    reg32_t                 Buffer;

    // Software state.
    UsbSchedulerObject_t    Object;
    reg32_t                 OriginalFlags;
    reg32_t                 OriginalHeader;
});

/* Transfer descriptor Flags fields. */
#define UHCI_TD_LENGTH_MASK             0x7FF
#define UHCI_TD_MAXLENGTH               0x4FF
#define UHCI_TD_ACTIVE                  (1 << 23)
#define UHCI_TD_IOC                     (1 << 24)
#define UHCI_TD_ISOCHRONOUS             (1 << 25)
#define UHCI_TD_LOWSPEED                (1 << 26)
#define UHCI_TD_SETCOUNT(n)             ((n & 0x3) << 27)
#define UHCI_TD_SHORT_PACKET            (1 << 29)
#define UHCI_TD_ACTUALLENGTH(n)         (n & UHCI_TD_LENGTH_MASK)

#define UHCI_TD_GETCOUNT(n)             ((n >> 27) & 0x3)
#define UHCI_TD_STATUS(n)               ((n >> 17) & 0x3F)

/* Transfer descriptor Header fields. */
#define UHCI_TD_PID_SETUP               0x2D
#define UHCI_TD_PID_IN                  0x69
#define UHCI_TD_PID_OUT                 0xE1
#define UHCI_TD_DEVICE_ADDR(n)          ((n & 0x7F) << 8)
#define UHCI_TD_EP_ADDR(n)              ((n & 0xF) << 15)
#define UHCI_TD_DATA_TOGGLE             (1 << 19)
#define UHCI_TD_MAX_LEN(n)              ((n & UHCI_TD_LENGTH_MASK) << 21)
#define UHCI_TD_GET_LEN(n)              ((n >> 21) & UHCI_TD_LENGTH_MASK)

/**
 * @brief Hardware queue links with scheduler data and the selected queue index.
 */
PACKED_TYPESTRUCT(UhciQueueHead, {
    reg32_t Link;
    reg32_t Child;

    // Software state.
    UsbSchedulerObject_t Object;
    uint8_t              Queue;
});

/* Descriptor pool sizes, indices and alignment. */
#define UHCI_QH_ALIGNMENT                   16
#define UHCI_QH_POOL                        0
#define UHCI_QH_COUNT                       50

#define UHCI_TD_ALIGNMENT                   16
#define UHCI_TD_POOL                        1
#define UHCI_TD_COUNT                       400

/** 64 KiB pool for control and bulk packets that cross DMA segments. */
#define UHCI_STAGING_SLOTS                  1024

/* Fixed pool indices and the first index available for transfers. */
#define UHCI_POOL_QH_NULL                   0
#define UHCI_POOL_QH_ISOCHRONOUS            1
#define UHCI_POOL_QH_ASYNC                  9
#define UHCI_POOL_QH_LCTRL                  10
#define UHCI_POOL_QH_FCTRL                  11
#define UHCI_POOL_QH_FBULK                  12
#define UHCI_POOL_QH_START                  13

#define UHCI_POOL_TD_NULL                   0
#define UHCI_POOL_TD_START                  1

/**
 * @brief USB manager state and the current frame count for a UHCI controller.
 */
typedef struct _UhciController {
    UsbManagerController_t  Base;
    int                     Frame;
} UhciController_t;

/*******************************************************************************
 * Input/Output Methods
 *******************************************************************************/

/**
 * @brief Reads an 8-bit controller register.
 */
extern uint8_t
UhciRead8(
    _In_ UhciController_t*  Controller, 
    _In_ uint16_t           Register);

/**
 * @brief Reads a 16-bit controller register.
 */
extern uint16_t
UhciRead16(
    _In_ UhciController_t*  Controller, 
    _In_ uint16_t           Register);

/**
 * @brief Reads a 32-bit controller register.
 */
extern uint32_t
UhciRead32(
    _In_ UhciController_t*  Controller, 
    _In_ uint16_t           Register);

/**
 * @brief Writes an 8-bit controller register.
 */
extern void
UhciWrite8(
    _In_ UhciController_t*  Controller, 
    _In_ uint16_t           Register, 
    _In_ uint8_t            Value);

/**
 * @brief Writes a 16-bit controller register.
 */
extern void
UhciWrite16(
    _In_ UhciController_t*  Controller, 
    _In_ uint16_t           Register, 
    _In_ uint16_t           Value);

/**
 * @brief Writes a 32-bit controller register.
 */
extern void 
UhciWrite32(
    _In_ UhciController_t*  Controller, 
    _In_ uint16_t           Register, 
    _In_ uint32_t           Value);

/*******************************************************************************
 * Controller Methods
 *******************************************************************************/

/**
 * @brief Converts a UHCI condition code to a USB transfer status.
 */
extern enum USBTransferCode
UHCIErrorCodeToTransferStatus(
    _In_ int                        conditionCode);

/**
 * @brief Starts the controller.
 * @return OS_EOK on success.
 */
extern oserr_t
UhciStart(
    _In_ UhciController_t*          Controller,
    _In_ int                        Wait);

/**
 * @brief Stops the controller.
 * @return OS_EOK on success.
 */
extern oserr_t
UhciStop(
    _In_ UhciController_t*          Controller);

/**
 * @brief Resets the controller and leaves it stopped.
 */
extern oserr_t
UhciReset(
    _In_ UhciController_t*          Controller);

/**
 * @brief Initializes the controller's queue resources and resets its counters.
 */
extern oserr_t
UhciQueueInitialize(
    _In_ UhciController_t*          Controller);

/**
 * @brief Clears existing transfers and reinitializes the queues.
 */
extern oserr_t
UhciQueueReset(
    _In_ UhciController_t*          Controller);

/**
 * @brief Releases the descriptor pools and DMA resources.
 *
 * The controller must be stopped. If transfers remain, finish their cleanup
 * before calling again.
 *
 * @param controller The stopped controller to clean up.
 * @return OS_EOK on success, OS_EBUSY if transfers remain, or a scheduler cleanup error.
 */
extern oserr_t
UHCIQueueDestroy(
    _In_ UhciController_t* controller);

/**
 * @brief Resets the port and clears pending port events.
 */
extern oserr_t
UhciPortPrepare(
    _In_ UhciController_t*          Controller, 
    _In_ int                        Index);

/**
 * @brief Checks pending port events and reports connection changes to the USB service.
 */
extern oserr_t
UhciPortsCheck(
    _In_ UhciController_t*          Controller);

/**
 * @brief Updates the controller's stored frame count.
 * Call regularly to track changes in the hardware frame number.
 */
extern void
UhciUpdateCurrentFrame(
    _In_ UhciController_t*          controller);

/**
 * @brief Converts a transfer descriptor condition code to a status string index.
 */
extern int
UhciConditionCodeToIndex(
    _In_ int                        conditionCode);

/*******************************************************************************
 * Queue Head Methods
 *******************************************************************************/

/**
 * @brief Initializes a queue head and selects its transfer queue.
 *
 * @param controller The controller whose scheduler will run the transfer.
 * @param transfer The transfer to prepare the queue head for.
 * @param queueHead The allocated queue head to initialize.
 * @return OS_EOK on success, or an error if periodic bandwidth cannot be allocated.
 */
extern oserr_t
UHCIQHInitialize(
        _In_ UhciController_t*     controller,
        _In_ UsbManagerTransfer_t* transfer,
        _In_ UhciQueueHead_t*       queueHead);

/**
 * @brief Logs the queue head address, flags and links.
 */
extern void
UHCIQHDump(
    _In_ UhciController_t*          controller,
    _In_ UhciQueueHead_t*           queueHead);

/**
 * @brief Restarts an interrupt queue head at its first transfer descriptor.
 */
extern void
UHCIQHRestart(
    _In_ UhciController_t*          controller,
    _In_ UsbManagerTransfer_t*      transfer);

/**
 * @brief Links an async or interrupt queue head into the queue selected by its Queue field.
 */
extern void
UHCIQHLink(
    _In_ UhciController_t*          Controller,
    _In_ UhciQueueHead_t*           queueHead);

/**
 * @brief Unlinks an async or interrupt queue head from the queue selected by its Queue field.
 */
extern void
UHCIQHUnlink(
    _In_ UhciController_t*          controller,
    _In_ UhciQueueHead_t*           queueHead);

/*******************************************************************************
 * Transfer Descriptor Methods
 *******************************************************************************/

/**
 * @brief Initializes a setup transfer descriptor for execution.
 */
extern void
UHCITDSetup(
        _In_ UhciTransferDescriptor_t* td,
        _In_ uint32_t                  device,
        _In_ uint32_t                  endpoint,
        _In_ enum USBSpeed             speed,
        _In_ uintptr_t                 address);

/**
 * @brief Initializes a data transfer descriptor for execution.
 */
extern void
UHCITDData(
        _In_ UhciTransferDescriptor_t* td,
        _In_ enum USBTransferType      type,
        _In_ uint32_t                  pid,
        _In_ uint32_t                  device,
        _In_ uint32_t                  endpoint,
        _In_ enum USBSpeed             speed,
        _In_ uintptr_t                 address,
        _In_ uint32_t                  length,
        _In_ int                       toggle);

/**
 * @brief Logs the transfer descriptor fields.
 */
extern void
UHCITDDump(
    _In_ UhciController_t*          Controller,
    _In_ UhciTransferDescriptor_t*  Td);

/**
 * @brief Checks a transfer descriptor and updates the transfer's byte count and error status.
 */
extern void
UHCITDVerify(
        _In_ struct HCIProcessReasonScanContext* context,
        _In_ UhciTransferDescriptor_t*           td);

/**
 * @brief Resets a transfer descriptor's status for another transfer.
 * For interrupt transfers, also updates the buffer address within the circular buffer.
 */
extern void
UHCITDRestart(
        _In_ UhciController_t*         controller,
        _In_ UsbManagerTransfer_t*     transfer,
        _In_ UhciTransferDescriptor_t* td);

/*******************************************************************************
 * Transaction Methods
 *******************************************************************************/

/**
 * @brief Finishes preparing the transfer and queues it in the controller hardware.
 */
extern void
__DispatchTransfer(
    _In_ UhciController_t*          controller,
    _In_ UsbManagerTransfer_t*      transfer);

#endif // !__USB_UHCI__
