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

#define SIGNIFICAND_BIT  UINT64_C(0x0010000000000000)
#define FRACTION_MASK    UINT64_C(0x000fffffffffffff)
#define MAGNITUDE_MASK   UINT64_C(0x7fffffffffffffff)
#define INFINITY_BITS    UINT64_C(0x7ff0000000000000)

/** Normalize a finite, nonzero binary64 operand for exact integer division. */
static uint64_t
__Significand(
    _In_  uint64_t bits,
    _Out_ int*     exponent)
{
    uint64_t fraction = bits & FRACTION_MASK;
    int      biased = (int)(bits >> 52);

    if (biased) {
        *exponent = biased - 1023;
        return fraction | SIGNIFICAND_BIT;
    }
    *exponent = -1022;
    while (!(fraction & SIGNIFICAND_BIT)) {
        fraction <<= 1;
        (*exponent)--;
    }
    return fraction;
}

double
remquo(
    _In_  double dividend,
    _In_  double divisor,
    _Out_ int*   quotient)
{
    union {
        double   Value;
        uint64_t Bits;
    } left = { dividend }, right = { divisor };
    uint64_t leftBits = left.Bits & MAGNITUDE_MASK;
    uint64_t rightBits = right.Bits & MAGNITUDE_MASK;
    uint64_t remainder;
    uint64_t denominator;
    unsigned int lowQuotient = 0;
    int leftExponent;
    int rightExponent;
    int negativeQuotient = (int)((left.Bits ^ right.Bits) >> 63);
    int negativeRemainder = 0;
    double result;

    *quotient = 0;
    if (leftBits > INFINITY_BITS || rightBits > INFINITY_BITS) {
        return dividend + divisor;
    }
    if (!rightBits || leftBits == INFINITY_BITS) {
        return (dividend * divisor) / (dividend * divisor);
    }
    if (!leftBits || rightBits == INFINITY_BITS) {
        return dividend;
    }

    remainder = __Significand(leftBits, &leftExponent);
    denominator = __Significand(rightBits, &rightExponent);
    if (leftExponent < rightExponent) {
        // With a zero truncated quotient, only a magnitude above half of the
        // divisor rounds upward. Subtraction is exact in this interval.
        if (leftExponent == rightExponent - 1 && remainder > denominator) {
            *quotient = negativeQuotient ? -1 : 1;
            result = fabs(dividend) - fabs(divisor);
            return (left.Bits >> 63) ? -result : result;
        }
        return dividend;
    }

    // Long division retains seven quotient bits. The remainder never exceeds
    // 54 bits, so even the largest exponent gap needs no floating division or
    // rounded intermediate result. Seven bits also preserve ties-to-even.
    while (leftExponent > rightExponent) {
        lowQuotient <<= 1;
        if (remainder >= denominator) {
            remainder -= denominator;
            lowQuotient++;
        }
        lowQuotient &= 127;
        remainder <<= 1;
        leftExponent--;
    }
    lowQuotient <<= 1;
    if (remainder >= denominator) {
        remainder -= denominator;
        lowQuotient++;
    }
    if (remainder * 2 > denominator ||
        (remainder * 2 == denominator && (lowQuotient & 1))) {
        remainder = denominator - remainder;
        lowQuotient++;
        negativeRemainder = 1;
    }
    lowQuotient &= 127;
    *quotient = negativeQuotient ? -(int)lowQuotient : (int)lowQuotient;
    result = scalbn((double)remainder, rightExponent - 52);
    if (negativeRemainder ^ (int)(left.Bits >> 63)) {
        result = -result;
    }
    return result;
}

float
remquof(
    _In_  float dividend,
    _In_  float divisor,
    _Out_ int*  quotient)
{
    // A binary32 remainder is exactly representable in binary32. Widening
    // operands cannot change either the remainder or the quotient bits.
    return (float)remquo(dividend, divisor, quotient);
}

long double
remquol(
    _In_  long double dividend,
    _In_  long double divisor,
    _Out_ int*        quotient)
{
    return remquo(dividend, divisor, quotient);
}

double
remainder(
    _In_ double dividend,
    _In_ double divisor)
{
    int quotient;

    return remquo(dividend, divisor, &quotient);
}

float
remainderf(
    _In_ float dividend,
    _In_ float divisor)
{
    int quotient;

    return remquof(dividend, divisor, &quotient);
}

long double
remainderl(
    _In_ long double dividend,
    _In_ long double divisor)
{
    return remainder(dividend, divisor);
}
