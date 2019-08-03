/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * POSIX System Types
 *   - Common definitions used by posix systems, we only define this header
 *     and most of the types for compliance, they are not used by Vali and
 *     programs should refrain from using posix headers on Vali
 */

#ifndef __SYS_TYPES__
#define __SYS_TYPES__

#include <os/osdefs.h>

// Handle the definition of time32
#ifndef _TIME32_T_DEFINED
#define _TIME32_T_DEFINED
  typedef long __time32_t;
#endif

// Handle the definition of time64
#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
#if _INTEGRAL_MAX_BITS >= 64
  typedef long long __time64_t;
#endif
#endif

// Handle the definition of time_t
#ifndef _TIME_T_DEFINED
#define _TIME_T_DEFINED
#ifdef _USE_32BIT_TIME_T
  typedef __time32_t time_t;
#else
  typedef __time64_t time_t;
#endif
#endif

// Handle the definition of common bit types
#ifndef __BIT_TYPES_DEFINED__
#define __BIT_TYPES_DEFINED__
typedef unsigned char       u_int8_t;
typedef unsigned short      u_int16_t;
typedef unsigned int        u_int32_t;
typedef unsigned long long  u_int64_t;
#endif

// Bsd types
typedef unsigned char       u_char;
typedef unsigned short      u_short;
typedef unsigned int        u_int;
typedef unsigned long       u_long;

// Sysv types
typedef unsigned char       unchar;
typedef unsigned short      ushort;
typedef unsigned int        uint;
typedef unsigned long       ulong;

// Handle definition of offset type for file
// types and interactions.
#ifndef OFF_T_DEFINED
#if _FILE_OFFSET_BITS==64
    typedef int64_t         off_t;
    typedef int64_t         off_t;
#else
    typedef long            off_t;
    typedef long long       off64_t;
#endif
#define OFF_T_DEFINED
#endif 

// Handle POSIX compliance by defining the below types
// even through they are not used in our OS.
typedef __INTPTR_TYPE__     ssize_t;
typedef unsigned short      nlink_t;
typedef unsigned short      _ino_t;
typedef unsigned short      ino_t;
typedef unsigned int        _dev_t;
typedef unsigned int        dev_t;
typedef unsigned long       mode_t;
typedef unsigned long       uid_t;
typedef unsigned long       gid_t;
typedef long                pid_t;
typedef long                blksize_t;
typedef long                blkcnt_t;
typedef unsigned long       fsblkcnt_t;
typedef unsigned long       fsfilcnt_t;

// Handle some common math.h types that may be used
// by math library.
typedef int                 __int32_t;
typedef unsigned int        __uint32_t;

// Handle some common time.h types that may be used
// by time library.
typedef unsigned long       useconds_t;
typedef long                suseconds_t;

#endif
