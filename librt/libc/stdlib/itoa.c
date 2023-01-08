
#include <errno.h>
#include <stdint.h>
#include <string.h>

/*
 * @implemented
 */
char *i64toa(int64_t value, char *string, int radix)
{
    uint64_t val;
    int negative;
    char buffer[65];
    char *pos;
    int digit;

    if (value < 0 && radix == 10) {
	negative = 1;
        val = -value;
    } else {
	negative = 0;
        val = value;
    } /* if */

    pos = &buffer[64];
    *pos = '\0';

    do {
	digit = val % radix;
	val = val / radix;
	if (digit < 10) {
	    *--pos = (char)('0' + digit);
	} else {
	    *--pos = (char)('a' + digit - 10);
	} /* if */
    } while (val != 0L);

    if (negative) {
	*--pos = '-';
    } /* if */

    memcpy(string, pos, &buffer[64] - pos + 1);
    return string;
}

/*
 * @implemented
 */
int i64toa_s(int64_t value, char *str, size_t size, int radix)
{
    uint64_t val;
    unsigned int digit;
    int is_negative;
    char buffer[65], *pos;
    size_t len;

    if (!(str != NULL) || !(size > 0) ||
        !(radix >= 2) || !(radix <= 36))
    {
        if (str && size)
            str[0] = '\0';
        *__errno() = EINVAL;
        return EINVAL;
    }

    if (value < 0 && radix == 10)
    {
        is_negative = 1;
        val = -value;
    }
    else
    {
        is_negative = 0;
        val = value;
    }

    pos = buffer + 64;
    *pos = '\0';

    do
    {
        digit = val % radix;
        val /= radix;

        if (digit < 10)
            *--pos = (char)('0' + digit);
        else
            *--pos = (char)('a' + digit - 10);
    }
    while (val != 0);

    if (is_negative)
        *--pos = '-';

    len = buffer + 65 - pos;
    if (len > size)
    {
        size_t i;
        char *p = str;

        /* Copy the temporary buffer backwards up to the available number of
         * characters. Don't copy the negative sign if present. */

        if (is_negative)
        {
            p++;
            size--;
        }

        for (pos = buffer + 63, i = 0; i < size; i++)
            *p++ = *pos--;

        str[0] = '\0';
        //MSVCRT_INVALID_PMT("str[size] is too small");

        *__errno() = ERANGE;
        return ERANGE;
    }

    memcpy(str, pos, len);
    return 0;
}

/*
 * @implemented
 */
char *ui64toa(uint64_t value, char *string, int radix)
{
    char buffer[65];
    char *pos;
    int digit;

    pos = &buffer[64];
    *pos = '\0';

    do {
	digit = value % radix;
	value = value / radix;
	if (digit < 10) {
	    *--pos = (char)('0' + digit);
	} else {
	    *--pos = (char)('a' + digit - 10);
	} /* if */
    } while (value != 0L);

    memcpy(string, pos, &buffer[64] - pos + 1);
    return string;
}

/*
 * @implemented
 */
int ui64toa_s(uint64_t value, char *str, size_t size, int radix)
{
    char buffer[65], *pos;
    int digit;

    if (!(str != NULL) || !(size > 0) ||
        !(radix>=2) || !(radix<=36)) {
        *__errno() = EINVAL;
        return EINVAL;
    }

    pos = buffer+64;
    *pos = '\0';

    do {
        digit = value%radix;
        value /= radix;

        if(digit < 10)
            *--pos = (char)('0' + digit);
        else
            *--pos = (char)('a' + digit - 10);
    }while(value != 0);

    if((unsigned)(buffer-pos+65) > size) {
        //MSVCRT_INVALID_PMT("str[size] is too small");

        *__errno() = EINVAL;
        return EINVAL;
    }

    memcpy(str, pos, buffer-pos+65);
    return 0;
}

/*
 * @implemented
 */
int itoa_s(int value, char *str, size_t size, int radix)
{
    return ltoa_s(value, str, size, radix);
}

/*
 * @implemented
 */
char *itoa(int value, char *string, int radix)
{
  return ltoa(value, string, radix);
}

/*
 * @implemented
 */
char *ltoa(long value, char *string, int radix)
{
    unsigned long val;
    int negative;
    char buffer[33];
    char *pos;
    int digit;

    if (value < 0 && radix == 10) {
	negative = 1;
        val = -value;
    } else {
	negative = 0;
        val = value;
    } /* if */

    pos = &buffer[32];
    *pos = '\0';

    do {
	digit = val % radix;
	val = val / radix;
	if (digit < 10) {
	    *--pos = (char)('0' + digit);
	} else {
	    *--pos = (char)('a' + digit - 10);
	} /* if */
    } while (val != 0L);

    if (negative) {
	*--pos = '-';
    } /* if */

    memcpy(string, pos, &buffer[32] - pos + 1);
    return string;
}

/*
 * @implemented
 */
int ltoa_s(long value, char *str, size_t size, int radix)
{
    unsigned long val;
    unsigned int digit;
    int is_negative;
    char buffer[33], *pos;
    size_t len;

    if (!(str != NULL) || !(size > 0) ||
        (radix >= 2) || !(radix <= 36))
    {
        if (str && size)
            str[0] = '\0';

        *__errno() = EINVAL;
        return EINVAL;
    }

    if (value < 0 && radix == 10)
    {
        is_negative = 1;
        val = -value;
    }
    else
    {
        is_negative = 0;
        val = value;
    }

    pos = buffer + 32;
    *pos = '\0';

    do
    {
        digit = val % radix;
        val /= radix;

        if (digit < 10)
            *--pos = (char)('0' + digit);
        else
            *--pos = (char)('a' + digit - 10);
    }
    while (val != 0);

    if (is_negative)
        *--pos = '-';

    len = buffer + 33 - pos;
    if (len > size)
    {
        size_t i;
        char *p = str;

        /* Copy the temporary buffer backwards up to the available number of
         * characters. Don't copy the negative sign if present. */

        if (is_negative)
        {
            p++;
            size--;
        }

        for (pos = buffer + 31, i = 0; i < size; i++)
            *p++ = *pos--;

        str[0] = '\0';
        //("str[size] is too small");
        *__errno() = EINVAL;
        return ERANGE;
    }

    memcpy(str, pos, len);
    return 0;
}

/*
 * @implemented
 */
char *ultoa(unsigned long value, char *string, int radix)
{
    char buffer[33];
    char *pos;
    int digit;

    pos = &buffer[32];
    *pos = '\0';

    do {
	digit = value % radix;
	value = value / radix;
	if (digit < 10) {
	    *--pos = (char)('0' + digit);
	} else {
	    *--pos = (char)('a' + digit - 10);
	} /* if */
    } while (value != 0L);

    memcpy(string, pos, &buffer[32] - pos + 1);

    return string;
}