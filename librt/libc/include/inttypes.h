/*
 * Copyright (c) 2004, 2005 by
 * Ralf Corsepius, Ulm/Germany. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software
 * is freely granted, provided that this notice is preserved.
 */

/**
 *  @file  inttypes.h
 */

#ifndef _INTTYPES_H
#define _INTTYPES_H

// list of types that should be exposed through <inttypes.h>
#define __need_imaxdiv

#include <crtdefs.h>
#include <_types.h> // <stdint.h> automatically included by __need_imaxdiv

#if !defined(__cplusplus) || defined(__STDC_FORMAT_MACROS)

/* 7.8.1 Macros for format specifiers
 */
#define PRId8 "d"
#define PRId16 "d"
#define PRId32 "d"
#define PRId64 "lld"

#define PRIdLEAST8 "d"
#define PRIdLEAST16 "d"
#define PRIdLEAST32 "d"
#define PRIdLEAST64 "lld"

#define PRIdFAST8 "d"
#define PRIdFAST16 "d"
#define PRIdFAST32 "d"
#define PRIdFAST64 "lld"

#define PRIdMAX "lld"

#define PRIi8 "i"
#define PRIi16 "i"
#define PRIi32 "i"
#define PRIi64 "lli"
#define PRIll "lli"

#define PRIiLEAST8 "i"
#define PRIiLEAST16 "i"
#define PRIiLEAST32 "i"
#define PRIiLEAST64 "lli"

#define PRIiFAST8 "i"
#define PRIiFAST16 "i"
#define PRIiFAST32 "i"
#define PRIiFAST64 "lli"

#define PRIiMAX "lli"

#define PRIo8 "o"
#define PRIo16 "o"
#define PRIo32 "o"
#define PRIo64 "llo"

#define PRIoLEAST8 "o"
#define PRIoLEAST16 "o"
#define PRIoLEAST32 "o"
#define PRIoLEAST64 "llo"

#define PRIoFAST8 "o"
#define PRIoFAST16 "o"
#define PRIoFAST32 "o"
#define PRIoFAST64 "llo"

#define PRIoMAX "llo"

/* fprintf macros for unsigned types */
#define PRIu8 "u"
#define PRIu16 "u"
#define PRIu32 "u"
#define PRIu64 "llu"


#define PRIuLEAST8 "u"
#define PRIuLEAST16 "u"
#define PRIuLEAST32 "u"
#define PRIuLEAST64 "llu"

#define PRIuFAST8 "u"
#define PRIuFAST16 "u"
#define PRIuFAST32 "u"
#define PRIuFAST64 "llu"

#define PRIuMAX "llu"

#define PRIx8 "x"
#define PRIx16 "x"
#define PRIx32 "x"
#define PRIx64 "llx"

#define PRIxLEAST8 "x"
#define PRIxLEAST16 "x"
#define PRIxLEAST32 "x"
#define PRIxLEAST64 "llx"

#define PRIxFAST8 "x"
#define PRIxFAST16 "x"
#define PRIxFAST32 "x"
#define PRIxFAST64 "llx"

#define PRIxMAX "llx"

#define PRIX8 "X"
#define PRIX16 "X"
#define PRIX32 "X"
#define PRIX64 "llX"

#define PRIXLEAST8 "X"
#define PRIXLEAST16 "X"
#define PRIXLEAST32 "X"
#define PRIXLEAST64 "llX"

#define PRIXFAST8 "X"
#define PRIXFAST16 "X"
#define PRIXFAST32 "X"
#define PRIXFAST64 "llX"

#define PRIXMAX "llX"

/* Platform size independant formatters 
 * - signed
 * - unsigned */
#if defined(__STDC_FORMAT_MACROS_64)
#define PRIiIN "lli"
#define PRIuIN "llu"
#define PRIxIN "llx"
#else
#define PRIiIN "i"
#define PRIuIN "u"
#define PRIxIN "x"
#endif

/*
 *   fscanf macros for signed int types
 *   NOTE: if 32-bit int is used for int_fast8_t and int_fast16_t
 *   (see stdint.h, 7.18.1.3), FAST8 and FAST16 should have
 *   no length identifiers
 */

#define SCNd16 "hd"
#define SCNd32 "d"
#define SCNd64 "lld"

#define SCNdLEAST16 "hd"
#define SCNdLEAST32 "d"
#define SCNdLEAST64 "lld"

#define SCNdFAST16 "hd"
#define SCNdFAST32 "d"
#define SCNdFAST64 "lld"

#define SCNdMAX "lld"

#define SCNi16 "hi"
#define SCNi32 "i"
#define SCNll "lli"

#define SCNiLEAST16 "hi"
#define SCNiLEAST32 "i"
#define SCNiLEAST64 "lli"

#define SCNiFAST16 "hi"
#define SCNiFAST32 "i"
#define SCNiFAST64 "lli"

#define SCNiMAX "lli"

#define SCNo16 "ho"
#define SCNo32 "o"
#define SCNo64 "llo"

#define SCNoLEAST16 "ho"
#define SCNoLEAST32 "o"
#define SCNoLEAST64 "llo"

#define SCNoFAST16 "ho"
#define SCNoFAST32 "o"
#define SCNoFAST64 "llo"

#define SCNoMAX "llo"

#define SCNx16 "hx"
#define SCNx32 "x"
#define SCNx64 "llx"

#define SCNxLEAST16 "hx"
#define SCNxLEAST32 "x"
#define SCNxLEAST64 "llx"

#define SCNxFAST16 "hx"
#define SCNxFAST32 "x"
#define SCNxFAST64 "llx"

#define SCNxMAX "llx"

/* fscanf macros for unsigned int types */

#define SCNu16 "hu"
#define SCNu32 "u"
#define SCNu64 "llu"

#define SCNuLEAST16 "hu"
#define SCNuLEAST32 "u"
#define SCNuLEAST64 "llu"

#define SCNuFAST16 "hu"
#define SCNuFAST32 "u"
#define SCNuFAST64 "llu"

#define SCNuMAX "llu"

#ifdef _WIN64
#define PRIdPTR "lld"
#define PRIiPTR "lli"
#define PRIoPTR "llo"
#define PRIuPTR "llu"
#define PRIxPTR "llx"
#define PRIXPTR "llX"
#define SCNdPTR "lld"
#define SCNiPTR "lli"
#define SCNoPTR "llo"
#define SCNxPTR "llx"
#define SCNuPTR "llu"
#else
#define PRIdPTR "d"
#define PRIiPTR "i"
#define PRIoPTR "o"
#define PRIuPTR "u"
#define PRIxPTR "x"
#define PRIXPTR "X"
#define SCNdPTR "d"
#define SCNiPTR "i"
#define SCNoPTR "o"
#define SCNxPTR "x"
 #define SCNuPTR "u"
#endif

#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/*
 * no length modifier for char types prior to C9x
 * MS runtime  scanf appears to treat "hh" as "h" 
 */

/* signed char */
#define SCNd8 "hhd"
#define SCNdLEAST8 "hhd"
#define SCNdFAST8 "hhd"

#define SCNi8 "hhi"
#define SCNiLEAST8 "hhi"
#define SCNiFAST8 "hhi"

#define SCNo8 "hho"
#define SCNoLEAST8 "hho"
#define SCNoFAST8 "hho"

#define SCNx8 "hhx"
#define SCNxLEAST8 "hhx"
#define SCNxFAST8 "hhx"

/* unsigned char */
#define SCNu8 "hhu"
#define SCNuLEAST8 "hhu"
#define SCNuFAST8 "hhu"
#endif /* __STDC_VERSION__ >= 199901 */

#endif	/* !defined(__cplusplus) || defined(__STDC_FORMAT_MACROS) */

_CODE_BEGIN
/* C++11 Added functions, to support 128 bit integers */
#ifndef _CRT_IMAX_DEFINED
#define _CRT_IMAX_DEFINED
CRTDECL(imaxdiv_t, imaxdiv(intmax_t numer, intmax_t denomer));
CRTDECL(intmax_t,  imaxabs(intmax_t j));
#endif
_CODE_END

#endif
