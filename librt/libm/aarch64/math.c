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

#include <math.h>
#include <stdint.h>

// Scalar square root follows FPCR rounding and records exceptions in FPSR.
double
sqrt(double value)
{
    double result;

    __asm__("fsqrt %d0, %d1" : "=w"(result) : "w"(value));
    return result;
}

float
sqrtf(float value)
{
    float result;

    __asm__("fsqrt %s0, %s1" : "=w"(result) : "w"(value));
    return result;
}

long double
sqrtl(long double value)
{
    return sqrt(value);
}

double
scalbn(double value, int exponent)
{
    union {
        uint64_t Bits;
        double Value;
    } scale;

    // Consume extreme exponents in bounded chunks. Negative chunks leave
    // 53 exponent bits of headroom so only the final multiply can underflow.
    if (exponent > 1023) {
        value *= 0x1p1023;
        exponent -= 1023;
        if (exponent > 1023) {
            value *= 0x1p1023;
            exponent -= 1023;
            if (exponent > 1023) {
                exponent = 1023;
            }
        }
    } else if (exponent < -1022) {
        value *= 0x1p-969;
        exponent += 969;
        if (exponent < -1022) {
            value *= 0x1p-969;
            exponent += 969;
            if (exponent < -1022) {
                exponent = -1022;
            }
        }
    }
    scale.Bits = (uint64_t)(exponent + 1023) << 52;
    return value * scale.Value;
}

long double
scalbnl(long double value, int exponent)
{
    return scalbn(value, exponent);
}

float
scalbnf(float value, int exponent)
{
    // Binary64 exactly represents every binary32 input and power-of-two
    // product in the binary32 exponent range. The cast performs rounding.
    if (exponent > 512) {
        exponent = 512;
    } else if (exponent < -512) {
        exponent = -512;
    }
    return (float)scalbn(value, exponent);
}
