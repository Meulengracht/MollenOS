/*
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <internal/_locale.h>

#define LCCTYPE_SIZE (sizeof(struct lc_ctype_T) / sizeof(char *))

static char	numone[] = { '\1', '\0'};

const struct lc_ctype_T _C_ctype_locale = {
	"ASCII",			/* codeset */
	numone				/* mb_cur_max */
#ifdef __HAVE_LOCALE_INFO_EXTENDED__
	,
	{ "0", "1", "2", "3", "4",	/* outdigits */
	  "5", "6", "7", "8", "9" },
	{ L"0", L"1", L"2", L"3", L"4",	/* woutdigits */
	  L"5", L"6", L"7", L"8", L"9" }
#endif
};

/* NULL locale indicates global locale (called from setlocale) */
int __ctype_load_locale (struct __locale_t *locale, const char *name,
		     void *f_wctomb, const char *charset, int mb_cur_max)
{
	int ret = 0;
	struct lc_ctype_T ct;
	char *bufp = NULL;
	
	// @todo
	_CRT_UNUSED(bufp);
	_CRT_UNUSED(ct);
	
	_CRT_UNUSED(locale);
	_CRT_UNUSED(name);
	_CRT_UNUSED(f_wctomb);
	_CRT_UNUSED(mb_cur_max);
	_CRT_UNUSED(charset);
	return ret;
}
