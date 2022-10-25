/* Buffer allocation size quantum: all buffers allocated are a
 * multiple of this size.  This MUST be a power of two. */
#define SizeQuant   4

/* Define this symbol to enable the bpoold() function which dumps the
 * buffers in a buffer pool. */
#define BufDump     1

/* Define this symbol to enable the bpoolv() function for validating
 * a buffer pool. */ 
#define BufValid    1

/* Define this symbol to enable the bufdump() function which allows
 * dumping the contents of an allocated or free buffer. */
#define DumpData    1

/* Define this symbol to enable the bstats() function which calculates
 * the total free space in the buffer pool, the largest available
 * buffer, and the total space currently allocated. */
#define BufStats    1

/* Wipe free buffers to a guaranteed pattern of garbage to trip up
 * miscreants who attempt to use pointers into released buffers. */
#define FreeWipe    1

/* Use a best fit algorithm when searching for space for an
 * allocation request.  This uses memory more efficiently, but
 * allocation will be much slower. */
#define BestFit     1

/* Define this symbol to enable the bectl() function for automatic
 * pool space control. */
#define BECtl	    1

#include <stdio.h>

#ifdef lint
#define NDEBUG			      /* Exits in asserts confuse lint */
/* LINTLIBRARY */                     /* Don't complain about def, no ref */
extern char *sprintf();               /* Sun includes don't define sprintf */
#endif

#include <assert.h>
#include <string.h>
#include <stdlib.h>

/* BufDump implies DumpData */
#ifdef BufDump
#ifndef DumpData
#define DumpData    1
#endif
#endif

#ifdef DumpData
#include <ctype.h>
#endif

/*  Declare the interface, including the requested buffer size type,
    long.  */
#include <ddk/bytepool.h>

/* Type for size arguments to memxxx()
 * functions such as memcmp(). */
#define MemSize     int 	     

/* Queue links */
struct qlinks {
    struct bfhead *flink;	      /* Forward link */
    struct bfhead *blink;	      /* Backward link */
};

/* Header in allocated and free buffers */
struct bhead {
    long prevfree;		      /* Relative link back to previous
					 free buffer in memory or 0 if
					 previous buffer is allocated.	*/
    long bsize;		      /* Buffer size: positive if free,
					 negative if allocated. */
};
#define BH(p)	((struct bhead *) (p))

/*  Header in directly allocated buffers (by acqfcn) */
struct bdhead {
    long tsize;		      /* Total size, including overhead */
    struct bhead bh;		      /* Common header */
};
#define BDH(p)	((struct bdhead *) (p))

/* Header in free buffers */
struct bfhead {
    struct bhead bh;		      /* Common allocated/free header */
    struct qlinks ql;		      /* Links on free list */
};
#define BFH(p)	((struct bfhead *) (p))

/*  Minimum allocation quantum: */
#define QLSize	(sizeof(struct qlinks))
#define SizeQ	((SizeQuant > QLSize) ? SizeQuant : QLSize)
#define V   (void)		      /* To denote unwanted returned values */

/* End sentinel: value placed in bsize field of dummy block delimiting
   end of pool block.  The most negative number which will  fit  in  a
   long, defined in a way that the compiler will accept. */
#define ESent	((long) (-(((1L << (sizeof(long) * 8 - 2)) - 1) * 2) - 2))

/* bytepool
 * This is the structure containing all neccessary state-variables
 * to keep track of memory pools. */
typedef struct bytepool {
	struct bfhead freelist;
#ifdef BufStats
	long totalloc;	      /* Total space currently allocated */
	long numget, numrel;   /* Number of bget() and brel() calls */
#ifdef BECtl
	long numpblk;	      /* Number of pool blocks */
	long numpget, numprel; /* Number of block gets and rels */
	long numdget, numdrel; /* Number of direct gets and rels */
#endif /* BECtl */
#endif /* BufStats */

#ifdef BECtl
	/* Automatic expansion block management functions */
	int (*compfcn) (long sizereq, int sequence);
	void *(*acqfcn) (long size);
	void (*relfcn) (void *buf);
	long exp_incr;	      /* Expansion block size */
	long pool_len;	      /* 0: no bpool calls have been made
					 -1: not all pool blocks are
					     the same size
					 >0: (common) block size for all
					     bpool calls made so far
				      */
#endif
} bytepool_t;

/* bget
 * Used for buffer allocation with the given pool. Returns NULL if there
 * is no more room available.  */
void*
bget(
	_In_ bytepool_t *pool, 
	_In_ long requested_size)
{
	// Variables
    long size = requested_size;
    struct bfhead *b;
#ifdef BestFit
    struct bfhead *best;
#endif
    void *buf;
#ifdef BECtl
    int compactseq = 0;
#endif

	assert(pool != NULL);
    assert(size > 0);

    if (size < SizeQ) { 	      /* Need at least room for the */
	size = SizeQ;		      /*    queue links.  */
    }
#ifdef SizeQuant
#if SizeQuant > 1
    size = (size + (SizeQuant - 1)) & (~(SizeQuant - 1));
#endif
#endif

    size += sizeof(struct bhead);     /* Add overhead in allocated buffer
					 to size required. */

#ifdef BECtl
    /* If a compact function was provided in the call to bectl(), wrap
       a loop around the allocation process  to  allow	compaction  to
       intervene in case we don't find a suitable buffer in the chain. */
    while (1) {
#endif
	b = pool->freelist.ql.flink;
#ifdef BestFit
	best = &pool->freelist;
#endif

	/* Scan the free list searching for the first buffer big enough
	   to hold the requested size buffer. */
#ifdef BestFit
	while (b != &pool->freelist) {
	    if (b->bh.bsize >= size) {
		if ((best == &pool->freelist) || (b->bh.bsize < best->bh.bsize)) {
		    best = b;
		}
	    }
	    b = b->ql.flink;		  /* Link to next buffer */
	}
	b = best;
#endif /* BestFit */

	while (b != &pool->freelist) {
	    if ((long) b->bh.bsize >= size) {

		/* Buffer  is big enough to satisfy  the request.  Allocate it
		   to the caller.  We must decide whether the buffer is  large
		   enough  to  split  into  the part given to the caller and a
		   free buffer that remains on the free list, or  whether  the
		   entire  buffer  should  be  removed	from the free list and
		   given to the caller in its entirety.   We  only  split  the
		   buffer if enough room remains for a header plus the minimum
		   quantum of allocation. */
		if ((b->bh.bsize - size) > (SizeQ + (sizeof(struct bhead)))) {
		    struct bhead *ba, *bn;

		    ba = BH(((char *) b) + (b->bh.bsize - size));
		    bn = BH(((char *) ba) + size);
		    assert(bn->prevfree == b->bh.bsize);
		    /* Subtract size from length of free block. */
		    b->bh.bsize -= size;
		    /* Link allocated buffer to the previous free buffer. */
		    ba->prevfree = b->bh.bsize;
		    /* Plug negative size into user buffer. */
		    ba->bsize = -(long) size;
		    /* Mark buffer after this one not preceded by free block. */
		    bn->prevfree = 0;

#ifdef BufStats
		    pool->totalloc += size;
		    pool->numget++;		  /* Increment number of bget() calls */
#endif
		    buf = (void *) ((((char *) ba) + sizeof(struct bhead)));
		    return buf;
		} else {
		    struct bhead *ba;

		    ba = BH(((char *) b) + b->bh.bsize);
		    assert(ba->prevfree == b->bh.bsize);

			/* The buffer isn't big enough to split.  Give  the  whole
		       shebang to the caller and remove it from the free list. */
		    assert(b->ql.blink->ql.flink == b);
		    assert(b->ql.flink->ql.blink == b);
		    b->ql.blink->ql.flink = b->ql.flink;
		    b->ql.flink->ql.blink = b->ql.blink;

#ifdef BufStats
		    pool->totalloc += b->bh.bsize;
		    pool->numget++;		  /* Increment number of bget() calls */
#endif
		    /* Negate size to mark buffer allocated. */
		    b->bh.bsize = -(b->bh.bsize);

		    /* Zero the back pointer in the next buffer in memory
		       to indicate that this buffer is allocated. */
		    ba->prevfree = 0;

		    /* Give user buffer starting at queue links. */
		    buf =  (void *) &(b->ql);
		    return buf;
		}
	    }
	    b = b->ql.flink;		  /* Link to next buffer */
	}
#ifdef BECtl

	/* We failed to find a buffer.  If there's a compact  function
	   defined,  notify  it  of the size requested.  If it returns
	   TRUE, try the allocation again. */
	if ((pool->compfcn == NULL) || (!(*pool->compfcn)(size, ++compactseq))) {
	    break;
	}
    }

    /* No buffer available with requested size free. */
    /* Don't give up yet -- look in the reserve supply. */
    if (pool->acqfcn != NULL) {
	if (size > pool->exp_incr - sizeof(struct bhead)) {

	    /* Request	is  too  large	to  fit in a single expansion
	       block.  Try to satisy it by a direct buffer acquisition. */
	    struct bdhead *bdh;

	    size += sizeof(struct bdhead) - sizeof(struct bhead);
	    if ((bdh = BDH((*pool->acqfcn)((long) size))) != NULL) {

		/*  Mark the buffer special by setting the size field
		    of its header to zero.  */
		bdh->bh.bsize = 0;
		bdh->bh.prevfree = 0;
		bdh->tsize = size;
#ifdef BufStats
		pool->totalloc += size;
		pool->numget++;	      /* Increment number of bget() calls */
		pool->numdget++;	      /* Direct bget() call count */
#endif
		buf =  (void *) (bdh + 1);
		return buf;
	    }

	} else {

	    /*	Try to obtain a new expansion block */
	    void *newpool;

	    if ((newpool = (*pool->acqfcn)((long) pool->exp_incr)) != NULL) {
		bpool(newpool, pool->exp_incr, &pool);
                buf =  bget(pool, requested_size);  /* This can't, I say, can't
						 get into a loop. */
		return buf;
	    }
	}
    }

    /*	Still no buffer available */
#endif /* BECtl */
    return NULL;
}

/* bgetz
 * Allocate a buffer and clear its contents to zero.  We clear
 * the  entire  contents  of  the buffer to zero, not just the
 * region requested by the caller. */
void*
bgetz(
	_In_ bytepool_t *pool,
	_In_ long size)
{
    char *buf = (char *) bget(pool, size);

    if (buf != NULL) {
	struct bhead *b;
	long rsize;

	b = BH(buf - sizeof(struct bhead));
	rsize = -(b->bsize);
	if (rsize == 0) {
	    struct bdhead *bd;

	    bd = BDH(buf - sizeof(struct bdhead));
	    rsize = bd->tsize - sizeof(struct bdhead);
	} else {
	    rsize -= sizeof(struct bhead);
	}
	assert(rsize >= size);
	V memset(buf, 0, (MemSize) rsize);
    }
    return ((void *) buf);
}

/* bgetr
 * Reallocate a buffer.  This is a minimal implementation,
 * simply in terms of brel()  and  bget().	 It  could  be
 * enhanced to allow the buffer to grow into adjacent free
 * blocks and to avoid moving data unnecessarily.  */
void*
bgetr(
	_In_ bytepool_t *pool, 
	_In_ void *buf, 
	_In_ long size)
{
	// Variables
    void *nbuf;
    long osize;		      /* Old size of buffer */
    struct bhead *b;

    if ((nbuf = bget(pool, size)) == NULL) { /* Acquire new buffer */
	return NULL;
    }
    if (buf == NULL) {
	return nbuf;
    }
    b = BH(((char *) buf) - sizeof(struct bhead));
    osize = -b->bsize;
#ifdef BECtl
    if (osize == 0) {
	/*  Buffer acquired directly through acqfcn. */
	struct bdhead *bd;

	bd = BDH(((char *) buf) - sizeof(struct bdhead));
	osize = bd->tsize - sizeof(struct bdhead);
    } else
#endif
	osize -= sizeof(struct bhead);
    assert(osize > 0);
    V memcpy((char *) nbuf, (char *) buf, /* Copy the data */
	     (MemSize) ((size < osize) ? size : osize));
    brel(pool, buf);
    return nbuf;
}

/* brel
 * Release a previous allocated buffer in the given pool. */
void
brel(
	_In_ bytepool_t *pool, 
	_In_ void *buf)
{
    struct bfhead *b, *bn;

    b = BFH(((char *) buf) - sizeof(struct bhead));
#ifdef BufStats
    pool->numrel++;			      /* Increment number of brel() calls */
#endif
    assert(buf != NULL);

#ifdef BECtl
    if (b->bh.bsize == 0) {	      /* Directly-acquired buffer? */
	struct bdhead *bdh;

	bdh = BDH(((char *) buf) - sizeof(struct bdhead));
	assert(b->bh.prevfree == 0);
#ifdef BufStats
	pool->totalloc -= bdh->tsize;
	assert(pool->totalloc >= 0);
	pool->numdrel++;		      /* Number of direct releases */
#endif /* BufStats */
#ifdef FreeWipe
	V memset((char *) buf, 0x55,
		 (MemSize) (bdh->tsize - sizeof(struct bdhead)));
#endif /* FreeWipe */
	assert(pool->relfcn != NULL);
	(*pool->relfcn)((void *) bdh);      /* Release it directly. */
	return;
    }
#endif /* BECtl */

    /* Buffer size must be negative, indicating that the buffer is
       allocated. */
    if (b->bh.bsize >= 0) {
	bn = NULL;
    }
    assert(b->bh.bsize < 0);

    /*	Back pointer in next buffer must be zero, indicating the
	same thing: */
    assert(BH((char *) b - b->bh.bsize)->prevfree == 0);

#ifdef BufStats
    pool->totalloc += b->bh.bsize;
    assert(pool->totalloc >= 0);
#endif

    /* If the back link is nonzero, the previous buffer is free.  */
    if (b->bh.prevfree != 0) {

	/* The previous buffer is free.  Consolidate this buffer  with	it
	   by  adding  the  length  of	this  buffer  to the previous free
	   buffer.  Note that we subtract the size  in	the  buffer  being
           released,  since  it's  negative to indicate that the buffer is
	   allocated. */
	register long size = b->bh.bsize;

        /* Make the previous buffer the one we're working on. */
	assert(BH((char *) b - b->bh.prevfree)->bsize == b->bh.prevfree);
	b = BFH(((char *) b) - b->bh.prevfree);
	b->bh.bsize -= size;
    } else {

	/* The previous buffer isn't allocated.  Insert this buffer
	   on the free list as an isolated free block. */
	assert(pool->freelist.ql.blink->ql.flink == &pool->freelist);
	assert(pool->freelist.ql.flink->ql.blink == &pool->freelist);
	b->ql.flink = &pool->freelist;
	b->ql.blink = pool->freelist.ql.blink;
	pool->freelist.ql.blink = b;
	b->ql.blink->ql.flink = b;
	b->bh.bsize = -b->bh.bsize;
    }

    /* Now we look at the next buffer in memory, located by advancing from
       the  start  of  this  buffer  by its size, to see if that buffer is
       free.  If it is, we combine  this  buffer  with	the  next  one	in
       memory, dechaining the second buffer from the free list. */
    bn =  BFH(((char *) b) + b->bh.bsize);
    if (bn->bh.bsize > 0) {

	/* The buffer is free.	Remove it from the free list and add
	   its size to that of our buffer. */
	assert(BH((char *) bn + bn->bh.bsize)->prevfree == bn->bh.bsize);
	assert(bn->ql.blink->ql.flink == bn);
	assert(bn->ql.flink->ql.blink == bn);
	bn->ql.blink->ql.flink = bn->ql.flink;
	bn->ql.flink->ql.blink = bn->ql.blink;
	b->bh.bsize += bn->bh.bsize;

	/* Finally,  advance  to   the	buffer	that   follows	the  newly
	   consolidated free block.  We must set its  backpointer  to  the
	   head  of  the  consolidated free block.  We know the next block
	   must be an allocated block because the process of recombination
	   guarantees  that  two  free	blocks will never be contiguous in
	   memory.  */
	bn = BFH(((char *) b) + b->bh.bsize);
    }
#ifdef FreeWipe
    V memset(((char *) b) + sizeof(struct bfhead), 0x55,
	    (MemSize) (b->bh.bsize - sizeof(struct bfhead)));
#endif
    assert(bn->bh.bsize < 0);

    /* The next buffer is allocated.  Set the backpointer in it  to  point
       to this buffer; the previous free buffer in memory. */
    bn->bh.prevfree = b->bh.bsize;

#ifdef BECtl

    /*	If  a  block-release function is defined, and this free buffer
	constitutes the entire block, release it.  Note that  pool_len
	is  defined  in  such a way that the test will fail unless all
	pool blocks are the same size.	*/
    if (pool->relfcn != NULL &&
	((long) b->bh.bsize) == (pool->pool_len - sizeof(struct bhead))) {

	assert(b->bh.prevfree == 0);
	assert(BH((char *) b + b->bh.bsize)->bsize == ESent);
	assert(BH((char *) b + b->bh.bsize)->prevfree == b->bh.bsize);
	/*  Unlink the buffer from the free list  */
	b->ql.blink->ql.flink = b->ql.flink;
	b->ql.flink->ql.blink = b->ql.blink;

	(*pool->relfcn)(b);
#ifdef BufStats
	pool->numprel++;		      /* Nr of expansion block releases */
	pool->numpblk--;		      /* Total number of blocks */
	assert(pool->numpblk == pool->numpget - pool->numprel);
#endif /* BufStats */
    }
#endif /* BECtl */
}

#ifdef BECtl

/* bectl
 * Establish automatic pool expansion control  */
void
bectl(
	_In_ bytepool_t *pool,
	_In_ int (*compact) (long sizereq, int sequence),
	_In_ void *(*acquire) (long size),
	_In_ void (*release) (void *buf),
	_In_ long pool_incr)
{
    pool->compfcn = compact;
    pool->acqfcn = acquire;
    pool->relfcn = release;
    pool->exp_incr = pool_incr;
}
#endif


/* bpool
 * Add a region of memory to the buffer pool. If the pointer to the bytepool is passed as
 * null, it is treated as a new memory region and thus initialized. Otherwise memory is
 * added to the existing pool. */
oserr_t
bpool(
	_In_ void *buf, 
	_In_ long len,
	_Out_ bytepool_t **out)
{
	// Variables
    struct bfhead *b = BFH(buf);
    struct bhead *bn;

	// Determine if we should create a new pool
	if (*out == NULL) {
		*out = (bytepool_t*)malloc(sizeof(bytepool_t));
		memset(*out, 0, sizeof(bytepool_t));
		(*out)->freelist.ql.flink = &(*out)->freelist;
		(*out)->freelist.ql.blink = &(*out)->freelist;
	}

#ifdef SizeQuant
    len &= ~(SizeQuant - 1);
#endif
#ifdef BECtl
    if ((*out)->pool_len == 0) {
	(*out)->pool_len = len;
    } else if (len != (*out)->pool_len) {
	(*out)->pool_len = -1;
    }
#ifdef BufStats
    (*out)->numpget++;			      /* Number of block acquisitions */
    (*out)->numpblk++;			      /* Number of blocks total */
    assert((*out)->numpblk == (*out)->numpget - (*out)->numprel);
#endif /* BufStats */
#endif /* BECtl */

    /* Since the block is initially occupied by a single free  buffer,
       it  had	better	not  be  (much) larger than the largest buffer
       whose size we can store in bhead.bsize. */
    assert(len - sizeof(struct bhead) <= -((long) ESent + 1));

    /* Clear  the  backpointer at  the start of the block to indicate that
       there  is  no  free  block  prior  to  this   one.    That   blocks
       recombination when the first block in memory is released. */
    b->bh.prevfree = 0;

    /* Chain the new block to the free list. */
    assert((*out)->freelist.ql.blink->ql.flink == &(*out)->freelist);
    assert((*out)->freelist.ql.flink->ql.blink == &(*out)->freelist);
    b->ql.flink = &(*out)->freelist;
    b->ql.blink = (*out)->freelist.ql.blink;
    (*out)->freelist.ql.blink = b;
    b->ql.blink->ql.flink = b;

    /* Create a dummy allocated buffer at the end of the pool.	This dummy
       buffer is seen when a buffer at the end of the pool is released and
       blocks  recombination  of  the last buffer with the dummy buffer at
       the end.  The length in the dummy buffer  is  set  to  the  largest
       negative  number  to  denote  the  end  of  the pool for diagnostic
       routines (this specific value is  not  counted  on  by  the  actual
       allocation and release functions). */
    len -= sizeof(struct bhead);
    b->bh.bsize = (long) len;
#ifdef FreeWipe
    V memset(((char *) b) + sizeof(struct bfhead), 0x55,
	     (MemSize) (len - sizeof(struct bfhead)));
#endif
    bn = BH(((char *) b) + len);
    bn->prevfree = (long) len;
    /* Definition of ESent assumes two's complement! */
    assert((~0) == -1);
    bn->bsize = ESent;
	return OS_EOK;
}

#ifdef BufStats

/* bstats
 * Return buffer allocation free space statistics.  */
void
bstats(
	_In_ bytepool_t *pool,
	_Out_ long *curalloc, 
	_Out_ long *totfree, 
	_Out_ long *maxfree, 
	_Out_ long *nget, 
	_Out_ long *nrel)
{
	// Variables
    struct bfhead *b = pool->freelist.ql.flink;

    *nget = pool->numget;
    *nrel = pool->numrel;
    *curalloc = pool->totalloc;
    *totfree = 0;
    *maxfree = -1;
    while (b != &pool->freelist) {
	assert(b->bh.bsize > 0);
	*totfree += b->bh.bsize;
	if (b->bh.bsize > *maxfree) {
	    *maxfree = b->bh.bsize;
	}
	b = b->ql.flink;	      /* Link to next buffer */
    }
}

#ifdef BECtl

/* bstatse
 * Return extended statistics */
void
bstatse(
	_In_ bytepool_t *pool,
	_Out_ long *pool_incr, 
	_Out_ long *npool, 
	_Out_ long *npget, 
	_Out_ long *nprel, 
	_Out_ long *ndget, 
	_Out_ long *ndrel)
{
    *pool_incr = (pool->pool_len < 0) ? -pool->exp_incr : pool->exp_incr;
    *npool = pool->numpblk;
    *npget = pool->numpget;
    *nprel = pool->numprel;
    *ndget = pool->numdget;
    *ndrel = pool->numdrel;
}
#endif /* BECtl */
#endif /* BufStats */

#ifdef DumpData

/* bufdump
 * Dump the data in a buffer.  This is called with the  user
 * data pointer, and backs up to the buffer header.  It will
 * dump either a free block or an allocated one. */
void
bufdump(
	_In_ bytepool_t *pool,
	_In_ void *buf)
{
	// Variables
    struct bfhead *b;
    unsigned char *bdump;
    long bdlen;

    b = BFH(((char *) buf) - sizeof(struct bhead));
    assert(b->bh.bsize != 0);
    if (b->bh.bsize < 0) {
	bdump = (unsigned char *) buf;
	bdlen = (-b->bh.bsize) - sizeof(struct bhead);
    } else {
	bdump = (unsigned char *) (((char *) b) + sizeof(struct bfhead));
	bdlen = b->bh.bsize - sizeof(struct bfhead);
    }

    while (bdlen > 0) {
	int i, dupes = 0;
	long l = bdlen;
	char bhex[50], bascii[20];

	if (l > 16) {
	    l = 16;
	}

	for (i = 0; i < l; i++) {
            V sprintf(bhex + i * 3, "%02X ", bdump[i]);
            bascii[i] = isprint(bdump[i]) ? bdump[i] : ' ';
	}
	bascii[i] = 0;
        V printf("%-48s   %s\n", bhex, bascii);
	bdump += l;
	bdlen -= l;
	while ((bdlen > 16) && (memcmp((char *) (bdump - 16),
				       (char *) bdump, 16) == 0)) {
	    dupes++;
	    bdump += 16;
	    bdlen -= 16;
	}
	if (dupes > 1) {
	    V printf(
                "     (%d lines [%d bytes] identical to above line skipped)\n",
		dupes, dupes * 16);
	} else if (dupes == 1) {
	    bdump -= 16;
	    bdlen += 16;
	}
    }
}
#endif

#ifdef BufDump

/* bpoold
 * Dump a buffer pool. The buffer headers are always listed.
 * If DUMPALLOC is nonzero, the contents of allocated buffers
 * are dumped. If DUMPFREE is nonzero, free blocks are
 * dumped as well. If FreeWipe  checking is  enabled, free
 * blocks which have been clobbered will always be dumped. */
void
bpoold(
	_In_ bytepool_t *pool,
	_In_ void *buf, 
	_In_ int dumpalloc, 
	_In_ int dumpfree)
{
    struct bfhead *b = BFH(buf);

    while (b->bh.bsize != ESent) {
	long bs = b->bh.bsize;

	if (bs < 0) {
	    bs = -bs;
            V printf("Allocated buffer: size %6ld bytes.\n", (long) bs);
	    if (dumpalloc) {
		bufdump(pool, (void *) (((char *) b) + sizeof(struct bhead)));
	    }
	} else {
            char *lerr = "";

	    assert(bs > 0);
	    if ((b->ql.blink->ql.flink != b) ||
		(b->ql.flink->ql.blink != b)) {
                lerr = "  (Bad free list links)";
	    }
            V printf("Free block:       size %6ld bytes.%s\n",
		(long) bs, lerr);
#ifdef FreeWipe
	    lerr = ((char *) b) + sizeof(struct bfhead);
	    if ((bs > sizeof(struct bfhead)) && ((*lerr != 0x55) ||
		(memcmp(lerr, lerr + 1,
		  (MemSize) (bs - (sizeof(struct bfhead) + 1))) != 0))) {
		V printf(
                    "(Contents of above free block have been overstored.)\n");
		bufdump(pool, (void *) (((char *) b) + sizeof(struct bhead)));
	    } else
#endif
	    if (dumpfree) {
		bufdump(pool, (void *) (((char *) b) + sizeof(struct bhead)));
	    }
	}
	b = BFH(((char *) b) + bs);
    }
}
#endif /* BufDump */

#ifdef BufValid

/* bpoolv
 * Validate a buffer pool. If NDEBUG isn't defined,
 * any error generates an assertion failure. */
oserr_t
bpoolv(
	_In_ bytepool_t *pool,
	_In_ void *buf)
{
    struct bfhead *b = BFH(buf);

    while (b->bh.bsize != ESent) {
	long bs = b->bh.bsize;

	if (bs < 0) {
	    bs = -bs;
	} else {
            char *lerr = "";

	    assert(bs > 0);
	    if (bs <= 0) {
		return OS_EUNKNOWN;
	    }
	    if ((b->ql.blink->ql.flink != b) ||
		(b->ql.flink->ql.blink != b)) {
                V printf("Free block: size %6ld bytes.  (Bad free list links)\n",
		     (long) bs);
		assert(0);
		return OS_EUNKNOWN;
	    }
#ifdef FreeWipe
	    lerr = ((char *) b) + sizeof(struct bfhead);
	    if ((bs > sizeof(struct bfhead)) && ((*lerr != 0x55) ||
		(memcmp(lerr, lerr + 1,
		  (MemSize) (bs - (sizeof(struct bfhead) + 1))) != 0))) {
		V printf(
                    "(Contents of above free block have been overstored.)\n");
		bufdump(pool, (void *) (((char *) b) + sizeof(struct bhead)));
		assert(0);
		return OS_EUNKNOWN;
	    }
#endif
	}
	b = BFH(((char *) b) + bs);
    }
    return OS_EOK;
}
#endif /* BufValid */
