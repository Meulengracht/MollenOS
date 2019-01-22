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
* MollenOS C Library - Variable Argument Definitions
*/

#ifndef _INC_VADEFS
#define _INC_VADEFS

#if !defined(MOLLENOS)
#error Only MollenOS target is supported!
#endif

#include <crtdefs.h>

#undef _CRT_PACKING
#define _CRT_PACKING 8
#pragma pack(push,_CRT_PACKING)

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _UINTPTR_T_DEFINED
#define _UINTPTR_T_DEFINED
#ifndef __uintptr_t_defined
#define __uintptr_t_defined
#undef uintptr_t
#ifdef _WIN64
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
  typedef unsigned int uintptr_t __attribute__ ((mode (DI)));
#else
  __MINGW_EXTENSION typedef unsigned __int64 uintptr_t;
#endif
#else
  typedef unsigned long uintptr_t;
#endif
#endif
#endif

#ifdef __GNUC__
#ifndef __GNUC_VA_LIST
#define __GNUC_VA_LIST
  typedef __builtin_va_list __gnuc_va_list;
#endif
#endif

#ifndef _VA_LIST_DEFINED
#define _VA_LIST_DEFINED
#if defined(__GNUC__)
  typedef __gnuc_va_list va_list;
#elif defined(_MSC_VER)
  typedef char *  va_list;
#endif
#endif

#ifdef __cplusplus
#define _ADDRESSOF(v) (&reinterpret_cast<const char &>(v))
#else
#define _ADDRESSOF(v) (&(v))
#endif

#if defined(__ia64__)
#define _VA_ALIGN 8
#define _SLOTSIZEOF(t) ((sizeof(t) + _VA_ALIGN - 1) & ~(_VA_ALIGN - 1))
#define _VA_STRUCT_ALIGN 16
#define _ALIGNOF(ap) ((((ap)+_VA_STRUCT_ALIGN - 1) & ~(_VA_STRUCT_ALIGN -1)) - (ap))
#define _APALIGN(t,ap) (__alignof(t) > 8 ? _ALIGNOF((uintptr_t) ap) : 0)
#else
#define _SLOTSIZEOF(t) (sizeof(t))
#define _APALIGN(t,ap) (__alignof(t))
#endif

#define _INTSIZEOF(n) ((sizeof(n) + sizeof(int) - 1) & ~(sizeof(int) - 1))

#if defined(__GNUC__)
#define _crt_va_start(v,l)	__builtin_va_start(v,l)
#define _crt_va_arg(v,l)	__builtin_va_arg(v,l)
#define _crt_va_end(v)	__builtin_va_end(v)
#define __va_copy(d,s)	__builtin_va_copy(d,s)
#elif defined(_MSC_VER)

#if defined(_M_IX86)
#define _crt_va_start(v,l)	((void)((v) = (va_list)_ADDRESSOF(l) + _INTSIZEOF(l)))
#define _crt_va_arg(v,l)	(*(l *)(((v) += _INTSIZEOF(l)) - _INTSIZEOF(l)))
#define _crt_va_end(v)	((void)((v) = (va_list)0))
#define __va_copy(d,s)	((void)((d) = (s)))
#elif defined(_M_AMD64)
#define _PTRSIZEOF(n) ((sizeof(n) + sizeof(void*) - 1) & ~(sizeof(void*) - 1))
#define _ISSTRUCT(t) ((sizeof(t) > sizeof(void*)) || (sizeof(t) & (sizeof(t)-1)) != 0)
#define _crt_va_start(v,l)	((void)((v) = (va_list)_ADDRESSOF(l) + _PTRSIZEOF(l)))
#define _crt_va_arg(v,t)	(_ISSTRUCT(t) ? \
                            (**(t**)(((v) += sizeof(void*)) - sizeof(void*))) : \
                            (*(t*)(((v) += sizeof(void*)) - sizeof(void*))))
#define _crt_va_end(v)	((void)((v) = (va_list)0))
#define __va_copy(d,s)	((void)((d) = (s)))
#elif defined(_M_ARM)
#ifdef  __cplusplus
  extern void __cdecl __va_start(va_list*, ...);
  #define _crt_va_start(ap,v) __va_start(&ap, _ADDRESSOF(v), _SLOTSIZEOF(v), _ADDRESSOF(v))
#else
  #define _crt_va_start(ap,v) (ap = (va_list)_ADDRESSOF(v) + _SLOTSIZEOF(v))
#endif
#define _crt_va_arg(ap,t) (*(t*)((ap += _SLOTSIZEOF(t) + _APALIGN(t,ap))  - _SLOTSIZEOF(t)))
#define _crt_va_end(ap)      ( ap = (va_list)0 )
#else
#error Please implement me
#endif

#endif

#if !defined(va_copy) && (!defined(__STRICT_ANSI__) || __STDC_VERSION__ + 0 >= 199900L)
#define va_copy(d,s)	__va_copy((d),(s))
#endif

#ifdef __cplusplus
}
#endif

#pragma pack(pop)
#endif
