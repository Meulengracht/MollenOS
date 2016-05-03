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
* MollenOS C Library - CRT Definitions
*/


#ifndef _INC_CRTDEFS
#define _INC_CRTDEFS

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

#ifndef _CRT_EXTERN
#ifdef _CRT_DYNAMIC
#define _CRT_EXTERN extern
#else
#define _CRT_EXTERN
#endif
#endif

#ifdef MOLLENOS
#ifndef _KERNEL_API
#define _CRT_EXPORT __declspec(dllimport)
#else
#define _CRT_EXPORT __declspec(dllexport)
#endif
#ifndef _MOS_API
#define _MOS_API
#endif
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
#endif

#ifndef _Out_
#define _Out_
#endif

#ifndef _M_IX86
#define _M_IX86 600
#endif

#ifndef _CRTIMP
#ifdef CRTDLL /* Defined for ntdll, crtdll, msvcrt, etc */
#define _CRTIMP
#elif defined(_DLL)
#define _CRTIMP __declspec(dllimport)
#define __CRT_INLINE __inline
#else /* !CRTDLL && !_DLL */
#define _CRTIMP
#define __CRT_INLINE __inline
#endif /* CRTDLL || _DLL */
#endif /* !_CRTIMP */

#ifndef __CRT_INLINE
#define __CRT_INLINE __inline
#endif


#ifndef _CRTIMP_ALT
 #ifdef _DLL
  #ifdef _CRT_ALTERNATIVE_INLINES
   #define _CRTIMP_ALT
  #else
   #define _CRTIMP_ALT _CRTIMP
   #define _CRT_ALTERNATIVE_IMPORTED
  #endif
 #else
  #define _CRTIMP_ALT
 #endif
#endif

#ifndef _CRTDATA
 #ifdef _M_CEE_PURE
  #define _CRTDATA(x) x
 #else
  #define _CRTDATA(x) _CRTIMP x
 #endif
#endif

#ifndef _CRTIMP2
 #define _CRTIMP2 _CRTIMP
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

#ifndef _MRTIMP2
 #define _MRTIMP2  _CRTIMP
#endif

#ifndef _MCRTIMP
 #define _MCRTIMP _CRTIMP
#endif

#ifndef _PGLOBAL
 #define _PGLOBAL
#endif

#ifndef _AGLOBAL
 #define _AGLOBAL
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

#ifndef _CRT_ALIGN
#if defined (__midl) || defined(__WIDL__)
#define _CRT_ALIGN(x)
#elif defined(_MSC_VER)
#define _CRT_ALIGN(x) __declspec(align(x))
#else
#define _CRT_ALIGN(x) __attribute__ ((aligned(x)))
#endif
#endif

#ifndef _CRTNOALIAS
#define _CRTNOALIAS
#endif

#ifndef _CRTRESTRICT
#define _CRTRESTRICT
#endif

#ifndef __CRTDECL
#define __CRTDECL __cdecl
#endif

#ifndef _CRT_UNUSED
#define _CRT_UNUSED(x) (void)x
#endif

#ifndef _CONST_RETURN
#ifdef __cplusplus
#define _CONST_RETURN const
#define _CRT_CONST_CORRECT_OVERLOADS
#else
#define _CONST_RETURN
#endif
#endif

#define __crt_typefix(ctype)

#ifdef _MSC_VER
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
#endif

/** Deprecated ***************************************************************/

#ifdef __GNUC__
#define _CRT_DEPRECATE_TEXT(_Text) __attribute__ ((deprecated))
#elif defined(_MSC_VER)
#define _CRT_DEPRECATE_TEXT(_Text) __declspec(deprecated(_Text))
#else
#define _CRT_DEPRECATE_TEXT(_Text)
#endif

#ifndef __STDC_WANT_SECURE_LIB__
#define __STDC_WANT_SECURE_LIB__ 1
#endif

#ifndef _CRT_INSECURE_DEPRECATE
# ifdef _CRT_SECURE_NO_DEPRECATE
#  define _CRT_INSECURE_DEPRECATE(_Replacement)
# else
#  define _CRT_INSECURE_DEPRECATE(_Replacement) \
    _CRT_DEPRECATE_TEXT("This may be unsafe, Try " #_Replacement " instead!")
# endif
#endif

#ifndef _CRT_INSECURE_DEPRECATE_CORE
# ifdef _CRT_SECURE_NO_DEPRECATE_CORE
#  define _CRT_INSECURE_DEPRECATE_CORE(_Replacement)
# else
#  define _CRT_INSECURE_DEPRECATE_CORE(_Replacement) \
    _CRT_DEPRECATE_TEXT("This may be unsafe, Try " #_Replacement " instead! Enable _CRT_SECURE_NO_DEPRECATE to avoid thie warning.")
# endif
#endif

#ifndef _CRT_NONSTDC_DEPRECATE
# ifdef _CRT_NONSTDC_NO_DEPRECATE
#  define _CRT_NONSTDC_DEPRECATE(_Replacement)
# else
#  define _CRT_NONSTDC_DEPRECATE(_Replacement) \
    _CRT_DEPRECATE_TEXT("Deprecated POSIX name, Try " #_Replacement " instead!")
# endif
#endif

#ifndef _CRT_INSECURE_DEPRECATE_MEMORY
#define _CRT_INSECURE_DEPRECATE_MEMORY(_Replacement)
#endif

#ifndef _CRT_INSECURE_DEPRECATE_GLOBALS
#define _CRT_INSECURE_DEPRECATE_GLOBALS(_Replacement)
#endif

#ifndef _CRT_MANAGED_HEAP_DEPRECATE
#define _CRT_MANAGED_HEAP_DEPRECATE
#endif

#ifndef _CRT_OBSOLETE
#define _CRT_OBSOLETE(_NewItem)
#endif


/** Constants ****************************************************************/

#define _ARGMAX 100

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t)-1)
#endif

#define __STDC_SECURE_LIB__ 200411L
#define __GOT_SECURE_LIB__ __STDC_SECURE_LIB__
#define _SECURECRT_FILL_BUFFER_PATTERN 0xFD


/** Type definitions *********************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
#undef size_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef unsigned int size_t __attribute__ ((mode (DI)));
#else
	typedef unsigned long long size_t;
#endif
#define SIZET_MAX 0xffffffffffffffffULL
#else
	typedef unsigned int size_t;
#define SIZET_MAX 0xffffffff
#endif
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#undef ssize_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef signed int ssize_t __attribute__((mode(DI)));
#else
	typedef signed long long ssize_t;
#endif
#else
	typedef signed int ssize_t;
#endif
#endif

#ifndef _INTPTR_T_DEFINED
#define _INTPTR_T_DEFINED
#ifndef __intptr_t_defined
#define __intptr_t_defined
#undef intptr_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef int intptr_t __attribute__ ((mode (DI)));
#else
	typedef long long intptr_t;
#endif
#else
	typedef int intptr_t;
#endif
#endif
#endif

#ifndef _UINTPTR_T_DEFINED
#define _UINTPTR_T_DEFINED
#ifndef __uintptr_t_defined
#define __uintptr_t_defined
#undef uintptr_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef unsigned int uintptr_t __attribute__ ((mode (DI)));
#else
	typedef unsigned long long uintptr_t;
#endif
#else
	typedef unsigned int uintptr_t;
#endif
#endif
#endif

#ifndef _PTRDIFF_T_DEFINED
#define _PTRDIFF_T_DEFINED
#ifndef _PTRDIFF_T_
#undef ptrdiff_t
#if defined(_WIN64) || defined(_X86_64)
#if defined(__GNUC__) && defined(__STRICT_ANSI__)
	typedef int ptrdiff_t __attribute__ ((mode (DI)));
#else
	typedef long long ptrdiff_t;
#endif
#else
	typedef int ptrdiff_t;
#endif
#endif
#endif

#ifndef _WCHAR_T_DEFINED
#define _WCHAR_T_DEFINED
#ifndef __cplusplus
  typedef unsigned short wchar_t;
#endif
#endif

#ifndef _WCTYPE_T_DEFINED
#define _WCTYPE_T_DEFINED
  typedef unsigned short wint_t;
  typedef unsigned short wctype_t;
#endif

#ifndef _ERRCODE_DEFINED
#define _ERRCODE_DEFINED
  typedef int errcode;
  typedef int errno_t;
#endif

#ifndef _TIME32_T_DEFINED
#define _TIME32_T_DEFINED
  typedef long __time32_t;
#endif

#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
#if _INTEGRAL_MAX_BITS >= 64
  typedef __int64 __time64_t;
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

struct threadmbcinfostruct;
typedef struct threadmbcinfostruct *pthreadmbcinfo;

#ifndef _TAGLC_ID_DEFINED
#define _TAGLC_ID_DEFINED
  typedef struct tagLC_ID {
    unsigned short wLanguage;
    unsigned short wCountry;
    unsigned short wCodePage;
  } LC_ID,*LPLC_ID;
#endif

#ifndef _THREADLOCALEINFO
#define _THREADLOCALEINFO
  typedef struct threadlocaleinfostruct {
    int refcount;
    unsigned int lc_codepage;
    unsigned int lc_collate_cp;
    unsigned long lc_handle[6];
    LC_ID lc_id[6];
    struct {
      char *locale;
      wchar_t *wlocale;
      int *refcount;
      int *wrefcount;
    } lc_category[6];
    int lc_clike;
    int mb_cur_max;
    int *lconv_intl_refcount;
    int *lconv_num_refcount;
    int *lconv_mon_refcount;
    struct lconv *lconv;
    int *ctype1_refcount;
    unsigned short *ctype1;
    const unsigned short *pctype;
    const unsigned char *pclmap;
    const unsigned char *pcumap;
    struct __lc_time_data *lc_time_curr;
  } threadlocinfo, *pthreadlocinfo;
#endif

struct __lc_time_data;

typedef struct localeinfo_struct {
    pthreadlocinfo locinfo;
    pthreadmbcinfo mbcinfo;
}_locale_tstruct,*_locale_t;

#ifdef __cplusplus
}
#endif

#endif /* !_INC_CRTDEFS */
