#ifndef __INTERNAL_SIGNAL_H
#define __INTERNAL_SIGNAL_H

#ifndef _CRT_ALL_H
#error DO NOT INCLUDE THIS HEADER DIRECTLY
#endif

#ifndef __SIGTYPE_DEFINED__
#define __SIGTYPE_DEFINED__
typedef	void (*__sa_handler_t)(int);
typedef void (*__sa_process_t)(int, void*);
#endif

typedef struct _sig_element {
	int 		   signal;
	const char*	   name;
	__sa_handler_t handler;
} sig_element;

#endif