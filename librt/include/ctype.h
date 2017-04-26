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
* MollenOS C Library - Standard Library Conversion Macros
*/

#ifndef _CTYPE_H_
#define _CTYPE_H_

/* Includes */
#include <crtdefs.h>

/* CPP Guard */
#ifdef __cplusplus
extern "C"
{
#endif

/* Checks for an alphanumeric character. */
_CRTIMP int isalnum(int __c);
/* Checks for an alphabetic character. */
_CRTIMP int isalpha(int __c);
/* Checks for a control character. */
_CRTIMP int iscntrl(int __c);
/* Checks for a digit (0 through 9). */
_CRTIMP int isdigit(int __c);
/* Checks for any printable character except space. */
_CRTIMP int isgraph(int __c);
/* Checks for a lower-case character. */
_CRTIMP int islower(int __c);
/* Checks for any printable character including space. */
_CRTIMP int isprint(int __c);
/* Checks for any printable character which is not a space
 * or an alphanumeric character. */
_CRTIMP int ispunct(int __c);
/* Checks for white-space characters. */
_CRTIMP int isspace(int __c);
/* Checks for an uppercase letter. */
_CRTIMP int isupper(int __c);
/* Checks for a hexadecimal digits. */
_CRTIMP int isxdigit(int __c);

/* Checks whether c is a 7-bit unsigned char value that
 * fits into the ASCII character set. */
_CRTIMP int tolower(int __c);
_CRTIMP int toupper(int __c);

/* <<isblank>> is a function which classifies singlebyte charset values by table
 * lookup.  It is a predicate returning non-zero for blank characters, and 0
 * for other characters.  It is defined only if <[c]> is representable as an
 * unsigned char or if <[c]> is EOF. */
_CRTIMP int isblank(int __c);

/* Determine whether or not the given character 
 * is a valid ASCII character */
_CRTIMP int isascii(int __c);

/* <<toascii>> is a macro which coerces integers 
 * to the ASCII range (0--127) by zeroing any higher-order bits. */
_CRTIMP int toascii(int __c);
#define _tolower(__c) ((unsigned char)(__c) - 'A' + 'a')
#define _toupper(__c) ((unsigned char)(__c) - 'a' + 'A')

#if defined(__POSIX_VISIBLE)
extern int isalnum_l(int __c, locale_t __l);
extern int isalpha_l(int __c, locale_t __l);
extern int isblank_l(int __c, locale_t __l);
extern int iscntrl_l(int __c, locale_t __l);
extern int isdigit_l(int __c, locale_t __l);
extern int isgraph_l(int __c, locale_t __l);
extern int islower_l(int __c, locale_t __l);
extern int isprint_l(int __c, locale_t __l);
extern int ispunct_l(int __c, locale_t __l);
extern int isspace_l(int __c, locale_t __l);
extern int isupper_l(int __c, locale_t __l);
extern int isxdigit_l(int __c, locale_t __l);
extern int tolower_l(int __c, locale_t __l);
extern int toupper_l(int __c, locale_t __l);
extern int isascii_l(int __c, locale_t __l);
extern int toascii_l(int __c, locale_t __l);
#endif

#define	_CTYPE_U	01
#define	_CTYPE_L	02
#define	_CTYPE_N	04
#define	_CTYPE_S	010
#define _CTYPE_P	020
#define _CTYPE_C	040
#define _CTYPE_X	0100
#define	_CTYPE_B	0200

_CRTIMP const char *__locale_ctype_ptr(void);
# define __CTYPE_PTR	(__locale_ctype_ptr ())

#ifndef __cplusplus
/* These macros are intentionally written in a manner that will trigger
a gcc -Wall warning if the user mistakenly passes a 'char' instead
of an int containing an 'unsigned char'.  Note that the sizeof will
always be 1, which is what we want for mapping EOF to __CTYPE_PTR[0];
the use of a raw index inside the sizeof triggers the gcc warning if
__c was of type char, and sizeof masks side effects of the extra __c.
Meanwhile, the real index to __CTYPE_PTR+1 must be cast to int,
since isalpha(0x100000001LL) must equal isalpha(1), rather than being
an out-of-bounds reference on a 64-bit machine.  */
#define __ctype_lookup(__c) ((__CTYPE_PTR+sizeof(""[__c]))[(int)(__c)])

#define	isalpha(__c)	(__ctype_lookup(__c)&(_CTYPE_U|_CTYPE_L))
#define	isupper(__c)	((__ctype_lookup(__c)&(_CTYPE_U|_CTYPE_L))==_CTYPE_U)
#define	islower(__c)	((__ctype_lookup(__c)&(_CTYPE_U|_CTYPE_L))==_CTYPE_L)
#define	isdigit(__c)	(__ctype_lookup(__c)&_CTYPE_N)
#define	isxdigit(__c)	(__ctype_lookup(__c)&(_CTYPE_X|_CTYPE_N))
#define	isspace(__c)	(__ctype_lookup(__c)&_CTYPE_S)
#define ispunct(__c)	(__ctype_lookup(__c)&_CTYPE_P)
#define isalnum(__c)	(__ctype_lookup(__c)&(_CTYPE_U|_CTYPE_L|_CTYPE_N))
#define isprint(__c)	(__ctype_lookup(__c)&(_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_N|_CTYPE_B))
#define	isgraph(__c)	(__ctype_lookup(__c)&(_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_N))
#define iscntrl(__c)	(__ctype_lookup(__c)&_CTYPE_C)

#if defined(__GNUC__) && __ISO_C_VISIBLE >= 1999
#define isblank(__c) \
  __extension__ ({ __typeof__ (__c) __x = (__c);		\
        (__ctype_lookup(__x)&_B) || (int) (__x) == '\t';})
#endif

#if defined(__POSIX_VISIBLE)
const char *__locale_ctype_ptr_l(locale_t);
#define __ctype_lookup_l(__c,__l) ((__locale_ctype_ptr_l(__l)+sizeof(""[__c]))[(int)(__c)])

#define	isalpha_l(__c,__l)	(__ctype_lookup_l(__c,__l)&(_CTYPE_U|_CTYPE_L))
#define	isupper_l(__c,__l)	((__ctype_lookup_l(__c,__l)&(_CTYPE_U|_CTYPE_L))==_CTYPE_U)
#define	islower_l(__c,__l)	((__ctype_lookup_l(__c,__l)&(_CTYPE_U|_CTYPE_L))==_CTYPE_L)
#define	isdigit_l(__c,__l)	(__ctype_lookup_l(__c,__l)&_CTYPE_N)
#define	isxdigit_l(__c,__l)	(__ctype_lookup_l(__c,__l)&(_CTYPE_X|_CTYPE_N))
#define	isspace_l(__c,__l)	(__ctype_lookup_l(__c,__l)&_CTYPE_S)
#define ispunct_l(__c,__l)	(__ctype_lookup_l(__c,__l)&_CTYPE_P)
#define isalnum_l(__c,__l)	(__ctype_lookup_l(__c,__l)&(_CTYPE_U|_CTYPE_L|_CTYPE_N))
#define isprint_l(__c,__l)	(__ctype_lookup_l(__c,__l)&(_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_N|_CTYPE_B))
#define	isgraph_l(__c,__l)	(__ctype_lookup_l(__c,__l)&(_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_N))
#define iscntrl_l(__c,__l)	(__ctype_lookup_l(__c,__l)&_CTYPE_C)

#if defined(__GNUC__)
#define isblank_l(__c, __l) \
  __extension__ ({ __typeof__ (__c) __x = (__c);		\
        (__ctype_lookup_l(__x,__l)&_B) || (int) (__x) == '\t';})
#endif

#endif /* __POSIX_VISIBLE >= 200809 */

#if defined(__MISC_VISIBLE) || defined(__XSI_VISIBLE)
#define isascii(__c)	((unsigned)(__c)<=0177)
#define toascii(__c)	((__c)&0177)
#endif

#if defined(__MISC_VISIBLE)
#define isascii_l(__c,__l)	((__l),(unsigned)(__c)<=0177)
#define toascii_l(__c,__l)	((__l),(__c)&0177)
#endif

/* Non-gcc versions will get the library versions, and will be
slightly slower.  These macros are not NLS-aware so they are
disabled if the system supports the extended character sets. */
# if defined(__GNUC__)
#  if !defined (_MB_EXTENDED_CHARSETS_ISO) && !defined (_MB_EXTENDED_CHARSETS_WINDOWS)
#   define toupper(__c) \
  __extension__ ({ __typeof__ (__c) __x = (__c);	\
      islower (__x) ? (int) __x - 'a' + 'A' : (int) __x;})
#   define tolower(__c) \
  __extension__ ({ __typeof__ (__c) __x = (__c);	\
      isupper (__x) ? (int) __x - 'A' + 'a' : (int) __x;})
#  else /* _MB_EXTENDED_CHARSETS* */
/* Allow a gcc warning if the user passed 'char', but defer to the
function.  */
#   define toupper(__c) \
  __extension__ ({ __typeof__ (__c) __x = (__c);	\
      (void) __CTYPE_PTR[__x]; (toupper) (__x);})
#   define tolower(__c) \
  __extension__ ({ __typeof__ (__c) __x = (__c);	\
      (void) __CTYPE_PTR[__x]; (tolower) (__x);})
#  endif /* _MB_EXTENDED_CHARSETS* */
# endif /* __GNUC__ */
#endif /* !__cplusplus */

/* For C++ backward-compatibility only.  */
_CRTDATA(__EXTERN __CONST char _ctype_[]);

#ifdef __cplusplus
}
#endif

#endif /* _CTYPE_H_ */
