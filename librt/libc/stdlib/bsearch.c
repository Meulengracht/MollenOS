/* Copyright (C) 1994 DJ Delorie, see COPYING.DJ for details */
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/*
 * @implemented
 */
void *
bsearch(const void *key,
	const void *base0,
	size_t nelem,
	size_t size,
	int(__cdecl *cmp)(const void *ck, const void *ce))
{
	char *base = (char *)base0;
	size_t lim;
	int cmpval;
	void *p;

	for (lim = nelem; lim != 0; lim >>= 1)
	{
		p = base + (lim >> 1) * size;
		cmpval = (*cmp)(key, p);
		if (cmpval == 0)
			return p;
		if (cmpval > 0)
		{				/* key > p: move right */
			base = (char *)p + size;
			lim--;
		} /* else move left */
	}
	return 0;
}

void * __cdecl _lfind(const void *key,
	const void *base,
	unsigned int *nelp,
	unsigned int width,
	int(__cdecl *compar)(const void *, const void *))
{
	char* char_base = (char*)base;
	unsigned int i;

	for (i = 0; i < *nelp; i++) {
		if (compar(key, char_base) == 0)
			return char_base;
		char_base += width;
	}
	return NULL;
}

void * __cdecl _lsearch(const void *key,
	void *base,
	unsigned int *nelp,
	unsigned int width,
	int(__cdecl *compar)(const void *, const void *))
{
	void *ret_find = _lfind(key, base, nelp, width, compar);

	if (ret_find != NULL)
		return ret_find;

	memcpy((void*)((int*)base + (*nelp*width)), key, width);

	(*nelp)++;
	return base;
}
