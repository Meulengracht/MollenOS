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

#if defined(__i386__)
#include <i386/fenv.h>
#elif defined(__amd64__)
#include <amd64/fenv.h>
#endif

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

void
StdCrash(
    _In_ int Signal)
{
    Context_t Context = { 0 };

    // Retrieve the errornous context, and then report an application crash
    // this will only get the LAST errornous context, which means in cases
    // where the application itself raises an issue this is invalid
    if (Syscall_ThreadGetContext(&Context) != OsSuccess) {
        // Read our current context
        ERROR("Unable to get crash context for thread, invoked by application %i", Signal);
    }
    
    // Not supported by modules
    if (!IsProcessModule()) {
        ProcessReportCrash(&Context, Signal);
    }
}

/* StdSignalEntry
 * Default entry for all signal-handlers */
void
StdSignalEntry(
    _In_ int Signal)
{
    __signalhandler_t Handler = SIG_ERR;
    int               Fixed   = 0;
    int               i;
    
    // Find handler
    for(i = 0; i < sizeof(signal_list) / sizeof(signal_list[0]); i++) {
        if (signal_list[i].signal == Signal) {
            Handler = signal_list[i].handler;
            break;
        }
    }

    // Handle math errors in any case
    if (Signal == SIGFPE) {
        Fixed = fetestexcept(FE_ALL_EXCEPT);
    }

    // Sanitize handler, and then invoke if a user has registered one
    if (Handler != SIG_IGN && Handler != SIG_ERR) {
        if (Handler == SIG_DFL) {
            if (!Fixed && (signal_fatality[Signal] == 1 || 
                    signal_fatality[Signal] == 2)) {
                StdCrash(Signal);
                _Exit(EXIT_FAILURE);
            }
        }
        else {
            Handler(Signal);
        }
    }

    // Clear any outstanding math errors
    if (Signal == SIGFPE && Fixed) {
        return;
    }

    // Unhandled signal, or division by zero specifically?
    if (Handler == SIG_ERR || (Signal != SIGINT && Signal != SIGUSR1 && Signal != SIGUSR2)) {
        ERROR("Unhandled signal %i. Aborting application", Signal);
        StdCrash(Signal);
        _Exit(Signal);
    }
}

/* StdSignalInitialize
 * Initializes the default signal-handler for the process. */
void
StdSignalInitialize()
{
    // Install default handler
    if (Syscall_InstallSignalHandler(StdSignalEntry) != OsSuccess) {
        // Uhh?
    }
}

/* signal
 * Install a handler for the given signal. We allow handlers for
 * SIGINT, SIGSEGV, SIGTERM, SIGILL, SIGABRT, SIGFPE. */
__signalhandler_t
signal(
    _In_ int sig,
    _In_ __signalhandler_t func)
{
    // Variables
    __signalhandler_t temp;
    unsigned int i;

    // Validate signal
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

/* raise
 * Sends signal sig to the program. The signal handler, specified using signal(), is invoked.
 * If the user-defined signal handling strategy is not set using signal() yet, 
 * it is implementation-defined whether the signal will be ignored or default handler will be invoked. */
int
raise(
    _In_ int sig)
{
    // Validate which signals we can update
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

    // Use the std-signal-entry, it correctly calls the attached handler.
    StdSignalEntry(sig);
    return 0;
}
