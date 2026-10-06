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

// Vali ARM64 uses binary64 for both double and long double.
long double
logl(
    _In_ long double value)
{
    return log(value);
}

long double
log10l(
    _In_ long double value)
{
    return log10(value);
}

long double
logbl(
    _In_ long double value)
{
    return logb(value);
}

long double
rintl(
    _In_ long double value)
{
    return rint(value);
}

long double
expl(
    _In_ long double value)
{
    return exp(value);
}

long double
exp2l(
    _In_ long double value)
{
    return exp2(value);
}

long double
expm1l(
    _In_ long double value)
{
    return expm1(value);
}

long double
powl(
    _In_ long double value,
    _In_ long double exponent)
{
    return pow(value, exponent);
}

long double
tgammal(
    _In_ long double value)
{
    return tgamma(value);
}

long double
lgammal_r(
    _In_  long double value,
    _Out_ int*        sign)
{
    return lgamma_r(value, sign);
}
