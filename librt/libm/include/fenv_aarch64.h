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

#ifndef __FENV_AARCH64_H__
#define __FENV_AARCH64_H__

#include <os/osdefs.h>

typedef struct {
    uint64_t Control;
    uint64_t Status;
} fenv_t;

typedef uint32_t fexcept_t;

// Exception flags use the FPSR cumulative-status bit assignments.
#define FE_INVALID      0x01
#define FE_DIVBYZERO    0x02
#define FE_OVERFLOW     0x04
#define FE_UNDERFLOW    0x08
#define FE_INEXACT      0x10
#define FE_DENORMAL     0x80
#define FE_ALL_EXCEPT   0x9f

// Rounding modes use the FPSR rounding-mode field assignments.
#define FE_TONEAREST    0x00000000
#define FE_UPWARD       0x00400000
#define FE_DOWNWARD     0x00800000
#define FE_TOWARDZERO   0x00c00000

_CODE_BEGIN
extern const fenv_t* _fe_dfl_env;
#define FE_DFL_ENV (_fe_dfl_env)

CRTDECL(int, feclearexcept(int excepts));
CRTDECL(int, fegetexceptflag(fexcept_t* flag, int excepts));
CRTDECL(int, fesetexceptflag(const fexcept_t* flag, int excepts));
CRTDECL(int, feraiseexcept(int excepts));
CRTDECL(int, fetestexcept(int excepts));
CRTDECL(int, fegetround(void));
CRTDECL(int, fesetround(int mode));
CRTDECL(int, fegetenv(fenv_t* environment));
CRTDECL(int, fesetenv(const fenv_t* environment));
CRTDECL(int, feholdexcept(fenv_t* environment));
CRTDECL(int, feupdateenv(const fenv_t* environment));

#if __BSD_VISIBLE

CRTDECL(int, feenableexcept(int excepts));
CRTDECL(int, fedisableexcept(int excepts));
CRTDECL(int, fegetexcept(void));

#endif // __BSD_VISIBLE

_CODE_END

#endif //!__FENV_AARCH64_H__
