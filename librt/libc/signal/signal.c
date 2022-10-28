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
 * C-Support Signal Implementation
 * - Definitions, prototypes and information needed.
 */

#include <assert.h>
#include <ddk/utils.h>
#include <errno.h>
#include <fenv.h>
#include <internal/_all.h>
#include <internal/_ipc.h>
#include <internal/_syscalls.h>
#include <os/context.h>
#include <os/threads.h>
#include <os/types/syscall.h>
#include <os/usched/job.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

typedef void (*__sa_process_t)(int, void*, void*);

// Assembly entry points that handle the stack changes made by the stack system in the kernel
extern void __signalentry(void);

// Default handler for memory mapping events
extern oserr_t HandleMemoryMappingEvent(int, void*);

// The consequences of recieving the different signals
char signal_fatality[NUMSIGNALS] = {
    0, // what

	0, /* SIGINT  */
	1, /* SIGQUIT */
	1, /* SIGILL  */
	0, /* SIGTRAP */
	1, /* SIGABRT */
	1, /* SIGFPE  */
	1, /* SIGKILL */
	1, /* SIGSEGV */
	1, /* SIGPIPE */
	0, /* SIGUSR1 */
	0, /* SIGUSR2 */
	0, /* SIGALRM */
	1, /* SIGTERM */
	0, /* SIGURG  */
    1, /* SIGSOCK */
    1, /* SIGEXIT */
    1, /* SIGEXITQ */
};

// Default interrupt handlers
static sig_element signal_list[] = {
    { 0, "Invalid Signal", SIG_DFL },
    { SIGINT,  "Interrupt (Ctrl-c)", SIG_DFL },
    { SIGQUIT, "Interrupt (Ctrl+break)", SIG_DFL },
    { SIGILL,  "Illegal instruction", SIG_DFL },
    { SIGTRAP, "Trap instruction", SIG_DFL },
    { SIGABRT, "Abnormal termination", SIG_DFL },
    { SIGFPE,  "Erroneous arithmetic operation such as divide by zero", SIG_DFL },
    { SIGKILL, "Kill signal received", SIG_DFL },
    { SIGSEGV, "Invalid memory access (segmentation fault)", SIG_DFL },
    { SIGPIPE, "Pipe operation failure", SIG_DFL },
    { SIGUSR1, "User-defined signal-1", SIG_IGN },
    { SIGUSR2, "User-defined signal-2", SIG_IGN },
    { SIGALRM, "Alarm has expired", SIG_DFL },
    { SIGTERM, "Termination request", SIG_DFL },
    { SIGURG,  "Urgent request has arrived that must be handled", SIG_DFL },
    { SIGSOCK, "Socket transmission error", SIG_DFL },
    { SIGEXIT, "Exit requested by application", exit },
    { SIGEXITQ, "Quick exit requested by application", quick_exit }
};

static void __CrashHandler(
    _In_ Context_t*   context,
    _In_ sig_element* signal,
    _In_ size_t       flags)
{
    // Not supported by phoenix
    if (!__crt_is_phoenix()) {
        oserr_t               osStatus;
        int                      status;
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());

        status = sys_process_report_crash(GetGrachtClient(), &msg.base, ThreadsCurrentId(),
                                          *__crt_processid_ptr(), (const uint8_t*)context,
                                          sizeof(Context_t), signal->signal);
        if (status) {
            // @todo log and return
            return;
        }
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_process_report_crash_result(GetGrachtClient(), &msg.base, &osStatus);
    }
    else {
        ERROR("Faulting address is 0x%" PRIxIN, CONTEXT_IP(context));
    }
    
    // Last thing is to exit application
    ERROR("Received signal %i (%s). Terminating application",
          signal->signal, signal->name);
    _Exit(EXIT_FAILURE);
}

static void __CreateSignalInformation(
    _In_ int   signal,
    _In_ void* argument0,
    _In_ void* argument1)
{
    // Handle math errors in any case
    if (signal == SIGFPE) {
        int ExceptionType = fetestexcept(FE_ALL_EXCEPT);
        (void)ExceptionType;
    }
}

static void __HandleSystemCallCompletion(
        _In_ void* argument0,
        _In_ void* cancellationToken)
{
    OSSyscallContext_t* context = argument0;
    assert(context != NULL);
    _CRT_UNUSED(cancellationToken);

    usched_mtx_lock(&context->Mutex);
    usched_cnd_notify_one(&context->Condition);
    usched_mtx_unlock(&context->Mutex);
}

// Called by assembler functions in arch/_signal.s
void
StdInvokeSignal(
    _In_ Context_t* context,
    _In_ size_t     flags,
    _In_ void*      argument0,
    _In_ void*      argument1)
{
    sig_element* sig     = NULL;
    int          sigNo   = (int)((flags >> 16) & 0xFFFF);
    unsigned int options = flags & 0xFFFF;
    int          fatal   = signal_fatality[sigNo] || (options & SIGNAL_HARDWARE_TRAP);
    int          i;

    // SPECIAL CASE MEMORY HANDLERS
    if (sigNo == SIGSEGV && HandleMemoryMappingEvent(sigNo, argument0) == OS_EOK) {
        return;
    }
    if (sigNo == SIGSYSCALL) {
        usched_job_queue(__HandleSystemCallCompletion, argument0);
        return;
    }
    
    for (i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == sigNo) {
            sig = &signal_list[i];
            break;
        }
    }
    
    // Check against unsupported signal
    if (sig != NULL) {
        __CreateSignalInformation(sigNo, argument0, argument1);
        if (sig->handler != SIG_IGN) {
            if (sig->handler == SIG_DFL || sig->handler == SIG_ERR) {
                __CrashHandler(context, sig, options);
            } else {
                __sa_process_t ext_handler = (__sa_process_t)sig->handler;
                ext_handler(sigNo, argument0, argument1);
            }
        }
    }

    // Unhandled signal, or division by zero specifically?
    if (sig == NULL) {
        sig_element _static_sig = { 
            .signal  = sigNo,
            .name    = "Unknown signal",
            .handler = NULL
        };
        __CrashHandler(context, &_static_sig, options);
    }
    else if (fatal) {
        __CrashHandler(context, sig, options);
    }
}

void
StdSignalInitialize()
{
    // Install default handler
    if (Syscall_InstallSignalHandler(__signalentry) != OS_EOK) {
        assert(0);
    }
}

static int
is_signal_valid(
    _In_ int   sig,
    _In_ void* handler)
{
    // Validate signal, currently we do not support POSIX signal extensions
    if (sig <= 0 || sig >= NUMSIGNALS) {
        _set_errno(EINVAL);
        return -1;
    }
    
    // Check specials
    if ((uintptr_t)handler < 10 && handler != (void*)SIG_DFL && handler != (void*)SIG_IGN) {
        _set_errno(EINVAL);
        return -1;
    }
    return 0;
}

void sigsetzero(struct sigset* sigset)
{
    if (!sigset) {
        return;
    }

    memset(sigset, 0, sizeof(struct sigset));
}

void sigsetadd(struct sigset* sigset, int signal)
{
    if (!sigset) {
        return;
    }

    sigset->mask |= (1 << signal);
}

int signald(struct sigset* sigset, unsigned int flags)
{
    // @todo we need to build the protocol functions in process manager
    // signal_queue_create
    // signal_queue_post
    // signal_queue_wait [implemented as pipe_read]
    // signal_queue_destroy

    return 0;
}

__sa_handler_t
signal(
    _In_ int            sig,
    _In_ __sa_handler_t handler)
{
    __sa_handler_t temp;
    unsigned int   i;

    if (is_signal_valid(sig, (void*)handler) != 0) {
        return SIG_ERR;
    }

    // Update handler list
    for(i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == sig) {
            temp = signal_list[i].handler;
            signal_list[i].handler = handler;
            return temp;
        }
    }
    _set_errno(EINVAL);
    return SIG_ERR;
}

int
raise(
    _In_ int sig)
{
    Context_t context;
    size_t    flags;
    
    if (sig <= 0 || sig >= NUMSIGNALS) {
        _set_errno(EINVAL);
        return -1;
    }

    flags = ((uint32_t)sig << 16);
    GetContext(&context);
    StdInvokeSignal(&context, flags, NULL, NULL);
    return 0;
}
