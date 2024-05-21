/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __UNICODE_PRIVATE_H__
#define __UNICODE_PRIVATE_H__

// include generated files
#include "mstr_digits.h"
#include "mstr_conv.h"
#include "mstr_props.h"
#include <ds/mstring.h>

// lookup unicode number descriptor
extern const __unicode_digit_t* __lookup_number(mchar_t val);

// lookup unicode properties
extern const __unicode_ctype_t* __lookup_props(mchar_t val);

#endif //!__UNICODE_PRIVATE_H__
