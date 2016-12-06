/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
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
* MollenOS C Library - ERR NO
*/

#ifndef __ERRNO_H__
#define __ERRNO_H__

/* CPP Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* Includes */
#include <crtdefs.h>

/* Definitions */
#ifndef _ERRCODE_DEFINED
#define _ERRCODE_DEFINED
	typedef int errcode;
	typedef int errno_t;
#endif

/* These are the errno access 
 * macros and should be used for 
 * either accessing or setting the 
 * current errno */
_CRTIMP errno_t *__errno(void);
#define errno (*__errno())
#define _set_errno(err) (errno = err)

/* Please don't use these variables directly.
   Use strerror instead. */
extern const char * const _sys_errlist[];
extern int _sys_nerr;
#define __errno_r(ptr) ((ptr)->_errno)

#define EOK 0
#define	EPERM 1		/* Not super-user */
#define	ENOENT 2	/* No such file or directory */
#define	ESRCH 3		/* No such process */
#define	EINTR 4		/* Interrupted system call */
#define	EIO 5		/* I/O error */
#define	ENXIO 6		/* No such device or address */
#define	E2BIG 7		/* Arg list too long */
#define	ENOEXEC 8	/* Exec format error */
#define	EBADF 9		/* Bad file number */
#define	ECHILD 10	/* No children */
#define	EAGAIN 11	/* No more processes */
#define	ENOMEM 12	/* Not enough core */
#define	EACCES 13	/* Permission denied */
#define	EFAULT 14	/* Bad address */
#define	ENOTBLK 15	/* Block device required */
#define	EBUSY 16	/* Mount device busy */
#define	EEXIST 17	/* File exists */
#define	EXDEV 18	/* Cross-device link */
#define	ENODEV 19	/* No such device */
#define	ENOTDIR 20	/* Not a directory */
#define	EISDIR 21	/* Is a directory */
#define	EINVAL 22	/* Invalid argument */
#define	ENFILE 23	/* Too many open files in system */
#define	EMFILE 24	/* Too many open files */
#define	ENOTTY 25	/* Not a typewriter */
#define	ETXTBSY 26	/* Text file busy */
#define	EFBIG 27	/* File too large */
#define	ENOSPC 28	/* No space left on device */
#define	ESPIPE 29	/* Illegal seek */
#define	EROFS 30	/* Read only file system */
#define	EMLINK 31	/* Too many links */
#define	EPIPE 32	/* Broken pipe */
#define	EDOM 33		/* Math arg out of domain of func */
#define	ERANGE 34	/* Math result not representable */
#define	ENOMSG 35	/* No message of desired type */
#define	EIDRM 36	/* Identifier removed */
#define	ECHRNG 37	/* Channel number out of range */
#define	EL2NSYNC 38	/* Level 2 not synchronized */
#define	EL3HLT 39	/* Level 3 halted */
#define	EL3RST 40	/* Level 3 reset */
#define	ELNRNG 41	/* Link number out of range */
#define	EUNATCH 42	/* Protocol driver not attached */
#define	ENOCSI 43	/* No CSI structure available */
#define	EL2HLT 44	/* Level 2 halted */
#define	EDEADLK 45	/* Deadlock condition */
#define	ENOLCK 46	/* No record locks available */
#define EBADE 47	/* Invalid exchange */
#define EBADR 48	/* Invalid request descriptor */
#define EXFULL 49	/* Exchange full */
#define ENOANO 50	/* No anode */
#define EBADRQC 51	/* Invalid request code */
#define EBADSLT 52	/* Invalid slot */
#define EDEADLOCK 53	/* File locking deadlock error */
#define EBFONT 54	/* Bad font file fmt */
#define ENOSTR 55	/* Device not a stream */
#define ENODATA 56	/* No data (for no delay io) */
#define ETIME 57	/* Timer expired */
#define ENOSR 58	/* Out of streams resources */
#define ENONET 59	/* Machine is not on the network */
#define ENOPKG 60	/* Package not installed */
#define EREMOTE 61	/* The object is remote */
#define ENOLINK 62	/* The link has been severed */
#define EADV 63		/* Advertise error */
#define ESRMNT 64	/* Srmount error */
#define	ECOMM 65	/* Communication error on send */
#define EPROTO 66	/* Protocol error */
#define	EMULTIHOP 67	/* Multihop attempted */
#define	ELBIN 68	/* Inode is remote (not really error) */
#define	EDOTDOT 69	/* Cross mount point (not really error) */
#define EBADMSG 70	/* Trying to read unreadable message */
#define EFTYPE 71	/* Inappropriate file type or format */
#define ENOTUNIQ 72	/* Given log. name not unique */
#define EBADFD 73	/* f.d. invalid for this operation */
#define EREMCHG 74	/* Remote address changed */
#define ELIBACC 75	/* Can't access a needed shared lib */
#define ELIBBAD 76	/* Accessing a corrupted shared lib */
#define ELIBSCN 77	/* .lib section in a.out corrupted */
#define ELIBMAX 78	/* Attempting to link in too many libs */
#define ELIBEXEC 79	/* Attempting to exec a shared library */
#define ENOSYS 80	/* Function not implemented */
#define ENMFILE 81      /* No more files */
#define ENOTEMPTY 82	/* Directory not empty */
#define ENAMETOOLONG 83	/* File or path name too long */
#define ELOOP 84	/* Too many symbolic links */
#define EOPNOTSUPP 85	/* Operation not supported on transport endpoint */
#define EPFNOSUPPORT 86 /* Protocol mily not supported */
#define ECONNRESET 87  /* Connection reset by peer */
#define ENOBUFS 88	/* No buffer space available */
#define EAFNOSUPPORT 89 /* Address family not supported by protocol family */
#define EPROTOTYPE 90	/* Protocol wrong type for socket */
#define ENOTSOCK 91	/* Socket operation on non-socket */
#define ENOPROTOOPT 92	/* Protocol not available */
#define ESHUTDOWN 96	/* Can't send after socket shutdown */
#define ECONNREFUSED 97	/* Connection refused */
#define EADDRINUSE 98		/* Address already in use */
#define ECONNABORTED 99	/* Connection aborted */
#define ENETUNREACH 100		/* Network is unreachable */
#define ENETDOWN 101		/* Network interface is not configured */
#define ETIMEDOUT 102		/* Connection timed out */
#define EHOSTDOWN 103		/* Host is down */
#define EHOSTUNREACH 104	/* Host is unreachable */
#define EINPROGRESS 105		/* Connection already in progress */
#define EALREADY 106		/* Socket already connected */
#define EDESTADDRREQ 107	/* Destination address required */
#define EMSGSIZE 108		/* Message too long */
#define EPROTONOSUPPORT 109	/* Unknown protocol */
#define ESOCKTNOSUPPORT 110	/* Socket type not supported */
#define EADDRNOTAVAIL 111	/* Address not available */
#define ENETRESET 112		/* Internet connection was reset */
#define EISCONN 113		/* Socket is already connected */
#define ENOTCONN 114		/* Socket is not connected */
#define ETOOMANYREFS 115
#define EPROCLIM 116
#define EUSERS 117
#define EDQUOT 118
#define ESTALE 119
#define ENOTSUP 120		/* Not supported */
#define ENOMEDIUM 121   /* No medium (in tape drive) */
#define ENOSHARE 122    /* No such host or network path */
#define ECASECLASH 123  /* Filename exists with different case */
#define EILSEQ 124
#define EOVERFLOW 125	/* Value too large for defined data type */
#define ECANCELED 126	/* Operation canceled */
#define ENOTRECOVERABLE 127	/* State not recoverable */
#define EOWNERDEAD 128	/* Previous owner died */
#define ESTRPIPE 129	/* Streams pipe error */
#define EWOULDBLOCK EAGAIN	/* Operation would block */

#define __ELASTERROR 2000	/* Users can add values starting here */

#ifdef __cplusplus
}
#endif

#endif

