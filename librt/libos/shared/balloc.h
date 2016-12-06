/* MollenOS
*
* Copyright 2011 - 2016, Philip Meulengracht
*
* This program is free software : you can redistribute it and / or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation ? , either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.If not, see <http://www.gnu.org/licenses/>.
*
*
* MollenOS C Library - Fixed Buffer Allocation
* -- http://www.fourmilab.ch/bget/
*/

#ifndef _BGET_ALLOCATOR_H_
#define _BGET_ALLOCATOR_H_

/* C-Library - Includes */
#include <crtdefs.h>

/* Definitons */
typedef long bufsize;

/* Prototypes */

/* Create a buffer pool of <len> bytes, using the storage starting at
 * <buffer>.	You  can  call	bpool()  subsequently  to   contribute
 * additional storage to the overall buffer pool. */
__CRT_EXTERN void bpool(void *buffer, bufsize len);

/* Allocate  a  buffer of <size> bytes.  The address of the buffer is
 * returned, or NULL if insufficient memory was available to allocate
 * the buffer. */
__CRT_EXTERN void *bget(bufsize size);

/* Allocate a buffer of <size> bytes and clear it to all zeroes.  The
 * address of the buffer is returned, or NULL if insufficient	memory
 * was available to allocate the buffer. */
__CRT_EXTERN void *bgetz(bufsize size);

/* Reallocate a buffer previously allocated by bget(),  changing  its
 * size  to  <newsize>  and  preserving  all  existing data.  NULL is
 * returned if insufficient memory is	available  to  reallocate  the
 * buffer, in which case the original buffer remains intact. */
__CRT_EXTERN void *bgetr(void *buffer, bufsize newsize);

/* Return  the  buffer  <buf>, previously allocated by bget(), to the
 * free space pool. */
__CRT_EXTERN void	brel(void *buf);

/* Expansion control: specify functions through which the package may
 * compact  storage  (or  take  other	appropriate  action)  when  an
 * allocation	request  fails,  and  optionally automatically acquire
 * storage for expansion blocks  when	necessary,  and  release  such
 * blocks when they become empty.  If <compact> is non-NULL, whenever
 * a buffer allocation request fails, the <compact> function will  be
 * called with arguments specifying the number of bytes (total buffer
 * size,  including  header  overhead)  required   to	 satisfy   the
 * allocation request, and a sequence number indicating the number of
 * consecutive  calls	on  <compact>  attempting  to	satisfy   this
 * allocation	request.   The sequence number is 1 for the first call
 * on <compact> for a given allocation  request,  and	increments  on
 * subsequent	calls,	permitting  the  <compact>  function  to  take
 * increasingly dire measures in an attempt to free up  storage.   If
 * the  <compact>  function  returns  a nonzero value, the allocation
 * attempt is re-tried.  If <compact> returns 0 (as  it  must	if  it
 * isn't  able  to  release  any  space  or add storage to the buffer
 * pool), the allocation request fails, which can  trigger  automatic
 * pool expansion if the <acquire> argument is non-NULL.  At the time
 * the  <compact>  function  is  called,  the	state  of  the	buffer
 * allocator  is  identical  to  that	at  the  moment the allocation
 * request was made; consequently, the <compact>  function  may  call
 * brel(), bpool(), bstats(), and/or directly manipulate  the	buffer
 * pool  in  any  manner which would be valid were the application in
 * control.  This does not, however, relieve the  <compact>  function
 * of the need to ensure that whatever actions it takes do not change
 * things   underneath  the  application  that  made  the  allocation
 * request.  For example, a <compact> function that released a buffer
 * in	the  process  of  being reallocated with bgetr() would lead to
 * disaster.  Implementing a safe and effective  <compact>  mechanism
 * requires  careful  design of an application's memory architecture,
 * and cannot generally be easily retrofitted into existing code.
 * 
 * If <acquire> is non-NULL, that function will be called whenever an
 * allocation	request  fails.  If the <acquire> function succeeds in
 * allocating the requested space and returns a pointer  to  the  new
 * area,  allocation will proceed using the expanded buffer pool.  If
 * <acquire> cannot obtain the requested space, it should return NULL
 * and   the	entire	allocation  process  will  fail.   <pool_incr>
 * specifies the normal expansion block size.	Providing an <acquire>
 * function will cause subsequent bget()  requests  for  buffers  too
 * large  to  be  managed in the linked-block scheme (in other words,
 * larger than <pool_incr> minus the buffer overhead) to be satisfied
 * directly by calls to the <acquire> function.  Automatic release of
 * empty pool blocks will occur only if all pool blocks in the system
 * are the size given by <pool_incr>. */
__CRT_EXTERN void	bectl(int(*compact)(bufsize sizereq, int sequence),
		       void *(*acquire)(bufsize size),
		       void (*release)(void *buf), bufsize pool_incr);

/* The amount	of  space  currently  allocated  is  stored  into  the
 * variable  pointed  to by <curalloc>.  The total free space (sum of
 * all free blocks in the pool) is stored into the  variable  pointed
 * to	by  <totfree>, and the size of the largest single block in the
 * free space	pool  is  stored  into	the  variable  pointed	to  by
 * <maxfree>.	 The  variables  pointed  to  by <nget> and <nrel> are
 * filled, respectively, with	the  number  of  successful  (non-NULL
 * return) bget() calls and the number of brel() calls. */
__CRT_EXTERN void	bstats(bufsize *curalloc, bufsize *totfree, bufsize *maxfree,
		       long *nget, long *nrel);

/* Extended  statistics: The expansion block size will be stored into
 * the variable pointed to by <pool_incr>, or the negative thereof if
 * automatic  expansion  block  releases are disabled.  The number of
 * currently active pool blocks will  be  stored  into  the  variable
 * pointed  to  by  <npool>.  The variables pointed to by <npget> and
 * <nprel> will be filled with, respectively, the number of expansion
 * block   acquisitions   and	releases  which  have  occurred.   The
 * variables pointed to by <ndget> and <ndrel> will  be  filled  with
 * the  number  of  bget()  and  brel()  calls, respectively, managed
 * through blocks directly allocated by the acquisition  and  release
 * functions. */
__CRT_EXTERN void	bstatse(bufsize *pool_incr, long *npool, long *npget,
		       long *nprel, long *ndget, long *ndrel);

/* The buffer pointed to by <buf> is dumped on standard output. */
__CRT_EXTERN void	bufdump(void *buf);

/* All buffers in the buffer pool <pool>, previously initialised by a
 * call on bpool(), are listed in ascending memory address order.  If
 * <dumpalloc> is nonzero, the  contents  of  allocated  buffers  are
 * dumped;  if <dumpfree> is nonzero, the contents of free blocks are
 * dumped. */
__CRT_EXTERN void	bpoold(void *pool, int dumpalloc, int dumpfree);

/* The  named	buffer	pool,  previously  initialised	by  a  call on
 * bpool(), is validated for bad pointers, overwritten data, etc.  If
 * compiled with NDEBUG not defined, any error generates an assertion
 * failure.  Otherwise 1 is returned if the pool is valid,  0	if  an
 * error is found. */
__CRT_EXTERN int	bpoolv(void *pool);

#endif //!_BGET_ALLOCATOR_H_
