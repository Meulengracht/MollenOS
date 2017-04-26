#ifndef __INTERNAL_SIGNAL_H
#define __INTERNAL_SIGNAL_H

#ifndef _CRT_ALL_H
#error DO NOT INCLUDE THIS HEADER DIRECTLY
#endif

#ifndef __SIGTYPE_DEFINED__
#define __SIGTYPE_DEFINED__
typedef	void (*__p_sig_fn_t)(int);
#endif

void _default_handler(int signal);
typedef struct _sig_element
{
	int signal;
	char *signame;
	__p_sig_fn_t handler;
}sig_element;

#endif