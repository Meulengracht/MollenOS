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
 */
#include <stdint.h>
#include <math.h>

#define	__fenv_static
#define	__fenv_extern
#include <fenv.h>

static int    __sse_support = 0;
static fenv_t fe_dfl_env;
static fenv_t fe_nomask_env;

// global read-only pointers
const fenv_t *_fe_dfl_env = &fe_dfl_env;
const fenv_t *_fe_nomask_env = &fe_nomask_env;

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#define __get_cpuid(Function, Registers) __cpuid(Registers, Function);
#else
#include <cpuid.h>
#define __get_cpuid(Function, Registers) __cpuid(Function, Registers[0], Registers[1], Registers[2], Registers[3]);
#endif

int
fesetexceptflag(const fexcept_t *flagp, int excepts)
{
	fenv_t env;
	uint32_t mxcsr;

	__fnstenv(&env);
	env.__status &= ~excepts;
	env.__status |= *flagp & excepts;
	__fldenv(env);

	if (__sse_support) {
		__stmxcsr(&mxcsr);
		mxcsr &= ~excepts;
		mxcsr |= *flagp & excepts;
		__ldmxcsr(mxcsr);
	}

	return (0);
}

int
feraiseexcept(int excepts)
{
	fexcept_t ex = excepts;

	fesetexceptflag(&ex, excepts);
	__fwait();
	return (0);
}

extern inline int fetestexcept(int __excepts);
extern inline int fegetround(void);
extern inline int fesetround(int __round);

int
fegetenv(fenv_t *envp)
{
	uint32_t mxcsr;

	__fnstenv(envp);
	/*
	 * fnstenv masks all exceptions, so we need to restore
	 * the old control word to avoid this side effect.
	 */
	__fldcw(envp->__control);
	if (__sse_support) {
		__stmxcsr(&mxcsr);
		__set_mxcsr(*envp, mxcsr);
	}
	return (0);
}

int
feholdexcept(fenv_t *envp)
{
	uint32_t mxcsr;

	__fnstenv(envp);
	__fnclex();
	if (__sse_support) {
		__stmxcsr(&mxcsr);
		__set_mxcsr(*envp, mxcsr);
		mxcsr &= ~FE_ALL_EXCEPT;
		mxcsr |= FE_ALL_EXCEPT << _SSE_EMASK_SHIFT;
		__ldmxcsr(mxcsr);
	}
	return (0);
}

extern inline int fesetenv(const fenv_t *__envp);

int
feupdateenv(const fenv_t *envp)
{
	uint32_t mxcsr;
	uint16_t status;

	__fnstsw(&status);
	if (__sse_support)
		__stmxcsr(&mxcsr);
	else
		mxcsr = 0;
	fesetenv(envp);
	feraiseexcept((mxcsr | status) & FE_ALL_EXCEPT);
	return (0);
}

int
feenableexcept(int mask)
{
	uint32_t mxcsr, omask;
	uint16_t control;

	mask &= FE_ALL_EXCEPT;
	__fnstcw(&control);
	if (__sse_support)
		__stmxcsr(&mxcsr);
	else
		mxcsr = 0;
	omask = ~(control | mxcsr >> _SSE_EMASK_SHIFT) & FE_ALL_EXCEPT;
	control &= ~mask;
	__fldcw(control);
	if (__sse_support) {
		mxcsr &= ~(mask << _SSE_EMASK_SHIFT);
		__ldmxcsr(mxcsr);
	}
	return (omask);
}

int
fedisableexcept(int mask)
{
	uint32_t mxcsr, omask;
	uint16_t control;

	mask &= FE_ALL_EXCEPT;
	__fnstcw(&control);
	if (__sse_support)
		__stmxcsr(&mxcsr);
	else
		mxcsr = 0;
	omask = ~(control | mxcsr >> _SSE_EMASK_SHIFT) & FE_ALL_EXCEPT;
	control |= mask;
	__fldcw(control);
	if (__sse_support) {
		mxcsr |= mask << _SSE_EMASK_SHIFT;
		__ldmxcsr(mxcsr);
	}
	return (omask);
}

int
__has_sse(void)
{
    return __sse_support;
}

void
__check_sse(void)
{
	int CpuRegisters[4] = { 0 };
	__get_cpuid(1, CpuRegisters);
	if (CpuRegisters[3] & 0x2000000) {
		__sse_support = 1;
	}
}


#define __FE_DENORM         (1 << 1)
#define __FE_ALL_EXCEPT_X86 (FE_ALL_EXCEPT | __FE_DENORM)

void
_fpreset(void)
{
  // check for sse support
  __check_sse();
  
  // reset fpu
  __finit();
  
  // The default cw value, 0x37f, is rounding mode zero.  The MXCSR has
  // no precision control, so the only thing to do is set the exception
  // mask bits.
  
  // initialize the MXCSR register: mask all exceptions
  unsigned int mxcsr = __FE_ALL_EXCEPT_X86 << _SSE_EMASK_SHIFT;
  if (__sse_support) {
    __ldmxcsr(mxcsr);
  }
  
  // Setup unmasked environment, but leave __FE_DENORM masked.
  feenableexcept (FE_ALL_EXCEPT);
  fegetenv (&fe_nomask_env);
  
  // Restore default exception masking (all masked).
  fedisableexcept (FE_ALL_EXCEPT);
  fegetenv (&fe_dfl_env);
}
