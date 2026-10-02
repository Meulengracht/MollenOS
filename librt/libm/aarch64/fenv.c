/**
 * Copyright, Philip Meulengracht
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

#include <fenv.h>

static const fenv_t g_defaultEnvironment;
const fenv_t* _fe_dfl_env = &g_defaultEnvironment;

static uint64_t
__ReadControl(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, fpcr" : "=r"(value));
    return value;
}

static uint64_t
__ReadStatus(void)
{
    uint64_t value;

    __asm__ volatile("mrs %0, fpsr" : "=r"(value));
    return value;
}

static void
__WriteControl(uint64_t value)
{
    __asm__ volatile("msr fpcr, %0\nisb" :: "r"(value) : "memory");
}

static void
__WriteStatus(uint64_t value)
{
    __asm__ volatile("msr fpsr, %0" :: "r"(value) : "memory");
}

int
feclearexcept(int excepts)
{
    __WriteStatus(__ReadStatus() & ~(uint64_t)(excepts & FE_ALL_EXCEPT));
    return 0;
}

int
fetestexcept(int excepts)
{
    return (int)__ReadStatus() & excepts & FE_ALL_EXCEPT;
}

int
fegetexceptflag(fexcept_t* flag, int excepts)
{
    *flag = fetestexcept(excepts);
    return 0;
}

int
fesetexceptflag(const fexcept_t* flag, int excepts)
{
    uint64_t mask = excepts & FE_ALL_EXCEPT;

    __WriteStatus((__ReadStatus() & ~mask) | (*flag & mask));
    return 0;
}

int
feraiseexcept(int excepts)
{
    // ARM implementations may omit synchronous FP traps. Cumulative flags
    // are always supported, independently of the optional trap enables.
    __WriteStatus(__ReadStatus() | (excepts & FE_ALL_EXCEPT));
    return 0;
}

int
fegetround(void)
{
    return (int)__ReadControl() & FE_TOWARDZERO;
}

int
fesetround(int mode)
{
    if (mode & ~FE_TOWARDZERO) {
        return -1;
    }
    __WriteControl((__ReadControl() & ~(uint64_t)FE_TOWARDZERO) | mode);
    return 0;
}

int
fegetenv(fenv_t* environment)
{
    environment->Control = __ReadControl();
    environment->Status = __ReadStatus();
    return 0;
}

int
fesetenv(const fenv_t* environment)
{
    __WriteControl(environment->Control);
    __WriteStatus(environment->Status);
    return 0;
}

int
feholdexcept(fenv_t* environment)
{
    fegetenv(environment);
    __WriteControl(environment->Control & ~((uint64_t)FE_ALL_EXCEPT << 8));
    feclearexcept(FE_ALL_EXCEPT);
    return 0;
}

int
feupdateenv(const fenv_t* environment)
{
    int flags = fetestexcept(FE_ALL_EXCEPT);

    fesetenv(environment);
    return feraiseexcept(flags);
}

int
fegetexcept(void)
{
    return (int)(__ReadControl() >> 8) & FE_ALL_EXCEPT;
}

int
feenableexcept(int excepts)
{
    int previous = fegetexcept();
    uint64_t mask = (uint64_t)(excepts & FE_ALL_EXCEPT) << 8;

    __WriteControl(__ReadControl() | mask);
    if ((__ReadControl() & mask) != mask) {
        return -1;
    }
    return previous;
}

int
fedisableexcept(int excepts)
{
    int previous = fegetexcept();

    __WriteControl(__ReadControl() & ~((uint64_t)(excepts & FE_ALL_EXCEPT) << 8));
    return previous;
}
