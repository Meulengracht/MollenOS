/*
 * COPYRIGHT:       GNU GPL, see COPYING in the top level directory
 * PROJECT:         ReactOS crt library
 * FILE:            lib/sdk/crt/printf/streamout.c
 * PURPOSE:         Implementation of streamout
 * PROGRAMMER:      Timo Kreuzer
 */

#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <float.h>
#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#define MB_CUR_MAX 10
#define BUFFER_SIZE (32 + 17)

typedef struct _STRING
{
	unsigned short Length;
	unsigned short MaximumLength;
	void *Buffer;
} STRING;

enum
{
	/* Formatting flags */
	FLAG_ALIGN_LEFT =    0x01,
	FLAG_FORCE_SIGN =    0x02,
	FLAG_FORCE_SIGNSP =  0x04,
	FLAG_PAD_ZERO =      0x08,
	FLAG_SPECIAL =       0x10,

	/* Data format flags */
	FLAG_SHORT =         0x100,
	FLAG_LONG =          0x200,
	FLAG_WIDECHAR =      FLAG_LONG,
	FLAG_INT64 =         0x400,
#ifdef _WIN64
	FLAG_INTPTR =        FLAG_INT64,
#else
	FLAG_INTPTR =        0,
#endif
	FLAG_LONGDOUBLE =    0x800,
};

#define va_arg_f(argptr, flags) \
	(flags & FLAG_INT64) ? va_arg(argptr, __int64) : \
	(flags & FLAG_SHORT) ? (short)va_arg(argptr, int) : \
	va_arg(argptr, int)

#define va_arg_fu(argptr, flags) \
	(flags & FLAG_INT64) ? va_arg(argptr, unsigned __int64) : \
	(flags & FLAG_SHORT) ? (unsigned short)va_arg(argptr, int) : \
	va_arg(argptr, unsigned int)

#define va_arg_ffp(argptr, flags) \
	(flags & FLAG_LONGDOUBLE) ? va_arg(argptr, long double) : \
	va_arg(argptr, double)

#define get_exp(f) (int)floor(f == 0 ? 0 : (f >= 0 ? log10(f) : log10(-f)))
#define round(x) floor((x) + 0.5)

/* Helpers */
#define IsUTF8(Character) (((Character) & 0xC0) == 0x80)

#ifdef LIBC_KERNEL
extern int VideoPutChar(int Character);
#endif

/* Encoding U32 back to UTF-8 */
/* Converts a single char to UTF8 */
int StreamCharacterToUtf8(uint32_t Character, void* oBuffer, uint32_t *Length)
{
	/* Encode Buffer */
	char TmpBuffer[10] = { 0 };
	char* BufPtr = &TmpBuffer[0];

	uint32_t NumBytes = 0;
	uint32_t Error = 0;

	if (Character <= 0x7F)  /* 0XXX XXXX one byte */
	{
		TmpBuffer[0] = (char)Character;
		NumBytes = 1;
	}
	else if (Character <= 0x7FF)  /* 110X XXXX  two bytes */
	{
		TmpBuffer[0] = (char)(0xC0 | (Character >> 6));
		TmpBuffer[1] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 2;
	}
	else if (Character <= 0xFFFF)  /* 1110 XXXX  three bytes */
	{
		TmpBuffer[0] = (char)(0xE0 | (Character >> 12));
		TmpBuffer[1] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[2] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 3;

		/* Sanity no special characters */
		if (Character == 0xFFFE || Character == 0xFFFF)
			Error = 1;
	}
	else if (Character <= 0x1FFFFF)  /* 1111 0XXX  four bytes */
	{
		TmpBuffer[0] = (char)(0xF0 | (Character >> 18));
		TmpBuffer[1] = (char)(0x80 | ((Character >> 12) & 0x3F));
		TmpBuffer[2] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[3] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 4;

		if (Character > 0x10FFFF)
			Error = 1;
	}
	else if (Character <= 0x3FFFFFF)  /* 1111 10XX  five bytes */
	{
		TmpBuffer[0] = (char)(0xF8 | (Character >> 24));
		TmpBuffer[1] = (char)(0x80 | (Character >> 18));
		TmpBuffer[2] = (char)(0x80 | ((Character >> 12) & 0x3F));
		TmpBuffer[3] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[4] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 5;
		Error = 1;
	}
	else if (Character <= 0x7FFFFFFF)  /* 1111 110X  six bytes */
	{
		TmpBuffer[0] = (char)(0xFC | (Character >> 30));
		TmpBuffer[1] = (char)(0x80 | ((Character >> 24) & 0x3F));
		TmpBuffer[2] = (char)(0x80 | ((Character >> 18) & 0x3F));
		TmpBuffer[3] = (char)(0x80 | ((Character >> 12) & 0x3F));
		TmpBuffer[4] = (char)(0x80 | ((Character >> 6) & 0x3F));
		TmpBuffer[5] = (char)(0x80 | (Character & 0x3F));
		NumBytes = 6;
		Error = 1;
	}
	else
		Error = 1;

	/* Write buffer only if it's a valid byte sequence */
	if (!Error && oBuffer != NULL)
		memcpy(oBuffer, BufPtr, NumBytes);

	/* We want the length */
	*Length = NumBytes;

	/* Sanity */
	if (Error)
		return -1;
	else
		return 0;
}

/* PutChar Wrapper 
 * Supports UTF-8 && Unicode */
static int StreamOutCharacter(char **oStream, uint32_t *oLen, uint32_t Character)
{
	/* Sanity */
	if (*oLen == 0)
		return 0;

	/* Do we have a stream to write to? */
	if (oStream) {
		/* We cannot just copy an U32 character 
		 * to stream like this, we must encode it back */
		uint32_t uLen = 0;
		StreamCharacterToUtf8(Character, *oStream, &uLen);
		(*oStream) += uLen;
		*oLen -= uLen;

		/* Done ! */
		return Character;
	}
	else 
	{
		/* These routines need the 
		 * unicode-point */
#ifndef LIBC_KERNEL
		return putchar(Character);
#else
		return VideoPutChar(Character);
#endif
	}
}

static int streamout_astring(char **out, uint32_t *cnt, const char *string, size_t count)
{
	char chr;
	int written = 0;

	while (count--)
	{
		chr = *string++;
		if (StreamOutCharacter(out, cnt, chr) == 0) return -1;
		written++;
	}

	return written;
}

static int streamout_wstring(char **out, uint32_t *cnt, const wchar_t *string, size_t count)
{
	wchar_t chr;
	int written = 0;

	while (count--)
	{
		chr = *string++;
		{
			if (StreamOutCharacter(out, cnt, chr) == 0) return -1;
			written++;
		}
	}

	return written;
}

void format_float(char chr, unsigned int flags, int precision,
					char **string, const char **prefix, va_list *argptr)
{
	static const char digits_l[] = ("0123456789abcdef0x");
	static const char digits_u[] = ("0123456789ABCDEF0X");
	static const char _nan[] = ("#QNAN");
	static const char _infinity[] = ("#INF");
	const char *digits = digits_l;
	int exponent = 0, sign;
	long double fpval, fpval2;
	int padding = 0, num_digits, val32, base = 10;

	/* Normalize the precision */
	if (precision < 0) precision = 6;
	else if (precision > 17)
	{
		padding = precision - 17;
		precision = 17;
	}

	/* Get the float value and calculate the exponent */
	fpval = va_arg_ffp(*argptr, flags);
	exponent = get_exp((double)fpval);
	sign = fpval < 0 ? -1 : 1;

	switch (chr)
	{
	case ('G'):
		digits = digits_u;
	case ('g'):
		if (precision > 0) precision--;
		if (exponent < -4 || exponent >= precision) goto case_e;

		/* Shift the decimal point and round */
		fpval2 = round(sign * (double)fpval * pow(10., precision));

		/* Skip trailing zeroes */
		while (precision && (unsigned __int64)fpval2 % 10 == 0)
		{
			precision--;
			fpval2 /= 10;
		}
		break;

	case ('E'):
		digits = digits_u;
	case ('e'):
case_e:
		/* Shift the decimal point and round */
		fpval2 = round(sign * (double)fpval * pow(10., precision - exponent));

		/* Compensate for changed exponent through rounding */
		if (fpval2 >= (unsigned __int64)pow(10., precision + 1))
		{
			exponent++;
			fpval2 = round(sign * (double)fpval * pow(10., precision - exponent));
		}

		val32 = exponent >= 0 ? exponent : -exponent;

		// FIXME: handle length of exponent field:
		// http://msdn.microsoft.com/de-de/library/0fatw238%28VS.80%29.aspx
		num_digits = 3;
		while (num_digits--)
		{
			*--(*string) = digits[val32 % 10];
			val32 /= 10;
		}

		/* Sign for the exponent */
		*--(*string) = (exponent >= 0 ? ('+') : ('-'));

		/* Add 'e' or 'E' separator */
		*--(*string) = digits[0xe];
		break;

	case ('A'):
		digits = digits_u;
	case ('a'):
		//            base = 16;
		// FIXME: TODO

	case ('f'):
	default:
		/* Shift the decimal point and round */
		fpval2 = round(sign * (double)fpval * pow(10., precision));
		break;
	}

	/* Handle sign */
	if (fpval < 0)
	{
		*prefix = ("-");
	}
	else if (flags & FLAG_FORCE_SIGN)
		*prefix = ("+");
	else if (flags & FLAG_FORCE_SIGNSP)
		*prefix = (" ");

	/* Handle special cases first */
	if (_isnan((double)fpval))
	{
		(*string) -= sizeof(_nan) / sizeof(char) - 1;
		strcpy((*string), _nan);
		fpval2 = 1;
	}
	else if (!_finite((double)fpval))
	{
		(*string) -= sizeof(_infinity) / sizeof(char) - 1;
		strcpy((*string), _infinity);
		fpval2 = 1;
	}
	else
	{
		/* Zero padding */
		while (padding-- > 0) *--(*string) = ('0');

		/* Digits after the decimal point */
		num_digits = precision;
		while (num_digits-- > 0)
		{
			*--(*string) = digits[(unsigned __int64)fpval2 % 10];
			fpval2 /= base;
		}
	}

	if (precision > 0 || flags & FLAG_SPECIAL)
		*--(*string) = ('.');

	/* Digits before the decimal point */
	do
	{
		*--(*string) = digits[(unsigned __int64)fpval2 % base];
		fpval2 /= base;
	}
	while ((unsigned __int64)fpval2);

}

#define streamout_string streamout_astring
#define USE_MULTISIZE 1

int _cdecl streamout(char **out, size_t size, const char *format, va_list argptr)
{
	static const char digits_l[] = "0123456789abcdef0x";
	static const char digits_u[] = "0123456789ABCDEF0X";
	static const char *_nullstring = "(null)";
	char buffer[BUFFER_SIZE + 1];
	uint32_t cnt = size;
	char chr, *string;
	STRING *nt_string;
	const char *digits, *prefix;
	int base, fieldwidth, precision, padding;
	size_t prefixlen, len;
	int written = 1, written_all = 0;
	unsigned int flags;
	unsigned __int64 val64;

	buffer[BUFFER_SIZE] = '\0';

	/* Iterate String */
	while (written >= 0)
	{
		/* Get character and advance */
		chr = *format++;

		/* Check for end of format string */
		if (chr == '\0') 
			break;

		/* Check for 'normal' character or double % */
		if ((chr != ('%')) ||
			(chr = *format++) == ('%'))
		{
			/* Sanity */
			if (IsUTF8(chr))
			{
				/* Build UTF-8 */
				uint32_t uChar = (uint32_t)chr;
				uint32_t Size = 0;

				/* Iterate */
				while (*format && IsUTF8(*format))
				{
					/* Move */
					uChar <<= 6;

					/* Add */
					uChar += (unsigned char)*format;

					/* Inc */
					Size++;
					format++;
				}

				/* Move */
				uChar <<= 6;

				/* Add the last byte */
				if (Size == 1)
					uChar |= (((unsigned char)*format) & 0x1F);
				else if (Size == 2)
					uChar |= (((unsigned char)*format) & 0xF);
				else if (Size == 3)
					uChar |= (((unsigned char)*format) & 0x7);

				/* Write the character to the stream */
				if ((written = StreamOutCharacter(out, &cnt, uChar)) == 0)
					return -1;
			}
			else
			{
				/* Write the character to the stream */
				if ((written = StreamOutCharacter(out, &cnt, chr)) == 0)
					return -1;
			}

			/* Done */
			written_all += written;
			continue;
		}

		/* Handle flags-characters */
		flags = 0;
		while (1)
		{
			if (chr == ('-')) flags |= FLAG_ALIGN_LEFT;
			else if (chr == ('+')) flags |= FLAG_FORCE_SIGN;
			else if (chr == (' ')) flags |= FLAG_FORCE_SIGNSP;
			else if (chr == ('0')) flags |= FLAG_PAD_ZERO;
			else if (chr == ('#')) flags |= FLAG_SPECIAL;
			else break;
			chr = *format++;
		}

		/* Handle field width modifier */
		if (chr == ('*'))
		{
			fieldwidth = va_arg(argptr, int);
			if (fieldwidth < 0)
			{
				flags |= FLAG_ALIGN_LEFT;
				fieldwidth = -fieldwidth;
			}
			chr = *format++;
		}
		else
		{
			fieldwidth = 0;
			while (chr >= ('0') && chr <= ('9'))
			{
				fieldwidth = fieldwidth * 10 + (chr - ('0'));
				chr = *format++;
			}
		}

		/* Handle precision modifier */
		if (chr == '.')
		{
			chr = *format++;

			if (chr == ('*'))
			{
				precision = va_arg(argptr, int);
				chr = *format++;
			}
			else
			{
				precision = 0;
				while (chr >= ('0') && chr <= ('9'))
				{
					precision = precision * 10 + (chr - ('0'));
					chr = *format++;
				}
			}
		}
		else precision = -1;

		/* Handle argument size prefix */
		do
		{
			if (chr == ('h')) flags |= FLAG_SHORT;
			else if (chr == ('w')) flags |= FLAG_WIDECHAR;
			else if (chr == ('L')) flags |= 0; // FIXME: long double
			else if (chr == ('F')) flags |= 0; // FIXME: what is that?
			else if (chr == ('l'))
			{
				/* Check if this is the 2nd 'l' in a row */
				if (format[-2] == 'l') flags |= FLAG_INT64;
				else flags |= FLAG_LONG;
			}
			else if (chr == ('I'))
			{
				if (format[0] == ('3') && format[1] == ('2'))
				{
					format += 2;
				}
				else if (format[0] == ('6') && format[1] == ('4'))
				{
					format += 2;
					flags |= FLAG_INT64;
				}
				else if (format[0] == ('x') || format[0] == ('X') ||
					format[0] == ('d') || format[0] == ('i') ||
					format[0] == ('u') || format[0] == ('o'))
				{
					flags |= FLAG_INTPTR;
				}
				else break;
			}
			else break;
			chr = *format++;
		}
		while (USE_MULTISIZE);

		/* Handle the format specifier */
		digits = digits_l;
		string = &buffer[BUFFER_SIZE];
		base = 10;
		prefix = 0;
		switch (chr)
		{
		case ('n'):
			if (flags & FLAG_INT64)
				*va_arg(argptr, __int64*) = written_all;
			else if (flags & FLAG_SHORT)
				*va_arg(argptr, short*) = (short)written_all;
			else
				*va_arg(argptr, int*) = written_all;
			continue;

		case ('C'):
			if (!(flags & FLAG_SHORT)) flags |= FLAG_WIDECHAR;
			goto case_char;

		case ('c'):
case_char:
			string = buffer;
			len = 1;
 			if (flags & FLAG_WIDECHAR)
 			{
 				((wchar_t*)string)[0] = (wchar_t)va_arg(argptr, int);
 				((wchar_t*)string)[1] = (wchar_t)('\0');
 			}
 			else
 			{
				((char*)string)[0] = (char)va_arg(argptr, int);
				((char*)string)[1] = (char)('\0');
			}
			break;

		case ('Z'):
			nt_string = va_arg(argptr, void*);
			if (nt_string && (string = nt_string->Buffer))
			{
				len = nt_string->Length;
				if (flags & FLAG_WIDECHAR) len /= sizeof(wchar_t);
				break;
			}
			string = 0;
			goto case_string;

		case ('S'):
			string = va_arg(argptr, void*);
			if (!(flags & FLAG_SHORT)) flags |= FLAG_WIDECHAR;
			goto case_string;

		case ('s'):
			string = va_arg(argptr, void*);

case_string:
			if (!string)
			{
				string = (char*)_nullstring;
				flags &= ~FLAG_WIDECHAR;
			}

// 			if (flags & FLAG_WIDECHAR)
// 				len = wcsnlen((wchar_t*)string, (unsigned)precision);
// 			else
				len = strlen((char*)string);
			precision = 0;
			break;

		case ('G'):
		case ('E'):
		case ('A'):
		case ('g'):
		case ('e'):
		case ('a'):
		case ('f'):
#ifdef _UNICODE
			flags |= FLAG_WIDECHAR;
#else
			flags &= ~FLAG_WIDECHAR;
#endif
			/* Use external function, one for kernel one for user mode */
			format_float(chr, flags, precision, &string, &prefix, &argptr);
			len = strlen(string);
			precision = 0;
			break;

		case ('d'):
		case ('i'):
			val64 = (__int64)va_arg_f(argptr, flags);

			if ((__int64)val64 < 0)
			{
				val64 = -(__int64)val64;
				prefix = ("-");
			}
			else if (flags & FLAG_FORCE_SIGN)
				prefix = ("+");
			else if (flags & FLAG_FORCE_SIGNSP)
				prefix = (" ");

			goto case_number;

		case ('o'):
			base = 8;
			if (flags & FLAG_SPECIAL)
			{
				prefix = ("0");
				if (precision > 0) precision--;
			}
			goto case_unsigned;

		case ('p'):
			precision = 2 * sizeof(void*);
			flags &= ~FLAG_PAD_ZERO;
			flags |= FLAG_INTPTR;
			/* Fall through */

		case ('X'):
			digits = digits_u;
			/* Fall through */

		case ('x'):
			base = 16;
			if (flags & FLAG_SPECIAL)
			{
				prefix = &digits[16];
			}

		case ('u'):
case_unsigned:
			val64 = va_arg_fu(argptr, flags);

case_number:
			flags &= ~FLAG_WIDECHAR;
			if (precision < 0) precision = 1;

			/* Gather digits in reverse order */
			while (val64)
			{
				*--string = digits[val64 % base];
				val64 /= base;
				precision--;
			}

			len = strlen(string);
			break;

		default:
			/* Treat anything else as a new character */
			format--;
			continue;
		}

		/* Calculate padding */
		prefixlen = prefix ? strlen(prefix) : 0;
		if (precision < 0) precision = 0;
		padding = (int)(fieldwidth - len - prefixlen - precision);
		if (padding < 0) padding = 0;

		/* Optional left space padding */
		if ((flags & (FLAG_ALIGN_LEFT | FLAG_PAD_ZERO)) == 0)
		{
			for (; padding > 0; padding--)
			{
				if ((written = StreamOutCharacter(out, &cnt, (' '))) == 0) return -1;
				written_all += written;
			}
		}

		/* Optional prefix */
		if (prefix)
		{
			written = streamout_string(out, &cnt, prefix, prefixlen);
			if (written == -1) return -1;
			written_all += written;
		}

		/* Optional left '0' padding */
		if ((flags & FLAG_ALIGN_LEFT) == 0) precision += padding;
		while (precision-- > 0)
		{
			if ((written = StreamOutCharacter(out, &cnt, ('0'))) == 0) return -1;
			written_all += written;
		}

		/* Output the string */
		if (flags & FLAG_WIDECHAR)
			written = streamout_wstring(out, &cnt, (wchar_t*)string, len);
		else
			written = streamout_astring(out, &cnt, (char*)string, len);
		if (written == -1) return -1;
		written_all += written;

#if 0 && SUPPORT_FLOAT
		/* Optional right '0' padding */
		while (precision-- > 0)
		{
			if ((written = StreamOutCharacter(out, &cnt, ('0'))) == 0) return -1;
			written_all += written;
			len++;
		}
#endif

		/* Optional right padding */
		if (flags & FLAG_ALIGN_LEFT)
		{
			while (padding-- > 0)
			{
				if ((written = StreamOutCharacter(out, &cnt, (' '))) == 0) return -1;
				written_all += written;
			}
		}

	}

	if (written == -1) return -1;

	return written_all;
}
