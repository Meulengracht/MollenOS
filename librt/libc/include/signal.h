/* MollenOS
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
 * Standard C-Signal Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __SIGNAL_H__
#define __SIGNAL_H__

#include <os/osdefs.h>

#ifndef _SIG_ATOMIC_T_DEFINED
#define _SIG_ATOMIC_T_DEFINED
  typedef int sig_atomic_t;
#endif

/* 
 * Signal names (from the Unix specification on signals)
 */

// Supported Vali signals
#define SIGINT      1  /* External interrupt, usually initiated by the user */
#define SIGQUIT     2  /* Quit */
#define SIGILL      3  /* Illegal instruction */
#define SIGTRAP     4  /* A breakpoint or trace instruction has been reached */
#define SIGABRT     5  /* Abnormal termination condition, as is e.g. initiated by abort() */
#define SIGFPE      6  /* Erroneous arithmetic operation such as divide by zero */
#define SIGKILL     7  /* You have been stabbed repeated with a large knife */
#define SIGSEGV     8  /* Invalid memory access (segmentation fault) */
#define SIGPIPE     9  /* Attempted to read or write from a broken pipe */
#define SIGUSR1     10 /* User Defined Signal #1 */
#define SIGUSR2     11 /* User Defined Signal #2 */
#define SIGALRM     12 /* This is your wakeup call. */
#define SIGTERM     13 /* Termination request, sent to the program */
#define SIGURG      14 /* An URGENT! event (On a socket) */
#define SIGSOCK     15 /* Socket transmission error */
#define SIGIPC      16 /* IPC event */
#define NUMSIGNALS  17

// Unsupported Vali signals
#define SIGBUS      -1 /* Bus error (device error) */
#define SIGEMT      -1 /* Emulation trap XXX */
#define SIGHUP      -1 /* Hangup */
#define SIGSYS      -1 /* Bad system call */
#define SIGCHLD     -1 /* Child status report */
#define SIGPWR      -1 /* We need moar powah! */
#define SIGWINCH    -1 /* Your containing terminal has changed size */
#define SIGPOLL     -1 /* XXX OBSOLETE; socket i/o possible */
#define SIGSTOP     -1 /* Stopped (signal) */
#define SIGTSTP     -1 /* ^Z (suspend) */
#define SIGCONT     -1 /* Unsuspended (please, continue) */
#define SIGTTIN     -1 /* TTY input has stopped */
#define SIGTTOUT    -1 /* TTY output has stopped */
#define SIGVTALRM   -1 /* Virtual timer has expired */
#define SIGPROF     -1 /* Profiling timer expired */
#define SIGXCPU     -1 /* CPU time limit exceeded */
#define SIGXFSZ     -1 /* File size limit exceeded */
#define SIGWAITING  -1 /* Herp */
#define SIGDIAF     -1 /* Die in a fire */
#define SIGHATE     -1 /* The sending process does not like you */
#define SIGWINEVENT -1 /* Window server event */
#define SIGCAT      -1 /* Everybody loves cats */

#ifndef __SIGTYPE_DEFINED__
#define __SIGTYPE_DEFINED__
typedef	void (*__sa_handler_t)(int);
typedef void (*__sa_process_t)(int, void*);
#endif
#define SIG_DFL (__sa_handler_t)0
#define SIG_IGN (__sa_handler_t)1
#define SIG_ERR (__sa_handler_t)-1

/* We allow handlers for SIGINT, SIGSEGV, SIGTERM, SIGILL, SIGABRT, SIGFPE. */
_CODE_BEGIN
CRTDECL(__sa_process_t, sigprocess(int sig, __sa_process_t handler));
CRTDECL(__sa_handler_t, signal(int sig, __sa_handler_t handler));
CRTDECL(int,            raise(int sig));
_CODE_END

#endif //__SIGNAL_H__
