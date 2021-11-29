/*
 * aPLib compression library  -  the smaller the better :)
 *
 * C safe depacker, header file
 *
 * Copyright (c) 1998-2014 Joergen Ibsen
 * All Rights Reserved
 *
 * http://www.ibsensoftware.com/
 */

#ifndef DEPACKS_H_INCLUDED
#define DEPACKS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#ifndef APLIB_ERROR
# define APLIB_ERROR ((unsigned int) (-1))
#endif

// header format:
//
//  offs  size    data
// --------------------------------------
//    0   dword   tag ('AP32')
//    4   dword   header_size (24 bytes)
//    8   dword   packed_size
//   12   dword   packed_crc
//   16   dword   orig_size
//   20   dword   orig_crc

/* function prototype */
unsigned int aP_get_orig_size(const void *source);
unsigned int aP_depack_safe(const void *source,
                            unsigned int srclen,
                            void *destination,
                            unsigned int dstlen);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* DEPACKS_H_INCLUDED */
