/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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

#include <internal/_all.h>
#include <internal/_utils.h>
#include <ddk/services/process.h>
#include <os/context.h>
#include <ddk/utils.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>
#include <fenv.h>

// Assembly entry points that handle the stack changes made
// by the stack system in the kernel
extern void __signalentry(void);

// The consequences of recieving the different signals
char signal_fatality[] = {
	0, /* 0? */
	1, /* SIGHUP     */
	1, /* SIGINT     */
	2, /* SIGQUIT    */
	2, /* SIGILL     */
	2, /* SIGTRAP    */
	2, /* SIGABRT    */
	2, /* SIGEMT     */
	2, /* SIGFPE     */
	1, /* SIGKILL    */
	2, /* SIGBUS     */
	2, /* SIGSEGV    */
	2, /* SIGSYS     */
	1, /* SIGPIPE    */
	1, /* SIGALRM    */
	1, /* SIGTERM    */
	0, /* SIGUSR1    */
	0, /* SIGUSR2    */
	0, /* SIGCHLD    */
	0, /* SIGPWR     */
	0, /* SIGWINCH   */
	0, /* SIGURG     */
	0, /* SIGPOLL    */
	3, /* SIGSTOP    */
	3, /* SIGTSTP    */
	0, /* SIGCONT    */
	3, /* SIGTTIN    */
	3, /* SIGTTOUT   */
	1, /* SIGVTALRM  */
	1, /* SIGPROF    */
	2, /* SIGXCPU    */
	2, /* SIGXFSZ    */
	0, /* SIGWAITING */
	1, /* SIGDIAF    */
	0, /* SIGHATE    */
	0, /* SIGWINEVENT*/
	0, /* SIGCAT     */
	0  /* SIGEND	 */
};

// Default interrupt handlers
static sig_element signal_list[] = {
    { SIGINT, "Interrupt (Ctrl-c)", SIG_DFL },
    { SIGILL, "Illegal instruction", SIG_DFL },
    { SIGFPE, "Erroneous arithmetic operation such as divide by zero", SIG_DFL },
    { SIGSEGV, "Invalid memory access (segmentation fault)", SIG_DFL },
    { SIGTERM, "Termination request", SIG_DFL },
    { SIGQUIT, "Interrupt (Ctrl+break)", SIG_DFL },
    { SIGABRT, "Abnormal termination", SIG_DFL },
    { SIGUSR1, "User-defined signal-1", SIG_IGN },
    { SIGUSR2, "User-defined signal-2", SIG_IGN }
};

static void
DefaultCrashHandler(
    _In_ sig_element* Signal,
    _In_ Context_t*   Context)
{
    // Not supported by modules
    if (!IsProcessModule()) {
        ProcessReportCrash(Context, Signal->signal);
    }
    
    // Last thing is to exit application
    ERROR("Received signal %i (%s). Terminating application", 
        Signal->signal, Signal->name);
    _Exit(EXIT_FAILURE);
}

static void
CreateSignalInformation(
    _In_ int Signal)
{
    // Handle math errors in any case
    if (Signal == SIGFPE) {
        int ExceptionType = fetestexcept(FE_ALL_EXCEPT);
        (void)ExceptionType;
    }
}

void
StdInvokeSignal(
    _In_ int        Signal,
    _In_ Context_t* Context)
{
    sig_element* sig = NULL;
    int          fatal;
    int          i;
    
    // 3 of the signals are relatively harmless and we can continue after
    fatal = Signal != SIGINT && Signal != SIGUSR1 && Signal != SIGUSR2;
    
    // Find handler
    for(i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == Signal) {
            sig = &signal_list[i];
            break;
        }
    }
    
    // Check against unsupported signal
    if (sig != NULL) {
        CreateSignalInformation(Signal);
        if (sig->handler != SIG_IGN) {
            if (sig->handler == SIG_DFL || sig->handler == SIG_ERR) {
                if (sig->handler == SIG_ERR ||
                    signal_fatality[Signal] == 1 || 
                    signal_fatality[Signal] == 2) {
                    DefaultCrashHandler(sig, Context);
                }
            }
            else {
                sig->handler(Signal);
            }
        }
    }

    // Unhandled signal, or division by zero specifically?
    if (sig == NULL) {
        sig_element _static_sig = { .signal = Signal };
        DefaultCrashHandler(&_static_sig, Context);
    }
    else if (fatal) {
        DefaultCrashHandler(sig, Context);
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

__signalhandler_t
signal(
    _In_ int               sig,
    _In_ __signalhandler_t func)
{
    __signalhandler_t temp;
    unsigned int      i;

    // Validate signal, currently we do not support POSIX
    // signal extensions
    switch (sig) {
        case SIGINT:
        case SIGILL:
        case SIGFPE:
        case SIGSEGV:
        case SIGTERM:
        case SIGQUIT:
        case SIGABRT:
        case SIGUSR1:
        case SIGUSR2:
            break;
        default: {
            _set_errno(EINVAL);
            return SIG_ERR;
        }
    }

    // Check specials
    if ((uintptr_t)func < 10 && func != SIG_DFL && func != SIG_IGN) {
        _set_errno(EINVAL);
        return SIG_ERR;
    }

    // Update handler list
    for(i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == sig) {
            temp = signal_list[i].handler;
            signal_list[i].handler = func;
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
    Context_t Empty = { 0 };
    
    switch (sig) {
        case SIGINT:
        case SIGILL:
        case SIGFPE:
        case SIGSEGV:
        case SIGTERM:
        case SIGQUIT:
        case SIGABRT:
        case SIGUSR1:
        case SIGUSR2:
            break;
        default:
            return -1;
    }
    
    StdInvokeSignal(sig, &Empty);
    return 0;
}
