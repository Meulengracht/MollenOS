/**
 * Copyright 2026, Philip Meulengracht
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
 * Handles DMA events from the kernel, stops hosts in the right order and
 * retries cleanup that failed.
 */

#include <ddk/utils.h>
#include <internal/_syscalls.h>
#include <internal/_tls.h>
#include <os/handle.h>
#include <os/notification_queue.h>
#include <os/time.h>
#include <os/usched/job.h>
#include "dma-private.h"

static OSHandle_t g_dmaEventQueue;
static bool       g_dmaEventsRead;

static void
__DmaRecoveryFailed(
    _In_ const char* operation,
    _In_ oserr_t error)
{
    oserr_t status;

    ERROR("DMA recovery: %s failed: %u; DMA is unavailable", operation, error);
    
    g_dmDmaState.ManagerState = DmDmaManagerUnavailable;
    if (g_dmDmaState.Manager == UUID_INVALID) {
        return;
    }

    // Destroying the manager handle stops the kernel from accepting new DMA
    // requests as well as new service starts. Keep all bus leases and records,
    // because they may still be protecting running hardware.
    status = Syscall_HandleDestroy(g_dmDmaState.Manager);
    if (status != OS_EOK && status != OS_EINCOMPLETE && status != OS_ENOENT) {
        ERROR("DMA recovery: manager close failed: %u", status);
        return;
    }
    g_dmDmaState.Manager = UUID_INVALID;
}

static DmDmaAttachment_t*
__FindEventAttachment(
    _In_ uint64_t id)
{
    DmDmaAttachment_t* entry;

    for (entry = g_dmDmaState.Devices; entry != NULL; entry = entry->Next) {
        if (entry->Id.Value == id) {
            return entry;
        }
    }
    return NULL;
}

static void
__ReleaseIdleLeases(void)
{
    DmDmaAttachment_t* entry;

    for (entry = g_dmDmaState.Devices; entry != NULL; entry = entry->Next) {
        if (!entry->Binding.Idle ||
            (entry->Host->State != DmDmaHostReady && entry->Host->State != DmDmaHostStopped)) {
            continue;
        }
        DmDmaLeaseRelease(&entry->Binding.Lease);
        entry->Binding.Result = (OSDmaBindingResult_t) { 0 };
        entry->Binding.Idle = false;
    }
}

oserr_t
DmDmaDrainEventsLocked(void)
{
    OSDmaAttachmentEvent_t events[DMA_MANAGER_MAX_EVENTS];
    DmDmaAttachment_t*     entry;
    uint32_t              count;
    uint32_t              i;
    unsigned int          batches;
    oserr_t               status;

    g_dmaEventsRead = false;
    if (g_dmDmaState.ManagerState != DmDmaManagerReady) {
        return OS_ECANCELLED;
    }

    // Limit how much one call reads, in case a broken kernel keeps returning
    // the same event. The next wakeup or control call can read another batch.
    for (batches = 0; batches < DMA_MANAGER_MAX_ATTACHMENTS / DMA_MANAGER_MAX_EVENTS; batches++) {
        status = DMAManagerReadEvents(
            g_dmDmaState.Manager,
            events,
            DMA_MANAGER_MAX_EVENTS,
            &count
        );
        if (status != OS_EOK) {
            if (status != OS_EINTERRUPTED) {
                __DmaRecoveryFailed("event read", status);
            }
            return status;
        }
        g_dmaEventsRead |= count != 0;

        // Record every event before doing any more kernel work. The records
        // already exist, so a failed allocation cannot make us lose an event
        // that has already been read.
        for (i = 0; i < count; i++) {
            entry = __FindEventAttachment(events[i].Attachment);
            if (entry == NULL || entry->Host == NULL || events[i].Reserved != 0 ||
                (events[i].Reasons & ~(DMA_ATTACHMENT_IDLE | DMA_ATTACHMENT_STOP_REQUIRED |
                                      DMA_ATTACHMENT_CLEANUP_FAILED)) != 0) {
                __DmaRecoveryFailed("invalid event", OS_EPROTOCOL);
                return OS_EPROTOCOL;
            }

            if (events[i].Reasons & (DMA_ATTACHMENT_STOP_REQUIRED | DMA_ATTACHMENT_CLEANUP_FAILED)) {
                // The same event can arrive more than once. Do not move a host
                // back to StopPending if it is already further along, or if its
                // bus cannot stop DMA at all.
                if (entry->Host->State == DmDmaHostReady) {
                    entry->Host->State = DmDmaHostStopPending;
                }
            }

            if ((events[i].Reasons & DMA_ATTACHMENT_IDLE) && entry->Binding.Lease != NULL &&
                events[i].Generation == entry->Binding.Result.Generation) {
                entry->Binding.Idle = true;
            }
        }
        if (count < DMA_MANAGER_MAX_EVENTS) {
            break;
        }
    }

    // A stop event for another device on the same host may arrive in a later
    // batch. Read all batches before releasing any lease, so a host-wide stop
    // always wins over a single idle device.
    __ReleaseIdleLeases();
    return OS_EOK;
}

static void
__RetryDmaStop(
    _InOut_ DmDmaHost_t* host,
    _In_ const OSTimestamp_t* now)
{
    // A retry keeps both the original ticket and any stop that already worked.
    // The delay doubles on each failure and is capped at 30 seconds, so a
    // kernel cleanup that keeps failing is not retried in a tight loop.
    host->RetryDelay = host->RetryDelay == 0 ? 1 : host->RetryDelay * 2;
    if (host->RetryDelay > 30) {
        host->RetryDelay = 30;
    }
    host->RetryAt = *now;
    host->RetryAt.Seconds += host->RetryDelay;
}

static bool
__DmaStopPending(
    _In_ const DmDmaHost_t* host)
{
    return host->State == DmDmaHostStopPending ||
           host->State == DmDmaHostStopPrepared ||
           host->State == DmDmaHostHardwareStopped;
}

static void
__ProcessDmaStops(
    _In_ OSTimestamp_t* now)
{
    DmDmaHost_t*       host;
    DmDmaAttachment_t* entry;
    oserr_t           status;

    for (host = g_dmDmaState.Hosts; host != NULL; host = host->Next) {
        if (!__DmaStopPending(host) ||
            OSTimestampCompare(&host->RetryAt, now) > 0) {
            continue;
        }

        if (host->State == DmDmaHostStopPending) {
            for (entry = g_dmDmaState.Devices; entry != NULL; entry = entry->Next) {
                if (entry->Host == host && entry->Id.Value != 0) {
                    break;
                }
            }

            if (entry == NULL) {
                __DmaRecoveryFailed("stop attachment lookup", OS_ENOENT);
                return;
            }

            status = DMAAttachmentPrepareStop(
                g_dmDmaState.Manager,
                &entry->Id,
                &host->Ticket
            );
            if (status != OS_EOK) {
                WARNING("DMA recovery: host %u prepare stop failed: %u", host->Device, status);
                __RetryDmaStop(host, now);
                continue;
            }

            host->State = DmDmaHostStopPrepared;
        }

        if (host->State == DmDmaHostStopPrepared) {
            status = DmDeviceStopDma(host->Device);
            if (status != OS_EOK) {
                if (status == OS_ENOTSUPPORTED) {
                    WARNING("DMA recovery: host %u cannot stop its hardware; keeping its bus guards",
                            host->Device);
                    host->State = DmDmaHostStopUnsupported;
                } else {
                    WARNING("DMA recovery: host %u physical stop failed: %u", host->Device, status);
                    __RetryDmaStop(host, now);
                }
                continue;
            }

            host->State = DmDmaHostHardwareStopped;
        }

        status = DMAAttachmentCompleteStop(
            g_dmDmaState.Manager,
            &host->Ticket
        );
        if (status != OS_EOK) {
            WARNING("DMA recovery: host %u complete stop failed: %u", host->Device, status);
            __RetryDmaStop(host, now);
            continue;
        }

        host->State = DmDmaHostStopped;
        host->RetryDelay = 0;
    }
    __ReleaseIdleLeases();
}

static void
__DmaRecoveryJob(
    _In_ void* argument,
    _In_ void* cancellationToken)
{
    struct ioset_event event;
    OSAsyncContext_t*  asyncContext = __tls_current()->async_context;
    DmDmaHost_t*       host;
    OSTimestamp_t      now;
    OSTimestamp_t      deadline;
    OSTimestamp_t*     waitUntil;
    oserr_t            status;
    int                eventCount;
    bool               eventsRead;
    struct timespec    wakeAt;

    (void)argument;
    (void)cancellationToken;

    for (;;) {
        usched_mtx_lock(&g_dmDmaLock);
        if (g_dmDmaState.ManagerState != DmDmaManagerReady) {
            usched_mtx_unlock(&g_dmDmaLock);
            return;
        }
        
        // Read events before the first wait and after every wakeup. The queue
        // only says something may have happened. The real work is in the
        // manager's event records.
        status = DmDmaDrainEventsLocked();
        if (status != OS_EOK) {
            usched_mtx_unlock(&g_dmDmaLock);
            if (status == OS_EINTERRUPTED) {
                usched_job_yield();
                continue;
            }
            return;
        }

        status = OSGetTime(OSTimeSource_UTC, &now);
        if (status != OS_EOK) {
            __DmaRecoveryFailed("clock read", status);
            usched_mtx_unlock(&g_dmDmaLock);
            return;
        }

        __ProcessDmaStops(&now);
        if (g_dmDmaState.ManagerState != DmDmaManagerReady) {
            usched_mtx_unlock(&g_dmDmaLock);
            return;
        }

        eventsRead = g_dmaEventsRead;
        waitUntil = NULL;

        for (host = g_dmDmaState.Hosts; host != NULL; host = host->Next) {
            if (!__DmaStopPending(host)) {
                continue;
            }

            if (waitUntil == NULL || OSTimestampCompare(&host->RetryAt, &deadline) < 0) {
                deadline = host->RetryAt;
                waitUntil = &deadline;
            }
        }
        usched_mtx_unlock(&g_dmDmaLock);

        if (eventsRead) {
            // The kernel keeps handing out events it has already given us, even
            // after a successful read. Sleeping for a second stops us from
            // spinning on full or partial batches and on duplicate events.
            // Control calls can still read events while we sleep.
            wakeAt.tv_sec = now.Seconds + 1;
            wakeAt.tv_nsec = now.Nanoseconds;
            if (usched_job_sleep(&wakeAt) != 0) {
                usched_mtx_lock(&g_dmDmaLock);
                __DmaRecoveryFailed("event backoff", OS_EUNKNOWN);
                usched_mtx_unlock(&g_dmDmaLock);
                return;
            }
            continue;
        }

        if (asyncContext != NULL) {
            OSAsyncContextInitialize(asyncContext);
        }

        status = OSNotificationQueueWait(
            &g_dmaEventQueue,
            &event,
            1,
            0,
            waitUntil,
            &eventCount,
            asyncContext
        );
        if (status != OS_EOK && status != OS_ETIMEOUT && status != OS_EINTERRUPTED) {
            usched_mtx_lock(&g_dmDmaLock);
            __DmaRecoveryFailed("queue wait", status);
            usched_mtx_unlock(&g_dmDmaLock);
            return;
        }
    }
}

oserr_t
DmDmaRecoveryInitialize(
    _In_ uuid_t manager)
{
    OSHandle_t         handle = { .ID = manager };
    struct ioset_event event = { .events = IOSETIN };
    oserr_t            status;
    oserr_t            cleanup;
    uuid_t             job;

    status = OSNotificationQueueCreate(0, &g_dmaEventQueue);
    if (status != OS_EOK) {
        ERROR("DmDmaRecoveryInitialize: queue creation failed: %u", status);
        return status;
    }

    status = OSNotificationQueueCtrl(
        &g_dmaEventQueue,
        IOSET_ADD,
    
        &handle,
        &event
    );
    if (status != OS_EOK) {
        ERROR("DmDmaRecoveryInitialize: manager subscription failed: %u", status);
        goto failure;
    }
    
    job = usched_job_queue(__DmaRecoveryJob, NULL);
    if (job == UUID_INVALID) {
        ERROR("DmDmaRecoveryInitialize: failed to queue event worker");
        status = OS_EOOM;
        goto failure;
    }
    return OS_EOK;

failure:
    cleanup = OSHandleRelease(&g_dmaEventQueue);
    if (cleanup != OS_EOK) {
        ERROR("DmDmaRecoveryInitialize: queue rollback failed: %u", cleanup);
    }
    return status;
}
