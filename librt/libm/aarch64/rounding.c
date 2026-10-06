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

long
lrint(
    _In_ double value)
{
    double rounded;
    int32_t result;

    // Round in the current FPCR mode before the saturating integer conversion.
    // Both instructions report exceptions through FPSR, including invalid
    // conversions for NaNs and values outside the destination integer range.
    __asm__ volatile("frintx %d0, %d1" : "=w"(rounded) : "w"(value));
    __asm__ volatile("fcvtzs %w0, %d1" : "=r"(result) : "w"(rounded));
    return result;
}

long
lrintf(
    _In_ float value)
{
    float rounded;
    int32_t result;

    // Round in the current FPCR mode before the saturating integer conversion.
    // Both instructions report exceptions through FPSR, including invalid
    // conversions for NaNs and values outside the destination integer range.
    __asm__ volatile("frintx %s0, %s1" : "=w"(rounded) : "w"(value));
    __asm__ volatile("fcvtzs %w0, %s1" : "=r"(result) : "w"(rounded));
    return result;
}

long
lrintl(
    _In_ long double value)
{
    return lrint(value);
}

long long
llrint(
    _In_ double value)
{
    double rounded;
    long long result;

    // Round in the current FPCR mode before the saturating integer conversion.
    // Both instructions report exceptions through FPSR, including invalid
    // conversions for NaNs and values outside the destination integer range.
    __asm__ volatile("frintx %d0, %d1" : "=w"(rounded) : "w"(value));
    __asm__ volatile("fcvtzs %0, %d1" : "=r"(result) : "w"(rounded));
    return result;
}

long long
llrintf(
    _In_ float value)
{
    float rounded;
    long long result;

    // Round in the current FPCR mode before the saturating integer conversion.
    // Both instructions report exceptions through FPSR, including invalid
    // conversions for NaNs and values outside the destination integer range.
    __asm__ volatile("frintx %s0, %s1" : "=w"(rounded) : "w"(value));
    __asm__ volatile("fcvtzs %0, %s1" : "=r"(result) : "w"(rounded));
    return result;
}

long long
llrintl(
    _In_ long double value)
{
    return llrint(value);
}

