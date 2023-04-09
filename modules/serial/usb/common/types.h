/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __USB_COMMON_TYPES__
#define __USB_COMMON_TYPES__

// import types
typedef struct UsbManagerTransfer UsbManagerTransfer_t;

enum HCIProcessReason {
    // NONE is only used in instances where a reason code
    // does not apply. Should never be specified when using
    // HCIProcessElement as a callback.
    HCIPROCESS_REASON_NONE,
    // DUMP requests that the information of the queue element
    // to be dumped.
    HCIPROCESS_REASON_DUMP,
    // SCAN requests that the element provided be validated. Scan
    // will occur on hardware notifications, and the usb common code
    // needs to detect which transfers to act on.
    HCIPROCESS_REASON_SCAN,
    HCIPROCESS_REASON_RESET,
    HCIPROCESS_REASON_FIXTOGGLE,
    HCIPROCESS_REASON_LINK,
    HCIPROCESS_REASON_UNLINK,
    HCIPROCESS_REASON_CLEANUP
};

enum HCIProcessEvent {
    HCIPROCESS_EVENT_RESET_DONE
};

struct HCIProcessReasonScanContext {
    UsbManagerTransfer_t* Transfer;
    int                   ElementsExecuted;
    int                   ElementsProcessed;
    int                   LastToggle;
    bool                  Short;
    uint32_t              BytesTransferred;
    enum USBTransferCode  Result;
};

#endif //!__USB_COMMON_TYPES__
