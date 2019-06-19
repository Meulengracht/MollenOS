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
 * MollenOS C11-Support Io Implementation
 * - Definitions, prototypes and information needed.
 */

#ifndef __STDC_STDIO__
#define __STDC_STDIO__

#include <os/osdefs.h>
#define __need_wint_t
#include <stddef.h>
#include <stdarg.h>

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
#define _FWIDE     0x0200
#define _FBYTE     0x0400
#define _IOVRT     0x0800

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
CRTDECL(FILE*,                      stdio_get_std(int n));
#define STDOUT_FILENO               (int)0
#define STDIN_FILENO                (int)1
#define STDERR_FILENO               (int)2
#define stdout						stdio_get_std(STDOUT_FILENO)
#define stdin						stdio_get_std(STDIN_FILENO)
#define	stderr						stdio_get_std(STDERR_FILENO)

/*******************************
 *       File Access           *
 *******************************/
CRTDECL(OsStatus_t, _lock_file(FILE * stream));
CRTDECL(OsStatus_t, _unlock_file(FILE * stream));
CRTDECL(int,        _fflags(const char *mode, int *open_flags, int *stream_flags));
CRTDECL(int,        fclose(FILE * stream));
CRTDECL(FILE*,      fopen(const char * filename, const char * mode));
CRTDECL(FILE*,      fdopen(int fd, const char *mode));
CRTDECL(FILE*,      freopen(const char * filename, const char * mode, FILE * stream));
CRTDECL(int,        remove(const char * filename));
CRTDECL(int,        rename(const char * oldname, const char * newname));
CRTDECL(FILE*,      tmpfile(void));
CRTDECL(char*,      tmpnam(char * str));
CRTDECL(int,        fflush(FILE * stream));
CRTDECL(void,       setbuf(FILE* file, char *buf));
CRTDECL(int,        setvbuf(FILE* file, char *buf, int mode, size_t size));
CRTDECL(int,        fileno(FILE * stream));

/*******************************
 *       Formatted IO          *
 *******************************/
CRTDECL(int,        printf(const char *format, ...));
CRTDECL(int,        vprintf(const char *format, va_list argptr));
CRTDECL(int,        sprintf(char *buffer, const char *format, ...));
CRTDECL(int,        snprintf(char *str, size_t size, const char *format, ...));
CRTDECL(int,        vsprintf(char *buffer, const char *format, va_list argptr));
CRTDECL(int,        vsnprintf(char *str, size_t size, const char *format, va_list ap));
CRTDECL(int,        asprintf(char **ret, const char *format, ...));
CRTDECL(int,        vasprintf(char **ret, const char *format, va_list ap));

_CRTIMP int scanf(
    _In_ const char *format, 
    ...);
_CRTIMP int wscanf(
    _In_ const wchar_t *format, 
    ...);
_CRTIMP int sscanf(
    _In_ const char *str, 
    _In_ const char *format, 
    ...);
_CRTIMP int swscanf(
    _In_ const wchar_t *str, 
    _In_ const wchar_t *format, 
    ...);

_CRTIMP int vscanf(
    _In_ const char *format,
    _In_ va_list vlist);
_CRTIMP int vfscanf(
    FILE *stream,
    const char *format, 
    va_list vlist);
_CRTIMP int vsscanf(
    const char *buffer,
    const char *format, 
    va_list vlist);
_CRTIMP int vwscanf(
    const wchar_t *format,
    va_list vlist);
_CRTIMP int vfwscanf(
    FILE *stream,
    const wchar_t *format,
    va_list vlist);
_CRTIMP int vswscanf(
    const wchar_t *buffer,
    const wchar_t *format,
    va_list vlist);

_CRTIMP int fprintf(
    _In_ FILE *file, 
    _In_ const char *format,
    ...);
_CRTIMP int vfprintf(
    _In_ FILE *file, 
    _In_ const char *format, 
    _In_ va_list argptr);
_CRTIMP int fscanf(
    _In_ FILE *file, 
    _In_ const char *format, 
    ...);
_CRTIMP int fwscanf(
    _In_ FILE *file, 
    _In_ const wchar_t *format, 
    ...);

_CRTIMP int wprintf(
    _In_ const wchar_t *format, 
    ...);
_CRTIMP int vwprintf(
    _In_ const wchar_t *format, 
    _In_ va_list valist);
_CRTIMP int swprintf(
    _In_ wchar_t *restrict buffer,
    _In_ size_t len,
    _In_ const wchar_t *restrict format,
    ...);
_CRTIMP int swnprintf(
    _In_ wchar_t *buffer,
    _In_ size_t count,
    _In_ const wchar_t *format,
	...);
_CRTIMP int vswprintf(
    _In_ wchar_t *buffer,
    _In_ const wchar_t *format,
	_In_ va_list argptr);

_CRTIMP int vfwprintf(
    _In_ FILE* file, 
    _In_ const wchar_t *format, 
	_In_ va_list argptr);
_CRTIMP int fwprintf(
    _In_ FILE* file, 
    _In_ const wchar_t *format, 
    ...);

_CRTIMP int streamout(
    _In_ FILE *stream, 
    _In_ const char *format, 
    _In_ va_list argptr);
_CRTIMP int wstreamout(
    _In_ FILE *stream, 
    _In_ const wchar_t *format, 
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
    _In_ const char *s);

_CRTIMP int fgetchar(void);
_CRTIMP int fputchar(
    _In_ int character);
_CRTIMP int fputc(
	_In_ int character,
	_In_ FILE* file);
_CRTIMP int fputs(
	_In_ const char *s, 
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

/* putwc
 * Writes a wide character ch to the given output stream stream. putwc() may be implemented as a 
 * macro and may evaluate stream more than once. */
CRTDECL(wint_t, putwc(
    _In_ wchar_t ch,
    _In_ FILE *stream));
_CRTIMP wint_t putwch(
    _In_ wchar_t character);
_CRTIMP int putws(
    _In_ const wchar_t *s);
_CRTIMP wchar_t* getws(
    _In_ wchar_t* buf);
CRTDECL(int, fwide(
    _In_ FILE *stream,
    _In_ int mode));

_CRTIMP wint_t fgetwchar(void);
_CRTIMP wint_t fputwchar(
    _In_ wint_t wc);

/* putwchar 
 * Writes a wide character ch to stdout */
CRTDECL(wint_t, putwchar(
    _In_ wchar_t ch));

/* fputwc
 * Writes a wide character ch to the given output stream stream. putwc() may be implemented as a 
 * macro and may evaluate stream more than once. */
CRTDECL(wint_t, fputwc(
    _In_ wchar_t c, 
    _In_ FILE* stream));
_CRTIMP wint_t fgetwc(
    _In_ FILE *file);
_CRTIMP int fputws(
    _In_ const wchar_t *str,
    _In_ FILE *stream);
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
	_In_ const void *vptr,
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
	_In_ const fpos_t *pos);
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
    _In_ const char * str);
_CRTIMP void wperror(
	_In_ const wchar_t *str);
_CODE_END

#endif //!__STDC_STDIO__
