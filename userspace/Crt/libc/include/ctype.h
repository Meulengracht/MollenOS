/* MollenOS
	Character macros

*/

#ifndef __CTYPE_INC__
#define __CTYPE_INC__

#ifdef _MSC_VER
// Get rid of conversion warnings
#pragma warning (disable:4244)
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define _CTYPE_U      0x01    /* upper */
#define _CTYPE_L      0x02    /* lower */
#define _CTYPE_D      0x04    /* digit */
#define _CTYPE_C      0x08    /* cntrl */
#define _CTYPE_P      0x10    /* punct */
#define _CTYPE_S      0x20    /* white space (space/lf/tab) */
#define _CTYPE_X      0x40    /* hex digit */
#define _CTYPE_SP     0x80    /* hard space (0x20) */

extern unsigned char _ctype[];

#define __ismask(x) (_ctype[(int)(unsigned char)(x)])

/* Checks for an alphanumeric character. */
#define isalnum(c)      ((__ismask(c)&(_CTYPE_U|_CTYPE_L|_CTYPE_D)) != 0)
/* Checks for an alphabetic character. */
#define isalpha(c)      ((__ismask(c)&(_CTYPE_U|_CTYPE_L)) != 0)
/* Checks for a control character. */
#define iscntrl(c)      ((__ismask(c)&(_CTYPE_C)) != 0)
/* Checks for a digit (0 through 9). */
#define isdigit(c)      ((__ismask(c)&(_CTYPE_D)) != 0)
/* Checks for any printable character except space. */
#define isgraph(c)      ((__ismask(c)&(_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_D)) != 0)
/* Checks for a lower-case character. */
#define islower(c)      ((__ismask(c)&(_CTYPE_L)) != 0)
/* Checks for any printable character including space. */
#define isprint(c)      ((__ismask(c)&(_CTYPE_P|_CTYPE_U|_CTYPE_L|_CTYPE_D|_CTYPE_SP)) != 0)
/* Checks for any printable character which is not a space
 * or an alphanumeric character. */
#define ispunct(c)      ((__ismask(c)&(_CTYPE_P)) != 0)
/* Checks for white-space characters. */
#define isspace(c)      ((__ismask(c)&(_CTYPE_S)) != 0)
/* Checks for an uppercase letter. */
#define isupper(c)      ((__ismask(c)&(_CTYPE_U)) != 0)
/* Checks for a hexadecimal digits. */
#define isxdigit(c)     ((__ismask(c)&(_CTYPE_D|_CTYPE_X)) != 0)

/* Checks whether c is a 7-bit unsigned char value that
 * fits into the ASCII character set. */
#define isascii(c) (((unsigned char)(c))<=0x7f)
#define toascii(c) (((unsigned char)(c))&0x7f)

__inline unsigned char __tolower(unsigned char c) {
        return isupper(c) ? c - ('A' - 'a') : c;
}

__inline unsigned char __toupper(unsigned char c) {
        return islower(c) ? c - ('a' - 'A') : c;
}

/* Convert a character to lower case */
#define tolower(c) __tolower(c)

/* Convert a character to upper case */
#define toupper(c) __toupper(c)

#ifdef __cplusplus
}
#endif

#endif