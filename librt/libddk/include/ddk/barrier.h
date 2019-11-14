/**
 * MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * Memory and Hardware Barriers
 */
 
#ifndef __DDK_BARRIERS_H__
#define __DDK_BARRIERS_H__

#include <crtdefs.h>

///////////////////////////////////////////////////////////////////////////////
//// Microsoft Visual C++
///////////////////////////////////////////////////////////////////////////////
#if defined(_MSC_VER)
// Define software (compiler) barriers
#define sw_mb()  _ReadWriteBarrier()
#define sw_rmb() _ReadBarrier()
#define sw_wmb() _WriteBarrier()

// Define hardware barriers
#if defined(_M_ARM)   // ARM
#define mb()  __dmb( _ARM_BARRIER_ISH )
#define rmb() __dmb( _ARM_BARRIER_ISH )
#define wmb() __dmb( _ARM_BARRIER_ISHST )
#elif defined(_IA64_) // Itanium
#define mb()  __mf()
#define rmb() __mf()
#define wmb() __mf()
#elif defined(_M_IX86 )
#define mb()  __asm volatile { lock add dword ptr [esp - 4], 0 }
#define rmb() __asm volatile { lock add dword ptr [esp - 4], 0 }
#define wmb() __asm volatile { lock add dword ptr [esp - 4], 0 }

#define __smp_mb()  __asm volatile { lock add dword ptr [esp - 4], 0 }
#define __smp_rmb() sw_mb()
#define __smp_wmb() sw_mb()

#define smp_before_atomic() do { } while(0)
#define smp_after_atomic() do { } while(0)
#elif defined(_M_X64) || defined(_M_AMD64 )
#define mb()  _mm_mfence()
#define rmb() _mm_lfence()
#define wmb() _mm_sfence()

#define __smp_mb()  __asm volatile { lock add qword ptr [rsp - 4], 0 }
#define __smp_rmb() sw_mb()
#define __smp_wmb() sw_mb()

#define smp_before_atomic() do { } while(0)
#define smp_after_atomic() do { } while(0)
#endif

///////////////////////////////////////////////////////////////////////////////
//// Clang
///////////////////////////////////////////////////////////////////////////////
#elif defined(__clang__)
#define sw_mb()  __asm__ __volatile__ ( "" ::: "memory" )
#define sw_rmb() __asm__ __volatile__ ( "" ::: "memory" )
#define sw_wmb() __asm__ __volatile__ ( "" ::: "memory" )

// TODO: i64 and ARM

#if defined(_M_IX86 )
#define mb()      __asm__ __volatile__ ( "lock; addl $0,-4(%%esp)" ::: "memory", "cc" )
#define rmb()     __asm__ __volatile__ ( "lock; addl $0,-4(%%esp)" ::: "memory", "cc" )
#define wmb()     __asm__ __volatile__ ( "lock; addl $0,-4(%%esp)" ::: "memory", "cc" )

#define dma_mb()  sw_mb()
#define dma_rmb() sw_rmb()
#define dma_wmb() sw_wmb()

#define __smp_mb()  __asm__ __volatile__ ( "lock; addl $0,-4(%%esp)" ::: "memory", "cc" )
#define __smp_rmb() sw_mb()
#define __smp_wmb() sw_mb()

#define smp_before_atomic() do { } while(0)
#define smp_after_atomic() do { } while(0)
#elif defined(_M_X64) || defined(_M_AMD64 )
#define mb()      __asm__ __volatile__ ( "mfence" ::: "memory" )
#define rmb()     __asm__ __volatile__ ( "lfence" ::: "memory" )
#define wmb()     __asm__ __volatile__ ( "sfence" ::: "memory" )

#define dma_mb()  sw_mb()
#define dma_rmb() sw_rmb()
#define dma_wmb() sw_wmb()

#define __smp_mb()  __asm__ __volatile__ ( "lock; addl $0,-4(%%rsp)" ::: "memory", "cc" )
#define __smp_rmb() sw_mb()
#define __smp_wmb() sw_mb()

#define smp_before_atomic() do { } while(0)
#define smp_after_atomic() do { } while(0)
#endif //!_M_IX86
#endif //!__clang__

#ifdef __OSCONFIG_ENABLE_MULTIPROCESSORS

#define smp_mb()  __smp_mb()
#define smp_rmb() __smp_rmb()
#define smp_wmb() __smp_wmb()

#ifndef smp_before_atomic
#define smp_before_atomic() sw_mb()
#endif

#ifndef smp_after_atomic
#define smp_after_atomic() sw_mb()
#endif

#else

// On uniprocessor systems no more than a compiler barrier will be neccessary
#define smp_mb()  sw_mb()
#define smp_rmb() sw_rmb()
#define smp_wmb() sw_wmb()

#ifndef smp_before_atomic
#define smp_before_atomic() sw_mb()
#endif

#ifndef smp_after_atomic
#define smp_after_atomic() sw_mb()
#endif

#endif

#endif //!__DDK_BARRIERS_H__
