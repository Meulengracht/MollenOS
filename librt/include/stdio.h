/* MollenOS
 *
 * Copyright 2011 - 2017, Philip Meulengracht
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
 * MollenOS - C Standard Library
 */

#ifndef __STDIO_INC__
#define __STDIO_INC__

/* Includes */
#include <os/osdefs.h>
#include <stddef.h>
#include <stdarg.h>

/* C Guard */
_CODE_BEGIN

/*******************************
 *        Definitions          *
 *******************************/
#define EOF				(-1)

/* Seek from beginning of file.  */
#define SEEK_SET        0       
/* Seek from current position.  */
#define SEEK_CUR        1
/* Set file pointer to EOF plus "offset" */
#define SEEK_END        2

/* Standard I/O */
#define stdout						(FILE*)1 //1
#define stdin						(FILE*)2 //2
#define	stderr						(FILE*)3 //3
#define BUFSIZ						(int)2048

/* Set fpos_t to the arch-specific width */
#ifndef FPOS_T_DEFINED
#if _FILE_OFFSET_BITS==64
	typedef uint64_t fpos_t;
#else
	typedef uint32_t fpos_t;
#endif
#define FPOS_T_DEFINED
#endif

/* Define off_t if not already */
#ifndef OFF_T_DEFINED
#if _FILE_OFFSET_BITS==64
	typedef int64_t off_t;
	typedef int64_t off_t;
#else
	typedef long off_t;
	typedef long long off64_t;
#endif
#define OFF_T_DEFINED
#endif 

/* ErrNo Definitions 
 * Limits and extern 
 * list of strings */
#define _MAX_ERRNO	127
_CRTIMP __CONST char **_errstrings;

/* The C-Library Error Codes 
 * These closely relate the the 
 * cLibFileStream->code member */
#define _IOFBF     0x0000
#define _IOREAD	   0x0001
#define _IOWRT     0x0002
#define _IONBF     0x0004
#define _IOMYBUF   0x0008
#define _IOEOF     0x0010
#define _IOERR     0x0020
#define _IOLBF     0x0040
#define _IOSTRG    0x0040
#define _IORW      0x0080
#define _USERBUF   0x0100

/*******************************
 *       File Structures       *
 *******************************/
#ifndef _FILE_DEFINED
struct _iobuf {
	UUId_t   _fd;
    char    *_ptr;
    int      _cnt;
    char    *_base;
    int      _flag;
    int      _charbuf;
    int      _bufsiz;
    char    *_tmpfname;
	Flags_t	 _opts;
	Flags_t	 _access;
};
typedef struct _iobuf FILE;
#define _FILE_DEFINED
#endif

/*******************************
 *       File Access           *
 *******************************/
_CRTIMP int _lock_file(
	_In_ FILE * stream);
_CRTIMP int _unlock_file(
	_In_ FILE * stream);
_CRTIMP Flags_t faccess(
	_In_ __CONST char * mode);
_CRTIMP Flags_t fopts(
	_In_ __CONST char * mode);
_CRTIMP int fclose(
	_In_ FILE * stream);
_CRTIMP FILE *fopen(
	_In_ __CONST char * filename, 
	_In_ __CONST char * mode);
_CRTIMP FILE *fdopen(
	_In_ int fd, 
	_In_ __CONST char *mode);
_CRTIMP FILE *freopen(
	_In_ __CONST char * filename, 
	_In_ __CONST char * mode, 
	_In_ FILE * stream);
_CRTIMP int remove(
	_In_ __CONST char * filename);
_CRTIMP int rename(
	_In_ __CONST char * oldname, 
	_In_ __CONST char * newname);
_CRTIMP FILE *tmpfile(void);
_CRTIMP char *tmpnam(
	_In_ char * str);
_CRTIMP int fflush(
	_In_ FILE * stream);
_CRTIMP int _fileno(
	_In_ FILE * stream);
_CRTIMP void setbuf(
    _In_ FILE* file, 
    _In_ char *buf);
_CRTIMP int setvbuf(
    _In_ FILE* file, 
    _In_ char *buf, 
    _In_ int mode, 
    _In_ size_t size);

/*******************************
 *       Formatted IO          *
 *******************************/
_CRTIMP int printf(
    _In_ __CONST char *format, 
    ...);
_CRTIMP int vprintf(
    _In_ __CONST char *format, 
    _In_ va_list argptr);
_CRTIMP int sprintf(
    _In_ char *buffer,
    _In_ __CONST char *format,
    ...);
_CRTIMP int snprintf(
	_In_ char *str, 
	_In_ size_t size, 
	_In_ __CONST char *format, 
	...);
_CRTIMP int vsprintf(
    _In_ char *buffer,
    _In_ __CONST char *format,
    _In_ va_list argptr);
_CRTIMP int vsnprintf(
	_In_ char *str, 
	_In_ size_t size, 
	_In_ __CONST char *format,
	_In_ va_list ap);
_CRTIMP int asprintf(
	_In_ char **ret, 
	_In_ __CONST char *format, 
	...);
_CRTIMP int vasprintf(
	_In_ char **ret, 
	_In_ __CONST char *format, 
	_In_ va_list ap);
_CRTIMP int sscanf(__CONST char *ibuf, __CONST char *fmt, ...);
_CRTIMP int vsscanf(__CONST char *inp, char __CONST *fmt0, va_list ap);
//_CRTIMP int scanf(__CONST char *format, ...);
//_CRTIMP int vscanf(__CONST char * format, va_list arg);

_CRTIMP int fprintf(
    _In_ FILE *file, 
    _In_ __CONST char *format,
    ...);
_CRTIMP int vfprintf(
    _In_ FILE *file, 
    _In_ __CONST char *format, 
    _In_ va_list argptr);
_CRTIMP int fscanf(FILE *stream, __CONST char *format, ...);
_CRTIMP int vfscanf(FILE * stream, __CONST char *format, va_list arg);

_CRTIMP int wprintf(
    _In_ __CONST wchar_t *format, 
    ...);
_CRTIMP int vwprintf(
    _In_ __CONST wchar_t *format, 
    _In_ va_list valist);
_CRTIMP int swprintf(
    _In_ wchar_t *buffer,
    _In_ __CONST wchar_t *format,
	...);
_CRTIMP int vswprintf(
    _In_ wchar_t *buffer,
    _In_ __CONST wchar_t *format,
	_In_ va_list argptr);

_CRTIMP int vfwprintf(
    _In_ FILE* file, 
    _In_ __CONST wchar_t *format, 
	_In_ va_list argptr);
_CRTIMP int fwprintf(
    _In_ FILE* file, 
    _In_ __CONST wchar_t *format, 
    ...);

_CRTIMP int __svfscanf(FILE *fp, char __CONST * fmt0, va_list ap);
_CRTIMP int streamout(
    _In_ FILE *stream, 
    _In_ __CONST char *format, 
    _In_ va_list argptr);
_CRTIMP int wstreamout(
    _In_ FILE *stream, 
    _In_ __CONST wchar_t *format, 
    _In_ va_list argptr);

/*******************************
 *       Character IO          *
 *******************************/
_CRTIMP int fpeekc(FILE * stream);
_CRTIMP int fungetc (int character, FILE * stream);

_CRTIMP int getchar(void);
_CRTIMP int putchar(
    _In_ int character);
_CRTIMP int getc(
    _In_ FILE* file);
_CRTIMP int putc(
    _In_ int character, 
    _In_ FILE* file);
_CRTIMP char *gets(
    _In_ char *buf);
_CRTIMP int puts(
    _In_ __CONST char *s);

_CRTIMP int fgetchar(void);
_CRTIMP int fputchar(
    _In_ int character);
_CRTIMP int fputc(
	_In_ int character,
	_In_ FILE* file);
_CRTIMP int fputs(
	_In_ __CONST char *s, 
    _In_ FILE* file);
_CRTIMP int fgetc(
	_In_ FILE *file);
_CRTIMP char *fgets(
	_In_ char *s, 
	_In_ int size, 
	_In_ FILE *file);

_CRTIMP wint_t getwchar(void);
_CRTIMP int getw(
    _In_ FILE *file);
_CRTIMP wint_t getwc(
    _In_ FILE* file);
_CRTIMP int putw(
    _In_ int val, 
    _In_ FILE *file);
_CRTIMP wint_t putwch(
    _In_ wchar_t character);
_CRTIMP int putws(
    _In_ __CONST wchar_t *s);
_CRTIMP wchar_t* getws(
    _In_ wchar_t* buf);

_CRTIMP wint_t fgetwchar(void);
_CRTIMP wint_t fputwchar(
    _In_ wint_t wc);
_CRTIMP wint_t fputwc(
    _In_ wchar_t c, 
    _In_ FILE* stream);
_CRTIMP wint_t fgetwc(
    _In_ FILE *file);
_CRTIMP wchar_t *fgetws(
    _In_ wchar_t *s,
    _In_ int size,
    _In_ FILE *file);

#define peekc(stream) fpeekc(stream)
#define ungetc(c, stream) fungetc(c, stream)

/*******************************
 *         Direct IO           *
 *******************************/
_CRTIMP size_t fread(void * vptr, size_t size, size_t count, FILE * stream);
_CRTIMP size_t fwrite(__CONST void * vptr, size_t size, size_t count, FILE * stream);

/*******************************
 *     File Positioning        *
 *******************************/
_CRTIMP int fgetpos(FILE * stream, fpos_t * pos);
_CRTIMP int fsetpos(FILE * stream, __CONST fpos_t * pos);
_CRTIMP int fseek(FILE * stream, long int offset, int origin);
_CRTIMP long int ftell(FILE * stream);
_CRTIMP void rewind(
	_In_ FILE *file);
_CRTIMP int feof(
	_In_ FILE* file);

_CRTIMP off_t ftello(FILE *stream);
_CRTIMP int fseeko(FILE *stream, off_t offset, int origin);

/*******************************
 *       Error Handling        *
 *******************************/
_CRTIMP void clearerr(
	_In_ FILE *file);
_CRTIMP int ferror(
	_In_ FILE* file);
_CRTIMP void perror(__CONST char * str);
_CRTIMP char *strerror(int errnum);

_CODE_END

#endif
