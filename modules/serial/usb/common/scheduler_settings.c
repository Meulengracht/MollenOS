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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS MCore - USB Controller Scheduler
 * - Contains the implementation of a shared controller scheduker
 *   for all the usb drivers
 */
//#define __TRACE

/* Includes
 * - System */
#include <os/mollenos.h>
#include <ddk/utils.h>
#include "scheduler.h"

/* Includes
 * - Library */
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>

/* UsbSchedulerSettingsCreate
 * Initializes a new instance of the settings to customize the
 * scheduler. */
void
UsbSchedulerSettingsCreate(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ size_t                     FrameCount,
    _In_ size_t                     SubframeCount,
    _In_ size_t                     MaxBandwidthPerFrame,
    _In_ Flags_t                    Flags)
{
    // Sanitize
    assert(Settings != NULL);
    memset((void*)Settings, 0, sizeof(UsbSchedulerSettings_t));

    // Store initial configuration
    Settings->Flags                 = Flags;
    Settings->FrameCount            = FrameCount;
    Settings->SubframeCount         = SubframeCount;
    Settings->MaxBandwidthPerFrame  = MaxBandwidthPerFrame;
}

/* UsbSchedulerSettingsConfigureFrameList
 * Configure the framelist settings for the scheduler. This is always
 * neccessary to call if the controller is supplying its own framelist. */
void
UsbSchedulerSettingsConfigureFrameList(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ reg32_t*                   FrameList,
    _In_ uintptr_t                  FrameListPhysical)
{
    // Sanitize that we haven't specified to create it ourselves
    assert((Settings->Flags & USB_SCHEDULER_FRAMELIST) == 0);

    // Store configuration
    Settings->FrameList         = FrameList;
    Settings->FrameListPhysical = FrameListPhysical;
}

/* UsbSchedulerSettingsAddPool
 * Adds a new pool to the scheduler configuration that will get created
 * with the scheduler. */
void
UsbSchedulerSettingsAddPool(
    _In_ UsbSchedulerSettings_t*    Settings,
    _In_ size_t                     ElementSize,
    _In_ size_t                     ElementAlignment,
    _In_ size_t                     ElementCount,
    _In_ size_t                     ElementCountReserved,
    _In_ size_t                     LinkBreathMemberOffset,
    _In_ size_t                     LinkDepthMemberOffset,
    _In_ size_t                     ObjectMemberOffset)
{
    // Variables
    UsbSchedulerPool_t *Pool = NULL;

    // Sanitize that we don't create more pools than maximum
    assert(Settings->PoolCount < USB_POOL_MAXCOUNT);
    Pool = &Settings->Pools[Settings->PoolCount++];

    // Store configuration for pool
    Pool->ElementBaseSize           = ElementSize;
    Pool->ElementAlignedSize        = ALIGN(ElementSize, ElementAlignment, 1);
    Pool->ElementCount              = ElementCount;
    Pool->ElementCountReserved      = ElementCountReserved;
    Pool->ElementLinkBreathOffset   = LinkBreathMemberOffset;
    Pool->ElementDepthBreathOffset  = LinkDepthMemberOffset;
    Pool->ElementObjectOffset       = ObjectMemberOffset;
}
