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
 * MollenOS C Environment - Shared Routines
 */

/* Includes
 * - Library */
#include <os/mollenos.h>
#include <stdlib.h>
#include <threads.h>
#include <string.h>

#define _ATTRIBUTES read
#pragma section(".CRTMP$XCA",long,_ATTRIBUTES)
#pragma section(".CRTMP$XCZ",long,_ATTRIBUTES)
#pragma section(".CRTMP$XIA",long,_ATTRIBUTES)
#pragma section(".CRTMP$XIZ",long,_ATTRIBUTES)

#pragma section(".CRTMA$XCA",long,_ATTRIBUTES)
#pragma section(".CRTMA$XCZ",long,_ATTRIBUTES)
#pragma section(".CRTMA$XIA",long,_ATTRIBUTES)
#pragma section(".CRTMA$XIZ",long,_ATTRIBUTES)

#pragma section(".CRTVT$XCA",long,_ATTRIBUTES)
#pragma section(".CRTVT$XCZ",long,_ATTRIBUTES)

#pragma section(".CRT$XCA",long,_ATTRIBUTES)
#pragma section(".CRT$XCAA",long,_ATTRIBUTES)
#pragma section(".CRT$XCC",long,_ATTRIBUTES)
#pragma section(".CRT$XCZ",long,_ATTRIBUTES)
#pragma section(".CRT$XDA",long,_ATTRIBUTES)
#pragma section(".CRT$XDC",long,_ATTRIBUTES)
#pragma section(".CRT$XDZ",long,_ATTRIBUTES)
#pragma section(".CRT$XIA",long,_ATTRIBUTES)
#pragma section(".CRT$XIAA",long,_ATTRIBUTES)
#pragma section(".CRT$XIC",long,_ATTRIBUTES)
#pragma section(".CRT$XID",long,_ATTRIBUTES)
#pragma section(".CRT$XIY",long,_ATTRIBUTES)
#pragma section(".CRT$XIZ",long,_ATTRIBUTES)
#pragma section(".CRT$XLA",long,_ATTRIBUTES)
#pragma section(".CRT$XLC",long,_ATTRIBUTES)
#pragma section(".CRT$XLD",long,_ATTRIBUTES)
#pragma section(".CRT$XLZ",long,_ATTRIBUTES)
#pragma section(".CRT$XPA",long,_ATTRIBUTES)
#pragma section(".CRT$XPX",long,_ATTRIBUTES)
#pragma section(".CRT$XPXA",long,_ATTRIBUTES)
#pragma section(".CRT$XPZ",long,_ATTRIBUTES)
#pragma section(".CRT$XTA",long,_ATTRIBUTES)
#pragma section(".CRT$XTB",long,_ATTRIBUTES)
#pragma section(".CRT$XTX",long,_ATTRIBUTES)
#pragma section(".CRT$XTZ",long,_ATTRIBUTES)
#pragma section(".rdata$T",long,read)
#pragma section(".rtc$IAA",long,read)
#pragma section(".rtc$IZZ",long,read)
#pragma section(".rtc$TAA",long,read)
#pragma section(".rtc$TZZ",long,read)
#pragma section(".tls",long,read,write)
#pragma section(".tls$AAA",long,read,write)
#pragma section(".tls$ZZZ",long,read,write)

#if defined(_MSC_VER) || defined(__clang__)
#define _CRTALLOC(x) __declspec(allocate(x))
#elif defined(__GNUC__)
#define _CRTALLOC(x) __attribute__ ((section (x) ))
#else
#error Your compiler is not supported.
#endif

#ifndef __INTERNAL_FUNC_DEFINED
#define __INTERNAL_FUNC_DEFINED
typedef void(*_PVFV)(void);
typedef int(*_PIFV)(void);
typedef void(*_PVFI)(int);
typedef void(*_PVTLS)(void*, unsigned long, void*);
#endif

/* CRT Segments
 * - COFF Linker segments for initializers/finalizers 
 *   I = C, C = C++, P = Pre-Terminators, T = Terminators. */
_CRTALLOC(".CRT$XIA") _PIFV __xi_a[]    = { 0 };
_CRTALLOC(".CRT$XIZ") _PIFV __xi_z[]    = { 0 };
_CRTALLOC(".CRT$XCA") _PVFV __xc_a[]    = { 0 };
_CRTALLOC(".CRT$XCZ") _PVFV __xc_z[]    = { 0 };
_CRTALLOC(".CRT$XPA") _PVFV __xp_a[]    = { 0 };
_CRTALLOC(".CRT$XPZ") _PVFV __xp_z[]    = { 0 };
_CRTALLOC(".CRT$XTA") _PVFV __xt_a[]    = { 0 };
_CRTALLOC(".CRT$XTZ") _PVFV __xt_z[]    = { 0 };
_CRTALLOC(".CRT$XLA") _PVTLS __xl_a[]   = { 0 };
_CRTALLOC(".CRT$XLZ") _PVTLS __xl_z[]   = { 0 };
#pragma comment(linker, "/merge:.CRT=.data")

_CRTALLOC(".tls") char _tls_start       = 0;
_CRTALLOC(".tls$ZZZ") char _tls_end     = 0;

/* Externs 
 * - Access to lib-c initializers */
CRTDECL(void, __CrtCallInitializers(_PVFV *pfbegin, _PVFV *pfend));
CRTDECL(int, __CrtCallInitializersEx(_PIFV *pfbegin, _PIFV *pfend));
CRTDECL(void, __CrtCallInitializersTls(_PVTLS *pfbegin, _PVTLS *pfend, void *dso_handle, unsigned long reason));

/* Globals
 * - Global exported shared variables */
static int _tls_init = 0;
void *__dso_handle = &__dso_handle;
void *_tls_module_data = NULL;
#if defined(i386) || defined(__i386__)
void **_tls_array = NULL; // on 64 bit this must be at gs:0x58, 32 bit this should point into tls area
unsigned long _tls_index = 0;
#else
#error "Implicit tls architecture must be implemented"
#endif

_CRTALLOC(".rdata$T") const struct {
    uintptr_t*  StartOfData;
    uintptr_t*  EndOfData;
    uintptr_t*  AddressOfTlsIndex;
    uintptr_t*  StartOfCallbacks;
    size_t      SizeOfZeroFill;
    size_t      Characteristics;
} _tls_used = {
        (uintptr_t*)(&_tls_start + 1),  // start of tls data
        (uintptr_t*)&_tls_end,          // end of tls data
        (uintptr_t*)&_tls_index,        // address of tls_index
        (uintptr_t*)(&__xl_a + 1),      // pointer to call back array
        0,                              // size of tls zero fill
        0                               // characteristics
};

// __CrtCreateTlsBlock
// Creates a new tls key for the module that links against
// this file
void __CrtCreateTlsBlock(void) {
    _tls_array = (void**)__get_reserved(1);
    _tls_index = 0;
    while (_tls_array[_tls_index] != NULL) _tls_index++;
}

// __CrtAttachTlsBlock
// Creates a new tls block for calling thread. This is automatically
// called for main thread, not new threads
void __CrtAttachTlsBlock(void) {
    size_t TlsDataSize = (size_t)_tls_used.EndOfData - (size_t)_tls_used.StartOfData;
    if (TlsDataSize > 0 && _tls_used.StartOfData < _tls_used.EndOfData) {
        _tls_module_data = malloc(TlsDataSize);
        memcpy(_tls_module_data, (void*)_tls_used.StartOfData, TlsDataSize);
        _tls_array[_tls_index] = _tls_module_data;
    }
    if (_tls_init == 0) {
        __CrtCallInitializersTls(__xl_a, __xl_z, __dso_handle, DLL_ACTION_INITIALIZE);
    }
    __CrtCallInitializersTls(__xl_a, __xl_z, __dso_handle, DLL_ACTION_THREADATTACH);
}

// On ALL coff platform this must be called
// to run all initializers for C/C++
void __CrtCxxInitialize(void) {
	__CrtCallInitializers(__xc_a, __xc_z);
	__CrtCallInitializersEx(__xi_a, __xi_z);
    __CrtCreateTlsBlock();
    __CrtAttachTlsBlock();
}

// On non-windows coff platforms this should not be run
// as terminators are registered by cxa_atexit.
void __CrtCxxFinalize(void) {
	__CrtCallInitializers(__xp_a, __xp_z);
	__CrtCallInitializers(__xt_a, __xt_z);
    __CrtCallInitializersTls(__xl_a, __xl_z, __dso_handle, DLL_ACTION_FINALIZE);
}
