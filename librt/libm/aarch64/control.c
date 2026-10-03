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
#define __BSD_VISIBLE 1

#include <math.h>
#include <fenv.h>

/** Maps the public CRT exception masks to native FPSR/FPCR bit positions. */
static const struct {
    unsigned int Mask;
    unsigned int Exception;
} g_exceptionMasks[] = {
    { _EM_INVALID, FE_INVALID },
    { _EM_DENORMAL, FE_DENORMAL },
    { _EM_ZERODIVIDE, FE_DIVBYZERO },
    { _EM_OVERFLOW, FE_OVERFLOW },
    { _EM_UNDERFLOW, FE_UNDERFLOW },
    { _EM_INEXACT, FE_INEXACT }
};

static unsigned int
__ReadFlags(void)
{
    unsigned int flags = _PC_53;
    unsigned int enabled;
    unsigned int index;

    enabled = fegetexcept();
    for (index = 0; index < sizeof(g_exceptionMasks) / sizeof(g_exceptionMasks[0]); index++) {
        if (!(enabled & g_exceptionMasks[index].Exception)) {
            flags |= g_exceptionMasks[index].Mask;
        }
    }
    switch (fegetround()) {
        case FE_DOWNWARD:
            flags |= _RC_DOWN;
            break;
        case FE_UPWARD:
            flags |= _RC_UP;
            break;
        case FE_TOWARDZERO:
            flags |= _RC_CHOP;
            break;
        default:
            break;
    }
    return flags;
}

unsigned int
_control87(
    _In_ unsigned int value,
    _In_ unsigned int mask)
{
    unsigned int flags;
    unsigned int index;
    int          enabled = 0;
    int          rounding = FE_TONEAREST;

    flags = (__ReadFlags() & ~mask) | (value & mask);
    for (index = 0; index < sizeof(g_exceptionMasks) / sizeof(g_exceptionMasks[0]); index++) {
        if (!(flags & g_exceptionMasks[index].Mask)) {
            enabled |= g_exceptionMasks[index].Exception;
        }
    }
    switch (flags & _MCW_RC) {
        case _RC_DOWN:
            rounding = FE_DOWNWARD;
            break;
        case _RC_UP:
            rounding = FE_UPWARD;
            break;
        case _RC_CHOP:
            rounding = FE_TOWARDZERO;
            break;
        default:
            break;
    }
    fedisableexcept(FE_ALL_EXCEPT & ~enabled);
    feenableexcept(enabled);
    fesetround(rounding);

    // Scalar ARM floating point has fixed precision. Return actual hardware
    // state, including trap enables that are optional on ARM implementations.
    return __ReadFlags();
}

unsigned int
_controlfp(
    _In_ unsigned int value,
    _In_ unsigned int mask)
{
    return _control87(value, mask & ~_EM_DENORMAL);
}
