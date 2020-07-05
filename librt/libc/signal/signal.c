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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
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
#include <os/context.h>
#include <signal.h>
#include <stdlib.h>

// Assembly entry points that handle the stack changes made
// by the stack system in the kernel
extern void __signalentry(void);

// The consequences of recieving the different signals
char signal_fatality[NUMSIGNALS] = {
	0, /* 0? */
	
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
    1, /* SIGIPC  */
};

// Default interrupt handlers
static sig_element signal_list[] = {
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
    { SIGSOCK, "Socket transmission error", SIG_DFL }
};

static void
DefaultCrashHandler(
    _In_ Context_t*   Context,
    _In_ sig_element* Signal,
    _In_ size_t       Flags)
{
    // Not supported by modules
    if (!IsProcessModule()) {
        OsStatus_t               osStatus;
        int                      status;
        struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetProcessService());

        status = svc_process_report_crash(GetGrachtClient(), &msg.base,
            *GetInternalProcessId(), Context, sizeof(Context_t), Signal->signal);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GetGrachtBuffer());
        svc_process_report_crash_result(GetGrachtClient(), &msg.base, &osStatus);
    }
    
    // Last thing is to exit application
    ERROR("Received signal %i (%s). Terminating application", 
        Signal->signal, Signal->name);
    _Exit(EXIT_FAILURE);
}

static void
CreateSignalInformation(
    _In_ int   Signal,
    _In_ void* Argument)
{
    // Handle math errors in any case
    if (Signal == SIGFPE) {
        int ExceptionType = fetestexcept(FE_ALL_EXCEPT);
        (void)ExceptionType;
    }
}

void
StdInvokeSignal(
    _In_ Context_t* Context,
    _In_ int        Signal,
    _In_ void*      Argument,
    _In_ size_t     Flags)
{
    sig_element* sig   = NULL;
    int          fatal = signal_fatality[Signal] || (Flags & SIGNAL_HARDWARE_TRAP);
    int          i;
    
    for (i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == Signal) {
            sig = &signal_list[i];
            break;
        }
    }
    
    // Check against unsupported signal
    if (sig != NULL) {
        CreateSignalInformation(Signal, Argument);
        if (sig->handler != SIG_IGN) {
            if (sig->handler == SIG_DFL || sig->handler == SIG_ERR) {
                DefaultCrashHandler(Context, sig, Flags);
            }
            else {
                __sa_process_t ext_handler = (__sa_process_t)sig->handler;
                ext_handler(Signal, Argument);
            }
        }
    }

    // Unhandled signal, or division by zero specifically?
    if (sig == NULL) {
        sig_element _static_sig = { 
            .signal  = Signal,
            .name    = "Unknown signal",
            .handler = NULL
        };
        DefaultCrashHandler(Context, &_static_sig, Flags);
    }
    else if (fatal) {
        DefaultCrashHandler(Context, sig, Flags);
    }
}

void
StdSignalInitialize()
{
    // Install default handler
    if (Syscall_InstallSignalHandler(__signalentry) != OsSuccess) {
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

__sa_process_t
sigprocess(int sig, __sa_process_t handler)
{
    return (__sa_process_t)signal(sig, (__sa_handler_t)handler);
}

int
raise(
    _In_ int sig)
{
    Context_t Context;
    
    if (sig <= 0 || sig >= NUMSIGNALS) {
        _set_errno(EINVAL);
        return -1;
    }
    
    GetContext(&Context);
    StdInvokeSignal(&Context, sig, NULL, 0);
    return 0;
}
