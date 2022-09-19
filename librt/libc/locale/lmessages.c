/*
 * Copyright (c) 2001 Alexey Zelkin <phantom@FreeBSD.org>
 * All rights reserved.
 *
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

/* Includes */
#include <stddef.h>
#include <internal/_locale.h>

#define LCMESSAGES_SIZE_FULL (sizeof(struct lc_messages_T) / sizeof(char *))
#define LCMESSAGES_SIZE_MIN \
		(offsetof(struct lc_messages_T, yesstr) / sizeof(char *))

//static char empty[] = "";

const struct lc_messages_T _C_messages_locale = {
	"^[yY]" ,	/* yesexpr */
	"^[nN]" ,	/* noexpr */
	"yes" , 	/* yesstr */
	"no"		/* nostr */
#ifdef __HAVE_LOCALE_INFO_EXTENDED__
	, "ASCII" ,	/* codeset */
	L"^[yY]" ,	/* wyesexpr */
	L"^[nN]" ,	/* wnoexpr */
	L"yes" , 	/* wyesstr */
	L"no"		/* wnostr */
#endif
};

//static struct lc_messages_T _messages_locale;
//static int	_messages_using_locale;
//static char	*_messages_locale_buf;

int __messages_load_locale (struct __locale_t *locale, const char *name,
			void *f_wctomb, const char *charset)
{
	int ret = 0;
	struct lc_messages_T me;
	char *bufp = NULL;

	// @todo
	_CRT_UNUSED(bufp);
	_CRT_UNUSED(me);
	_CRT_UNUSED(locale);
	_CRT_UNUSED(name);
	_CRT_UNUSED(f_wctomb);
	_CRT_UNUSED(charset);
  return ret;
}
