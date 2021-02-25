#ifndef __INTERNAL_SIGNAL_H
#define __INTERNAL_SIGNAL_H

#ifndef __SIGTYPE_DEFINED__
#define __SIGTYPE_DEFINED__
typedef	void (*__sa_handler_t)(int);
typedef void (*__sa_process_t)(int, void*, void*);
#endif

#define SIGNAL_SEPERATE_STACK 0x00000001U
#define SIGNAL_HARDWARE_TRAP  0x00000002U
#define SIGNAL_MASKED         0x00000004U

typedef struct _sig_element {
	int 		   signal;
	const char*	   name;
	__sa_handler_t handler;
} sig_element;

#endif