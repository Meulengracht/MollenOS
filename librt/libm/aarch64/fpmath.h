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

#ifndef __AARCH64_FPMATH_H__
#define __AARCH64_FPMATH_H__

// Vali uses the COFF binary64 representation for long double on AArch64.
union IEEEl2bits {
    long double e;
    struct {
        unsigned int manl : 32;
        unsigned int manh : 20;
        unsigned int exp : 11;
        unsigned int sign : 1;
    } bits;
};

#define LDBL_MANH_SIZE 20
#define LDBL_MANL_SIZE 32
#define LDBL_IMPLICIT_NBIT
#define mask_nbit_l(value) ((void)0)

#endif //!__AARCH64_FPMATH_H__
