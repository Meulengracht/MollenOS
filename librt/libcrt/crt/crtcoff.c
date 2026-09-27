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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * COFF/PE Image Support
 *   - Implements CRT routines and sections neccessary for proper running PE/COFF images.
 */

//#define __TRACE

#include <ddk/utils.h>
#include <internal/_tls.h>
#include <inttypes.h>

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
_CRTALLOC(".CRT$XIA") _PIFV __xi_a[]  = { 0 };
_CRTALLOC(".CRT$XIZ") _PIFV __xi_z[]  = { 0 };
_CRTALLOC(".CRT$XCA") _PVFV __xc_a[]  = { 0 };
_CRTALLOC(".CRT$XCZ") _PVFV __xc_z[]  = { 0 };
_CRTALLOC(".CRT$XPA") _PVFV __xp_a[]  = { 0 };
_CRTALLOC(".CRT$XPZ") _PVFV __xp_z[]  = { 0 };
_CRTALLOC(".CRT$XTA") _PVFV __xt_a[]  = { 0 };
_CRTALLOC(".CRT$XTZ") _PVFV __xt_z[]  = { 0 };
_CRTALLOC(".CRT$XLA") _PVTLS __xl_a[] = { 0 };
_CRTALLOC(".CRT$XLZ") _PVTLS __xl_z[] = { 0 };
#pragma comment(linker, "/merge:.CRT=.data")

_CRTALLOC(".tls")     char _tls_start = 0;
_CRTALLOC(".tls$ZZZ") char _tls_end   = 0;

CRTDECL(void, __cxa_callinitializers(_PVFV *pfbegin, _PVFV *pfend));
CRTDECL(int,  __cxa_callinitializers_ex(_PIFV *pfbegin, _PIFV *pfend));
CRTDECL(void, __cxa_callinitializers_tls(_PVTLS *pfbegin, _PVTLS *pfend, void *dso_handle, unsigned long reason));

void*      __dso_handle = &__dso_handle;
#if defined(i386) || defined(__i386__) || defined(amd64) || defined(__amd64__)
void**        _tls_array = NULL; // on 64 bit this must be at gs:0x58 [11], 32 bit this should point into tls area
unsigned long _tls_index = 0;
#elif defined(__aarch64__)
unsigned long _tls_index = 0;
#else
#error "Implicit tls architecture must be implemented"
#endif

_CRTALLOC(".rdata$T") const struct {
    uintptr_t*  StartOfData;
    uintptr_t*  EndOfData;
    uintptr_t*  AddressOfTlsIndex;
    uintptr_t*  StartOfCallbacks;
    uint32_t    SizeOfZeroFill;
    uint32_t    Characteristics;
} _tls_used = {
#if defined(__aarch64__)
    (uintptr_t*)&_tls_start,       // COFF SECREL offsets include the sentinel
#else
    // This follows the classic Windows/MinGW implicit-TLS convention: 
    // _tls_start (.tls$AAA) is a one-byte sentinel that only marks where 
    // the .tls section begins in the link order
    (uintptr_t*)(&_tls_start + 1), // legacy x86 template origin
#endif
    (uintptr_t*)&_tls_end,         // end of tls data
    (uintptr_t*)&_tls_index,       // address of tls_index
    (uintptr_t*)(&__xl_a + 1),     // pointer to call back array, skip the inital index
    0,                             // size of tls zero fill
    0                              // characteristics
};

// __cxa_module_tls_global_init
// Registers this module's PE TLS template with the shared module registry.
// Registration is idempotent per __dso_handle, so this may be called more 
// than once (e.g. once eagerly for the primary module, once
// via DLL_ACTION_TLSREGISTER/DLL_ACTION_INITIALIZE) without side effects.
void __cxa_module_tls_global_init(void)
{
    TRACE("__cxa_module_tls_global_init(0x%" PRIxIN ")", __dso_handle);

#if !defined(__aarch64__)
    // Compiled code reads this global directly (classic implicit-TLS ABI);
    // it aliases the same thread_storage_t.tls_array the registry fills in.
    _tls_array = (void**)__get_reserved(1);
#endif
    
// LLD/link.exe write the linked TLS alignment into Characteristics.
    unsigned int encoded = (*(const volatile uint32_t*)&_tls_used.Characteristics >> 20) & 15;
    size_t       alignment = encoded ? (size_t)1 << (encoded - 1) : 16;
    uintptr_t    start = (uintptr_t)_tls_used.StartOfData;
    uintptr_t    end = (uintptr_t)_tls_used.EndOfData;
    int          status;

    // Ensure that the TLS data section is valid.
    if (end < start) {
        BOCHSBREAK;
    }

    status = __tls_register_module(
        __dso_handle,
        (void*)start,
        end - start,
        _tls_used.SizeOfZeroFill,
        alignment,
        &_tls_index
    );
    if (status) {
        BOCHSBREAK;
    }
}

// __cxa_module_tls_thread_init
// Creates a new tls block for calling thread. This is automatically
// called for main thread, not new threads
void __cxa_module_tls_thread_init(void)
{
    // Block allocation happens once per-thread in __tls_prepare_modules();
    // this only asserts this module's block exists for the calling thread.
    (void)__vali_tls_get_block(_tls_index);
    __cxa_callinitializers_tls(
        __xl_a,
        __xl_z,
        __dso_handle, 
        DLL_ACTION_THREADATTACH
    );
}

// __cxa_module_tls_thread_finit
// Cleans up the tls block for calling thread. This is automatically
// called for main thread, not new threads
void __cxa_module_tls_thread_finit(void)
{
    __cxa_callinitializers_tls(
        __xl_a,
        __xl_z,
        __dso_handle,
        DLL_ACTION_THREADDETACH
    );
    // All blocks remain live until process-wide thread callbacks finish,
    // then __tls_release_modules() frees them together.
}

// On ALL coff platform this must be called
// to run all initializers for C/C++
void __cxa_module_global_init(void)
{
    TRACE("__cxa_module_global_init(0x%" PRIxIN ")", __dso_handle);
    // DLL_ACTION_TLSREGISTER already registered this module's TLS template.
    __cxa_callinitializers_tls(__xl_a, __xl_z, __dso_handle, DLL_ACTION_INITIALIZE);
    __cxa_module_tls_thread_init();
    TRACE(" > global init (c)");
	__cxa_callinitializers_ex(__xi_a, __xi_z);
    TRACE(" > global init (c++)");
	__cxa_callinitializers(__xc_a, __xc_z);
	TRACE(" > done");
}

// On non-windows coff platforms this should not be run
// as terminators are registered by cxa_atexit.
void __cxa_module_global_finit(void)
{
    TRACE("__cxa_module_global_finit(0x%" PRIxIN ")", __dso_handle);
	__cxa_callinitializers(__xp_a, __xp_z);
	__cxa_callinitializers(__xt_a, __xt_z);
    __cxa_callinitializers_tls(__xl_a, __xl_z, __dso_handle, DLL_ACTION_THREADDETACH);
    __cxa_callinitializers_tls(__xl_a, __xl_z, __dso_handle, DLL_ACTION_FINALIZE);
    // Module TLS blocks are freed process/thread-wide by __tls_release_modules().
}
