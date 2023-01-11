/**
 * MollenOS
 *
 * Copyright 2011, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * MollenOS C11-Support Io Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_STDIO__
#define __STDC_STDIO__

// list of types that should be exposed in stdio.h
#define __need_size_t
#define __need_wchar_t
#define __need_wint_t

#include <crtdefs.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

/*******************************
 *        Definitions          *
 *******************************/
#define EOF      (-1)
#define SEEK_SET 0 /* Seek from beginning of file.  */
#define SEEK_CUR 1 /* Seek from current position.  */
#define SEEK_END 2 /* Set file pointer to EOF plus "offset" */
#define BUFSIZ   (int)8192

/* Set fpos_t to the arch-specific width */
#ifndef FPOS_T_DEFINED
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS==64
	typedef uint64_t fpos_t;
#else
	typedef uint32_t fpos_t;
#endif
#define FPOS_T_DEFINED
#endif

/* Define off_t if not already */
#ifndef OFF_T_DEFINED
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS==64
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

/* Stdio file modes and flags
 * Definitions and bit-flags for available IO modes */
#define _IOFBF     0x0000
#define _IONBF     0x0004 // no buffering
#define _IOLBF     0x0040 // line buffering

#define _IOREAD	   0x0001 // currently reading
#define _IOWRT     0x0002 // currently writing

#define _IOMYBUF   0x0008 // buffer is allocated by stdio
#define _IOEOF     0x0010
#define _IOERR     0x0020

#define _IOSTRG    0x0080 // strange or no file descriptor
#define _IORW      0x0100 // read/write
#define _USERBUF   0x0200 // user-provided buffer
#define _FWIDE     0x0400
#define _FBYTE     0x0800
#define _IOVRT     0x1000

/*******************************
 *       File Structures       *
 *******************************/
#ifndef _FILE_DEFINED
#define _FILE_DEFINED
typedef struct _FILE FILE;
#endif

_CODE_BEGIN
CRTDECL(FILE*,         __get_std_handle(int n));
#define STDOUT_FILENO  (int)0
#define STDIN_FILENO   (int)1
#define STDERR_FILENO  (int)2
#define stdout         __get_std_handle(STDOUT_FILENO)
#define stdin          __get_std_handle(STDIN_FILENO)
#define	stderr         __get_std_handle(STDERR_FILENO)
#define L_tmpnam       32

/*******************************
 *       File Access           *
 *******************************/
CRTDECL(int,   fclose(FILE * stream));
CRTDECL(FILE*, fopen(const char * filename, const char * mode));
CRTDECL(FILE*, fdopen(int fd, const char *mode));
CRTDECL(FILE*, freopen(const char * filename, const char * mode, FILE * stream));
CRTDECL(int,   remove(const char * filename));
CRTDECL(int,   rename(const char * oldname, const char * newname));
CRTDECL(FILE*, tmpfile(void));
CRTDECL(char*, tmpnam(char* str));
CRTDECL(int,   fflush(FILE* stream));
CRTDECL(void,  setbuf(FILE* file, char *buf));
CRTDECL(int,   setvbuf(FILE* file, char *buf, int mode, size_t size));
CRTDECL(int,   fileno(FILE* stream));
CRTDECL(int,   fwide(FILE *stream, int mode));
CRTDECL(void,  flockfile(FILE* stream));
CRTDECL(int,   ftrylockfile(FILE* stream));
CRTDECL(void,  funlockfile(FILE* stream));

/*******************************
 *       Formatted IO          *
 *******************************/
CRTDECL(int, printf(const char *format, ...));
CRTDECL(int, vprintf(const char *format, va_list argptr));
CRTDECL(int, sprintf(char *buffer, const char *format, ...));
CRTDECL(int, snprintf(char *str, size_t size, const char *format, ...));
CRTDECL(int, vsprintf(char *buffer, const char *format, va_list argptr));
CRTDECL(int, vsnprintf(char *str, size_t size, const char *format, va_list ap));
CRTDECL(int, asprintf(char **ret, const char *format, ...));
CRTDECL(int, vasprintf(char **ret, const char *format, va_list ap));

CRTDECL(int, scanf(const char *format, ...));
CRTDECL(int, wscanf(const wchar_t *format, ...));
CRTDECL(int, sscanf(const char *str, const char *format, ...));
CRTDECL(int, swscanf(const wchar_t *str, const wchar_t *format, ...));
CRTDECL(int, vscanf(const char *format, va_list vlist));
CRTDECL(int, vfscanf(FILE *stream, const char *format,  va_list vlist));
CRTDECL(int, vsscanf(const char *buffer, const char *format,  va_list vlist));
CRTDECL(int, vwscanf(const wchar_t *format, va_list vlist));
CRTDECL(int, vfwscanf(FILE *stream, const wchar_t *format, va_list vlist));
CRTDECL(int, vswscanf(const wchar_t *buffer, const wchar_t *format, va_list vlist));

CRTDECL(int, fprintf(FILE *file, const char *format, ...));
CRTDECL(int, vfprintf(FILE *file, const char *format, va_list argptr));
CRTDECL(int, fscanf(FILE *file, const char *format, ...));
CRTDECL(int, fwscanf(FILE *file, const wchar_t *format, ...));
CRTDECL(int, wprintf(const wchar_t *format, ...));
CRTDECL(int, vwprintf(const wchar_t *format, va_list valist));
CRTDECL(int, swprintf(wchar_t *restrict buffer, size_t len, const wchar_t *restrict format, ...));
CRTDECL(int, swnprintf(wchar_t *buffer, size_t count, const wchar_t *format, ...));
CRTDECL(int, vswprintf(wchar_t *buffer, const wchar_t *format, va_list argptr));
CRTDECL(int, vfwprintf(FILE* file, const wchar_t *format, va_list argptr));
CRTDECL(int, fwprintf(FILE* file, const wchar_t *format, ...));

/*******************************
 *       Character IO          *
 *******************************/
CRTDECL(int,   ungetc(int character, FILE *file));
CRTDECL(int,   getchar(void));
CRTDECL(int,   putchar(int character));
CRTDECL(int,   getc(FILE* file));
CRTDECL(int,   putc(int character, FILE* file));
CRTDECL(char*, gets(char *buf));
CRTDECL(int,   puts(const char *s));

CRTDECL(int,   fgetchar(void));
CRTDECL(int,   fputchar(int character));
CRTDECL(int,   fputc(int character, FILE* file));
CRTDECL(int,   fputs(const char *s, FILE* file));
CRTDECL(int,   fgetc(FILE *file));
CRTDECL(char*, fgets(char *str, int size, FILE *file));

/*******************************
 *      Wide Character IO      *
 *******************************/
CRTDECL(wint_t, ungetwc(wint_t wc, FILE *file));
CRTDECL(wint_t, getwchar(void));
CRTDECL(int,    getw(FILE *file));
CRTDECL(wint_t, getwc(FILE* file));
CRTDECL(int,    putw(int val, FILE *file));

CRTDECL(wint_t,   putwc(wchar_t ch, FILE *stream));
CRTDECL(wint_t,   putwch(wchar_t character));
CRTDECL(int,      putws(const wchar_t *s));
CRTDECL(wchar_t*, getws(wchar_t* buf));
CRTDECL(wint_t,   fgetwchar(void));
CRTDECL(wint_t,   fputwchar(wint_t wc));
CRTDECL(wint_t,   putwchar(wchar_t ch));

CRTDECL(wint_t,   fputwc(wchar_t c, FILE* stream));
CRTDECL(wint_t,   fgetwc(FILE *file));
CRTDECL(int,      fputws(const wchar_t *str, FILE *stream));
CRTDECL(wchar_t*, fgetws(wchar_t *s, int size, FILE *file));

/*******************************
 *         Direct IO           *
 *******************************/
CRTDECL(size_t, fread(void *vptr, size_t size, size_t count, FILE *stream));
CRTDECL(size_t, fwrite(const void *vptr, size_t size, size_t count, FILE *stream));

/*******************************
 *     File Positioning        *
 *******************************/
CRTDECL(int,       fgetpos(FILE *stream, fpos_t *pos));
CRTDECL(int,       fsetpos(FILE *stream, const fpos_t *pos));
CRTDECL(int,       fseek(FILE *stream, long int offset, int origin));
CRTDECL(int,       fseeki64(FILE *file, long long offset, int whence));
CRTDECL(long,      ftell(FILE *stream));
CRTDECL(long long, ftelli64(FILE *stream));
CRTDECL(void,      rewind(FILE *file));
CRTDECL(int,       feof(FILE* file));
CRTDECL(off_t,     ftello(FILE *stream));
CRTDECL(int,       fseeko(FILE *stream, off_t offset, int origin));

/*******************************
 *       Error Handling        *
 *******************************/
CRTDECL(void, clearerr(FILE *file));
CRTDECL(int,  ferror(FILE* file));
CRTDECL(void, perror(const char * str));
CRTDECL(void, wperror(const wchar_t *str));
_CODE_END

#endif //!__STDC_STDIO__
