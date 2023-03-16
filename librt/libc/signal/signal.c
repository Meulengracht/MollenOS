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

#define __TRACE

#include <assert.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <errno.h>
#include <gracht/link/vali.h>
#include <fenv.h>
#include <internal/_syscalls.h>
#include <internal/_tls.h>
#include <internal/_utils.h>
#include <os/context.h>
#include <os/threads.h>
#include <os/types/signal.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>

#include <sys_process_service_client.h>

typedef struct _sig_element {
    int 		   signal;
    const char*	   name;
    __sa_handler_t handler;
} sig_element;

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
    _In_ sig_element* signal)
{
    // When we enter the crash handler, we disable all async operations
    // as we don't want any other userspace threads to run on this XU
    __tls_current()->async_context = NULL;

    // Not supported by phoenix
    if (!__crt_is_phoenix()) {
        oserr_t               osStatus;
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());

        sys_process_report_crash(
                GetGrachtClient(), &msg.base,
                ThreadsCurrentId(),
                __crt_process_id(),
                (const uint8_t*)context,
                sizeof(Context_t),
                signal->signal
        );
        gracht_client_await(GetGrachtClient(), &msg.base, 0);
        sys_process_report_crash_result(GetGrachtClient(), &msg.base, &osStatus);
    }
    else {
        ERROR("Faulting address is 0x%" PRIxIN, CONTEXT_IP(context));
    }
    
    // Last thing is to exit application
    ERROR("Received signal %i (%s). Terminating application", signal->signal, signal->name);
    _Exit(EXIT_FAILURE);
}

static void
__CreateSignalInformation(
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

static sig_element*
__GetSignalHandler(
        _In_ int sigNo)
{
    for (int i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == sigNo) {
            return &signal_list[i];
        }
    }
    return NULL;
}

// Called by assembler functions in arch/_signal.s
void
StdInvokeSignal(
    _In_ Context_t* context,
    _In_ size_t     flags,
    _In_ void*      argument0,
    _In_ void*      argument1)
{
    sig_element* sig;
    int          sigNo = SIGNAL_FLAG_NO(flags);
    int          fatal = signal_fatality[sigNo] || (flags & SIGNAL_FLAG_HARDWARE_TRAP);

    if (sigNo == SIGSEGV) {
        // SPECIAL CASE MEMORY HANDLERS
        if (flags & SIGNAL_FLAG_PAGEFAULT) {
            enum OSPageFaultCode code = (enum OSPageFaultCode)(size_t)argument1;
            if (code == OSPAGEFAULT_RESULT_TRAP) {
                // A trap was hit, check our trap systems
                if (HandleMemoryMappingEvent(sigNo, argument0) == OS_EOK) {
                    return;
                }
            }
        }
    }

    // Check against unsupported signal
    sig = __GetSignalHandler(sigNo);
    if (sig != NULL) {
        __CreateSignalInformation(sigNo, argument0, argument1);
        if (sig->handler != SIG_IGN) {
            if (sig->handler == SIG_DFL || sig->handler == SIG_ERR) {
                __CrashHandler(context, sig);
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
        __CrashHandler(context, &_static_sig);
    } else if (fatal) {
        __CrashHandler(context, sig);
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
__signal_valid(
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

    if (__signal_valid(sig, (void *) handler) != 0) {
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

    flags = ((uint32_t)(sig & 0xFF) << 24);
    GetContext(&context);
    StdInvokeSignal(&context, flags, NULL, NULL);
    return 0;
}
