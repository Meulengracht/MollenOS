#include <ddk/interrupt.h>
#include <event.h>
#include <io.h>
#include <ioset.h>
#include <os/usched/job.h>
#include <time.h>

#include "devices.h"
#include "quiesce.h"

static int g_quiesceEventSet = -1;
static int g_quiesceEvent = -1;

static void
__DrainQuiesceRequests(void)
{
    DeviceInterruptQuiesceRequest_t request;
    oserr_t                         status;

    while (DeviceInterruptQuiesceNext(&request) == OS_EOK) {
        status = DmDeviceQuiesceInterrupts(&request);
        if (status != OS_EOK) {
            return;
        }
        status = DeviceInterruptQuiesceComplete(request.Token);
        if (status != OS_EOK) {
            return;
        }
    }
}

static void
__WaitForQuiesceWork(void)
{
    struct ioset_event events[1];
    struct timespec   deadline;
    int               count;

    timespec_get(&deadline, TIME_UTC);
    deadline.tv_sec++;
    count = ioset_wait(g_quiesceEventSet, events, 1, &deadline);
    for (int i = 0; i < count; i++) {
        if (events[i].data.iod == g_quiesceEvent) {
            uint64_t value;
            (void)read(g_quiesceEvent, &value, sizeof(value));
        }
    }
}

static void
__QuiesceWorker(
    _In_ void* unused,
    _In_ void* cancellationToken)
{
    (void)unused;
    while (!usched_is_cancelled(cancellationToken)) {
        __DrainQuiesceRequests();
        __WaitForQuiesceWork();
    }
}

oserr_t
DmInterruptQuiesceInitialize(void)
{
    struct usched_job_parameters parameters;
    uuid_t                       worker;
    oserr_t                      status;

    if (g_quiesceEventSet >= 0) {
        return OS_EEXISTS;
    }

    g_quiesceEventSet = ioset(0);
    g_quiesceEvent = eventd(0, EVT_RESET_EVENT);
    if (g_quiesceEventSet < 0 || g_quiesceEvent < 0) {
        status = OS_EUNKNOWN;
        goto cleanup;
    }

    status = ioset_ctrl(
        g_quiesceEventSet,
        IOSET_ADD,
        g_quiesceEvent,
        &(struct ioset_event){
            .events = IOSETSYN,
            .data.iod = g_quiesceEvent
        }
    );
    if (status < 0) {
        status = OS_EUNKNOWN;
        goto cleanup;
    }

    status = DeviceInterruptQuiesceRegister(g_quiesceEvent);
    if (status != OS_EOK) {
        goto cleanup;
    }

    usched_job_parameters_init(&parameters);
    parameters.detached = true;
    worker = usched_job_queue3(__QuiesceWorker, NULL, &parameters);
    if (worker == UUID_INVALID) {
        status = OS_EOOM;
        goto cleanup;
    }
    return OS_EOK;

cleanup:
    if (g_quiesceEvent >= 0) {
        close(g_quiesceEvent);
        g_quiesceEvent = -1;
    }
    if (g_quiesceEventSet >= 0) {
        close(g_quiesceEventSet);
        g_quiesceEventSet = -1;
    }
    return status;
}