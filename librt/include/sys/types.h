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
* MollenOS C Library - POSIX definitions for comp
*/

#ifndef __SYS_TYPES__
#define __SYS_TYPES__

//Includes
#include <os/osdefs.h>

#ifndef _TIME32_T_DEFINED
#define _TIME32_T_DEFINED
  typedef long __time32_t;
#endif
#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
#if _INTEGRAL_MAX_BITS >= 64
  typedef long long __time64_t;
#endif
#endif
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#ifdef _USE_32BIT_TIME_T
  typedef __time32_t time_t;
#else
  typedef __time64_t time_t;
#endif
#endif

#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__
typedef unsigned char       u_int8_t;
typedef unsigned short     u_int16_t;
typedef unsigned int       u_int32_t;
typedef unsigned long long u_int64_t;
#endif

/* bsd */
typedef unsigned char           u_char;
typedef unsigned short          u_short;
typedef unsigned int            u_int;
typedef unsigned long           u_long;

/* sysv */
typedef unsigned char           unchar;
typedef unsigned short          ushort;
typedef unsigned int            uint;
typedef unsigned long           ulong;

/* Used in file handles for offsets */
#ifndef OFF_T_DEFINED
#if _FILE_OFFSET_BITS==64
	typedef int64_t off_t;
	typedef int64_t off_t;
#else
	typedef long off_t;
	typedef long long off64_t;
#endif
#define OFF_T_DEFINED
#endif 
typedef unsigned short nlink_t;

/* i-node number (not used on DOS) */
typedef unsigned short _ino_t;

/* Non-ANSI name for compatibility */
typedef unsigned short ino_t;

/* device code */
typedef unsigned int _dev_t;

/* Non-ANSI name for compatibility */
typedef unsigned int dev_t;

/* Node type */
typedef unsigned long mode_t;

/* Id types */
typedef unsigned long uid_t;
typedef unsigned long gid_t;
typedef long pid_t;

/* Block size */
typedef	long blksize_t;

/* Count of file blocks */
typedef long blkcnt_t;

/* Count of file system blocks */
typedef unsigned long fsblkcnt_t;

/* Count of files */
typedef unsigned long fsfilcnt_t;

/* Math.h */
typedef int __int32_t;
typedef unsigned int __uint32_t;

typedef unsigned long useconds_t;
typedef long suseconds_t;

#endif
