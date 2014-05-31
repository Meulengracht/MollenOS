/************************************************************************/
/* MEMORY MANAGEMENT                                                    */
/************************************************************************/
#include <MollenOS.h>
#include <stdlib.h>

void *VirtualAlloc(size_t Size)
{
	uint32_t Result = 0;

	//Calc pages
	uint32_t NumPages = Size / MOS_PAGE_SIZE;
	if(Size % MOS_PAGE_SIZE)
		NumPages++;

	//Syscall
	_syscall1(SYSCALL_ALLOCPAGE, NumPages, &Result);

	//Return
	return (void*)Result;
}

int VirtualFree(void *ptr, size_t Size)
{
	uint32_t Result = 0;

	//Calc pages
	uint32_t NumPages = Size / MOS_PAGE_SIZE;
	if(Size % MOS_PAGE_SIZE)
		NumPages++;

	//Syscall it
	_syscall2(SYSCALL_FREEPAGE, (uint32_t)ptr, NumPages, &Result);

	//Return
	return 1;
}

void *malloc(size_t Size)
{
	return HeapAlloc(Size);
}

void *realloc(void *ptr, size_t Size)
{
	return HeapReAlloc(ptr, Size);
}

void *calloc(size_t nmemb, size_t membSize)
{
	return HeapCAlloc(nmemb, membSize);
}

void free(void *ptr)
{
	HeapFree(ptr);
}

void abort(void)
{
	exit(-1);
}