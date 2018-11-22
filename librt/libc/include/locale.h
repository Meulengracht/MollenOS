/* MollenOS
 *
 * Copyright 2011 - 2016, Philip Meulengracht
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
 * along with this program.If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS C Library - Standard Definitions - C
 */

#ifndef _LOCALE_H_
#define _LOCALE_H_

/* Includes */
#define __need_NULL
#include <crtdefs.h>
#include <stddef.h>

/* Shorthands for the locale-structures
 * they are private and not exposed to the
 * user-environment */
struct __locale_t;
typedef struct __locale_t *locale_t;

/* Locale types, the different areas where
 * formatting changes due to localization */
#define LC_ALL	    0
#define LC_COLLATE  1
#define LC_CTYPE    2
#define LC_MONETARY 3
#define LC_NUMERIC  4
#define LC_TIME     5
#define LC_MESSAGES 6

/* Masks, needed for internal stuff */
#define LC_ALL_MASK			(1 << LC_ALL)
#define LC_COLLATE_MASK		(1 << LC_COLLATE)
#define LC_CTYPE_MASK		(1 << LC_CTYPE)
#define LC_MONETARY_MASK	(1 << LC_MONETARY)
#define LC_NUMERIC_MASK		(1 << LC_NUMERIC)
#define LC_TIME_MASK		(1 << LC_TIME)
#define LC_MESSAGES_MASK	(1 << LC_MESSAGES)

#define LC_GLOBAL_LOCALE	((struct __locale_t *) -1)

/* Cpp-Guard */
#ifdef __cplusplus
extern "C" {
#endif

/* The lconv structure 
 * Formatting info for numeric values  */
struct lconv {
	char *decimal_point;
	char *thousands_sep;
	char *grouping;
	char *int_curr_symbol;
	char *currency_symbol;
	char *mon_decimal_point;
	char *mon_thousands_sep;
	char *mon_grouping;
	char *positive_sign;
	char *negative_sign;
	char int_frac_digits;
	char frac_digits;
	char p_cs_precedes;
	char p_sep_by_space;
	char n_cs_precedes;
	char n_sep_by_space;
	char p_sign_posn;
	char n_sign_posn;
	char int_n_cs_precedes;
	char int_n_sep_by_space;
	char int_n_sign_posn;
	char int_p_cs_precedes;
	char int_p_sep_by_space;
	char int_p_sign_posn;
	char padding[2];
};

/* The setlocale function
 * Used to update the current localization */
_CRTIMP char *setlocale(int, __CONST char *);

/* The localeconv function
 * Get locale formatting parameters for quantities */
_CRTIMP struct lconv *localeconv(void);

/* The below functions are not actually strictly defined
 * by the C standard, but rather by the POSIX standard */
_CRTIMP locale_t newlocale(int, __CONST char *, locale_t);
_CRTIMP void freelocale(locale_t);
_CRTIMP locale_t duplocale(locale_t);
_CRTIMP locale_t uselocale(locale_t);

#ifdef __cplusplus
}
#endif

#endif /* _LOCALE_H_ */
