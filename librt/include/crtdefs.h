/* MollenOS
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
 * MollenOS C Library - CRT Definitions
 */
#ifndef __STDC_CRTDEF__
#define __STDC_CRTDEF__

#ifdef _USE_32BIT_TIME_T
#ifdef _WIN64
#error You cannot use 32-bit time_t (_USE_32BIT_TIME_T) with _WIN64
#undef _USE_32BIT_TIME_T
#endif
#else
#if _INTEGRAL_MAX_BITS < 64
#define _USE_32BIT_TIME_T
#endif
#endif

/** Properties ***************************************************************/

#ifndef _CRT_STRINGIZE
#define __CRT_STRINGIZE(_Value) #_Value
#define _CRT_STRINGIZE(_Value) __CRT_STRINGIZE(_Value)
#endif

#ifndef _CRT_WIDE
#define __CRT_WIDE(_String) L ## _String
#define _CRT_WIDE(_String) __CRT_WIDE(_String)
#endif

#ifdef __cplusplus
#ifndef restrict
#define restrict /*restrict*/
#endif
#endif

#if defined(i386) && !defined(__i386__)
#define __i386__ 1
#elif (defined(amd64) || defined(__amd64__)) && !defined(__x86_64__)
#define __x86_64__ 1
#endif

#if defined(__i386__)
#define __STDC_CONVENTION __cdecl
#define ASMDECL(ReturnType, Function) ReturnType __cdecl Function
#elif defined(__x86_64__)
#define __STDC_CONVENTION
#define ASMDECL(ReturnType, Function) ReturnType __cdecl Function
#endif

#if defined(__clang__)
    //  Clang 
    #define CRTEXPORT __declspec(dllexport)
    #define CRTIMPORT 
    #define CRTHIDE
    #define CRTEXTERN extern
#elif defined(_MSC_VER)
    //  Microsoft 
    #define CRTEXPORT __declspec(dllexport)
    #define CRTIMPORT __declspec(dllimport)
    #define CRTHIDE
    #define CRTEXTERN extern
#elif defined(__GNUC__)
    //  GCC
    #define CRTEXPORT __attribute__((visibility("default")))
    #define CRTIMPORT 
    #define CRTHIDE __attribute__((visibility("internal")))
    #define CRTEXTERN extern
#else
    //  do nothing and hope for the best?
    #define CRTEXPORT
    #define CRTIMPORT
    #define CRTHIDE
    #define CRTEXTERN extern
    #pragma warning Unknown dynamic link import/export semantics.
#endif

/* Standard C-Library Export 
 * Standard definition for the std-c. */
#ifdef __OSLIB_C_IMPLEMENTATION
#ifdef __OSLIB_C_SHAREDLIBRARY
#define __STDC_DECORATION CRTEXPORT
#else
#define __STDC_DECORATION 
#endif //!__OSLIB_C_SHAREDLIBRARY
#else
#ifdef __OSLIB_C_SHAREDLIBRARY
#define __STDC_DECORATION CRTIMPORT
#else
#define __STDC_DECORATION 
#endif //!__OSLIB_C_SHAREDLIBRARY
#endif //!__OSLIB_C_IMPLEMENTATION
#define CRTDECL(ReturnType, Function) __STDC_DECORATION ReturnType Function
#define CRTDECL_DATA(Type, Name) __STDC_DECORATION Type Name
#ifdef __STDC_LIB_EXT1__
#define CRTDECL_EX(ReturnType, Function) __STDC_DECORATION ReturnType Function
#else
#define CRTDECL_EX(ReturnType, Function)
#endif //!__STDC_LIB_EXT1__

#define DLL_ACTION_INITIALIZE   0
#define DLL_ACTION_FINALIZE     1
#define DLL_ACTION_THREADATTACH 2

/* This is the export definitions used by
 * the runtime libraries, they default to
 * import by standard. RT Libraries have
 * CRTDLL defined by default. 
 * The only time this needs to be defined
 * as static is for the OS */
#ifndef _CRTIMP
#if defined(CRTDLL)
#define _CRTIMP CRTEXPORT
#elif defined(_KRNL_DLL)
#define _CRTIMP CRTEXTERN
#else
#define _CRTIMP CRTIMPORT
#endif
#endif //!_CRTIMP

#ifndef __CRT_INLINE
#define __CRT_INLINE __inline
#endif

#ifndef __EXTERN
#define __EXTERN extern
#endif

#ifndef __CONST
#define __CONST const
#endif

#ifndef _CODE_TAGS
#define _CODE_TAGS
#ifdef __cplusplus
#define _CODE_BEGIN extern "C" {
#define _CODE_END }
#else
#define _CODE_BEGIN
#define _CODE_END
#endif
#endif

/* Kernel/LibOS export definitions
 * Used by kernel to export functions for modules
 * (Should soon be removed tho), and used by LibOS
 * to export functions */
#ifdef MOLLENOS

/* API Interfaces */
#ifndef SERVICEAPI
#define SERVICEAPI static __inline
#define SERVICEABI __STDC_CONVENTION
#endif

/* Kernel Export for Modules */
#ifndef KERNELAPI
#define KERNELAPI __EXTERN
#define KERNELABI __STDC_CONVENTION
#endif
#endif

#if (defined (__clang__))
#define PACKED_STRUCT(name, body) struct __attribute__((packed)) name body 
#define PACKED_TYPESTRUCT(name, body) typedef struct __attribute__((packed)) _##name body name##_t
#define PACKED_ATYPESTRUCT(opts, name, body) typedef opts struct __attribute__((packed)) _##name body name##_t
#elif (defined (__GNUC__))
#define PACKED_STRUCT(name, body) struct name body __attribute__((packed))
#define PACKED_TYPESTRUCT(name, body) typedef struct _##name body name##_t __attribute__((packed))
#define PACKED_ATYPESTRUCT(opts, name, body) typedef opts struct _##name body name##_t __attribute__((packed))
#elif (defined (__arm__))
#define PACKED_STRUCT(name, body) __packed struct name body
#define PACKED_TYPESTRUCT(name, body) __packed typedef struct _##name body name##_t
#define PACKED_ATYPESTRUCT(opts, name, body) __packed typedef opts struct _##name body name##_t
#elif (defined (_MSC_VER))
#define PACKED_STRUCT(name, body) __pragma(pack(push, 1)) struct name body __pragma(pack(pop))
#define PACKED_TYPESTRUCT(name, body) __pragma(pack(push, 1)) typedef struct _##name body name##_t __pragma(pack(pop))
#define PACKED_ATYPESTRUCT(opts, name, body) __pragma(pack(push, 1)) typedef opts struct _##name body name##_t __pragma(pack(pop))
#else
#error Please define packed struct for the used compiler
#endif

#ifndef _W64
 #if !defined(_midl) && defined(_X86_) && _MSC_VER >= 1300
  #define _W64 __w64
 #else
  #define _W64
 #endif
#endif

#ifndef _Check_return_
#define _Check_return_
#endif

#ifndef _In_
#define _In_
#define _In_Opt_
#endif

#ifndef _Out_
#define _Out_
#define _Out_Opt_
#endif

#ifndef _InOut_
#define _InOut_
#define _InOut_Opt_
#endif

#ifndef _M_IX86
#define _M_IX86 600
#endif

#ifndef _CRTIMP_PURE
 #define _CRTIMP_PURE _CRTIMP
#endif

#ifndef _CRTIMP_ALTERNATIVE
 #define _CRTIMP_ALTERNATIVE _CRTIMP
 #define _CRT_ALTERNATIVE_IMPORTED
#endif

#ifndef _CRTIMP_NOIA64
 #ifdef __ia64__
  #define _CRTIMP_NOIA64
 #else
  #define _CRTIMP_NOIA64 _CRTIMP
 #endif
#endif

#ifndef _CONST_RETURN
 #define _CONST_RETURN
#endif

#ifndef UNALIGNED
#if defined(__ia64__) || defined(__x86_64)
#define CRT_UNALIGNED __unaligned
#else
#define CRT_UNALIGNED
#endif
#endif

#if __STDC_VERSION__ >= 201112L
#define DECLSPEC_NORETURN(X) _Noreturn X
#else
#ifdef _MSC_VER
#define DECLSPEC_NORETURN(X) __declspec(noreturn) X
#else
#define DECLSPEC_NORETURN(X) X __attribute__((noreturn))
#endif
#endif
#define _CRTIMP_NORETURN(X) DECLSPEC_NORETURN(X)

#ifndef DECLSPEC_ADDRSAFE
#if defined(_MSC_VER) && (defined(_M_ALPHA) || defined(_M_AXP64))
#define DECLSPEC_ADDRSAFE __declspec(address_safe)
#else
#define DECLSPEC_ADDRSAFE
#endif
#endif /* DECLSPEC_ADDRSAFE */

#ifndef DECLSPEC_NOTHROW
#if !defined(MIDL_PASS)
#define DECLSPEC_NOTHROW __declspec(nothrow)
#else
#define DECLSPEC_NOTHROW
#endif
#endif /* DECLSPEC_NOTHROW */

#ifndef NOP_FUNCTION
#if defined(_MSC_VER)
#define NOP_FUNCTION __noop
#else
#define NOP_FUNCTION (void)0
#endif
#endif /* NOP_FUNCTION */

#ifndef _CRT_ALIGN
#if defined (__midl) || defined(__WIDL__)
#define _CRT_ALIGN(x)
#elif defined(_MSC_VER)
#define _CRT_ALIGN(x) __declspec(align(x))
#else
#define _CRT_ALIGN(x) __attribute__ ((aligned(x)))
#endif
#endif

#ifndef __CRTDECL
#define __CRTDECL __cdecl
#endif

#ifndef _CRT_UNUSED
#define _CRT_UNUSED(x) (void)x
#endif

#if defined(_MSC_VER)
#ifdef _AMD64_
#define MemoryBarrier __faststorefence
#endif

#ifdef _IA64_
#define MemoryBarrier __mf
#endif

// x86
__forceinline void
MemoryBarrier (void)
{
    long Barrier;
    __asm {
        xchg Barrier, eax
    }
}
#elif defined(__clang__)
#if defined(i386)
#define MemoryBarrier __sync_synchronize
#elif defined(amd64)
#define MemoryBarrier __sync_synchronize
#endif
#endif

/** Deprecated ***************************************************************/

#ifdef __GNUC__
#define _CRT_DEPRECATE_TEXT(_Text) __attribute__ ((deprecated))
#elif defined(_MSC_VER)
#define _CRT_DEPRECATE_TEXT(_Text) __declspec(deprecated(_Text))
#else
#define _CRT_DEPRECATE_TEXT(_Text)
#endif

/* Sometimes it's necessary to define __LITTLE_ENDIAN explicitly
 * but these catch some common cases. */
#if defined(i386) || defined(i486) || \
	defined(intel) || defined(x86) || defined(i86pc) || \
	defined(__alpha) || defined(__osf__) || defined(_X86_32)
#define __LITTLE_ENDIAN
#endif

#if defined(__i386__) || defined(i386) || defined(_X86_32)
#if !defined(_USE_32BIT_TIME_T) && !defined(_USE_64BIT_TIME_T)
#define _USE_32BIT_TIME_T
#endif
#else
#if defined(_USE_32BIT_TIME_T)
#error "Can't use 32 bit time structures in 64 bit mode"
#elif !defined(_USE_64BIT_TIME_T)
#define _USE_64BIT_TIME_T
#endif
#endif

#endif /* !__STDC_CRTDEF__ */
