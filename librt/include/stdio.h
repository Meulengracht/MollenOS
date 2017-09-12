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

/* Includes 
 * - Library */
#include <os/osdefs.h>
#include <stddef.h>
#include <stdarg.h>

/* C Guard */
_CODE_BEGIN

/*******************************
 *        Definitions          *
 *******************************/
#define EOF				(-1)
#define SEEK_SET        0 /* Seek from beginning of file.  */
#define SEEK_CUR        1 /* Seek from current position.  */
#define SEEK_END        2 /* Set file pointer to EOF plus "offset" */
#define BUFSIZ          (int)2048

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

/* Stdio errno String Definitions 
 * Definitions and symbols for error strings in standard C */
#define _MAX_ERRNO	127
_CRTIMP __CONST char **_errstrings;

/* Stdio file modes and flags
 * Definitions and bit-flags for available IO modes */
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
	int      _fd;
    char    *_ptr;
    int      _cnt;
    char    *_base;
    int      _flag;
    int      _charbuf;
    int      _bufsiz;
    char    *_tmpfname;
};
typedef struct _iobuf FILE;
#define _FILE_DEFINED
#endif

/* Stdio Standard Handles
 * Contains definitions for standard input and output handles used in C */
_CRTIMP FILE *getstdfile(int n);
#define STDOUT_FD                   (int)0
#define STDIN_FD                    (int)1
#define STDERR_FD                   (int)2
#define stdout						getstdfile(STDOUT_FD)
#define stdin						getstdfile(STDIN_FD)
#define	stderr						getstdfile(STDERR_FD)

/*******************************
 *       File Access           *
 *******************************/
_CRTIMP OsStatus_t _lock_file(
	_In_ FILE * stream);
_CRTIMP OsStatus_t _unlock_file(
	_In_ FILE * stream);
_CRTIMP int _fflags(
	_In_ __CONST char *mode, 
	_In_ int *open_flags, 
	_In_ int *stream_flags);
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
_CRTIMP int scanf(
    _In_ __CONST char *format, 
    ...);
_CRTIMP int wscanf(
    _In_ __CONST wchar_t *format, 
    ...);
_CRTIMP int sscanf(
    _In_ __CONST char *str, 
    _In_ __CONST char *format, 
    ...);
_CRTIMP int swscanf(
    _In_ __CONST wchar_t *str, 
    _In_ __CONST wchar_t *format, 
    ...);

_CRTIMP int fprintf(
    _In_ FILE *file, 
    _In_ __CONST char *format,
    ...);
_CRTIMP int vfprintf(
    _In_ FILE *file, 
    _In_ __CONST char *format, 
    _In_ va_list argptr);
_CRTIMP int fscanf(
    _In_ FILE *file, 
    _In_ __CONST char *format, 
    ...);
_CRTIMP int fwscanf(
    _In_ FILE *file, 
    _In_ __CONST wchar_t *format, 
    ...);

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
_CRTIMP int swnprintf(
    _In_ wchar_t *buffer,
    _In_ size_t count,
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
_CRTIMP int ungetc(
    _In_ int character, 
    _In_ FILE *file);
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

_CRTIMP wint_t ungetwc(
    _In_ wint_t wc, 
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

/*******************************
 *         Direct IO           *
 *******************************/
_CRTIMP size_t fread(
	_In_ void *vptr, 
	_In_ size_t size, 
	_In_ size_t count, 
	_In_ FILE *stream);
_CRTIMP size_t fwrite(
	_In_ __CONST void *vptr,
	_In_ size_t size,
	_In_ size_t count,
	_In_ FILE *stream);

/*******************************
 *     File Positioning        *
 *******************************/
_CRTIMP int fgetpos(
	_In_ FILE *stream, 
	_Out_ fpos_t *pos);
_CRTIMP int fsetpos(
	_In_ FILE *stream, 
	_In_ __CONST fpos_t *pos);
_CRTIMP int fseek(
	_In_ FILE *stream, 
	_In_ long int offset, 
    _In_ int origin);
_CRTIMP int fseeki64(
	_In_ FILE *file, 
	_In_ long long offset, 
    _In_ int whence);
_CRTIMP long ftell(
    _In_ FILE *stream);
_CRTIMP long long ftelli64(
	_In_ FILE *stream);
_CRTIMP void rewind(
	_In_ FILE *file);
_CRTIMP int feof(
	_In_ FILE* file);

_CRTIMP off_t ftello(
	_In_ FILE *stream);
_CRTIMP int fseeko(
	_In_ FILE *stream, 
	_In_ off_t offset, 
	_In_ int origin);

/*******************************
 *       Error Handling        *
 *******************************/
_CRTIMP void clearerr(
	_In_ FILE *file);
_CRTIMP int ferror(
	_In_ FILE* file);
_CRTIMP void perror(
    _In_ __CONST char * str);
_CRTIMP void wperror(
	_In_ __CONST wchar_t *str);
_CRTIMP char *strerror(int errnum);

_CODE_END

#endif
