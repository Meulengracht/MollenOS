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
* MollenOS C Library - Standard I/O
*/

#ifndef __STDIO_INC__
#define __STDIO_INC__

/* Includes */
#include <stdint.h>
#include <crtdefs.h>
#include <stddef.h>
#include <stdarg.h>

/* C Guard */
#ifdef __cplusplus
extern "C" {
#endif

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
#define stdout						NULL //0
#define stdin						NULL //1
#define	stderr						NULL //2
#define BUFSIZ						(int)512
#define _IOFBF						0x1
#define _IOLBF						0x2
#define _IONBF						0x4

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
#else
	typedef long int off_t;
#endif
#define OFF_T_DEFINED
#endif 

/* ErrNo Definitions 
 * Limits and extern 
 * list of strings */
#define _MAX_ERRNO	127
_CRT_EXTERN char *_errstrings[];

/* The C-Library Error Codes 
 * These closely relate the the 
 * cLibFileStream->code member */
#define CLIB_FCODE_EOF		0x1

/*******************************
 *       File Structures       *
 *******************************/
typedef struct _cLibFileStream
{
	/* The associated file
	 * descriptor. The id */
	int fd;

	/* Status Code 
	 * Updated by sysops */
	int code;

	/* Access Flags 
	 * Used for shortcuts */
	int flags;

} FILE, *PFILE;


/*******************************
 *       File Access           *
 *******************************/
_CRT_EXTERN int fflags(const char * mode);
_CRT_EXTERN int fclose(FILE * stream);
_CRT_EXTERN FILE *fopen(const char * filename, const char * mode);
_CRT_EXTERN FILE *fdopen(int fd, const char *mode);
_CRT_EXTERN FILE *freopen(const char * filename, const char * mode, FILE * stream);
_CRT_EXTERN int remove(const char * filename);
_CRT_EXTERN int fflush(FILE * stream);
_CRT_EXTERN int _fileno(FILE * stream);

//Rename
//tmpfile
//tmpnam

//extern void setbuf (FILE * stream, char * buffer);
//extern int setvbuf(FILE * stream, char * buffer, int mode, size_t size);

/*******************************
 *       Formatted IO          *
 *******************************/
_CRT_EXTERN int sprintf(char *str, const char *format, ...);
_CRT_EXTERN int snprintf(char *str, size_t size, const char *format, ...);
_CRT_EXTERN int vnprintf(char *str, size_t size, const char *format, ...);
_CRT_EXTERN int vsprintf(char *str, const char *format, va_list ap);
_CRT_EXTERN int vsnprintf(char *str, size_t size, const char *format, va_list ap);
_CRT_EXTERN int vasprintf(char **ret, const char *format, va_list ap);
_CRT_EXTERN int sscanf(const char *ibuf, const char *fmt, ...);
_CRT_EXTERN int vsscanf(const char *inp, char const *fmt0, va_list ap);

_CRT_EXTERN int fprintf(FILE * stream, const char * format, ...);
_CRT_EXTERN int vfprintf(FILE *stream, const char *format, va_list ap);
//_CRT_EXTERN int fscanf(FILE *stream, const char *format, ...);
//_CRT_EXTERN int vfscanf ( FILE * stream, const char * format, va_list arg );

_CRT_EXTERN int printf(const char *format, ...);
_CRT_EXTERN int vprintf(const char *format, va_list ap);
//_CRT_EXTERN int scanf(const char *format, ...);
//_CRT_EXTERN int vscanf(const char * format, va_list arg);

/* Helpers */
_CRT_EXTERN int __svfscanf(FILE *fp, char const * fmt0, va_list ap);

/*******************************
 *       Character IO          *
 *******************************/
_CRT_EXTERN int putchar(int character);
//_CRT_EXTERN int getchar(void);
//_CRT_EXTERN char *gets(char *sstr);
_CRT_EXTERN int puts(char *sstr);

_CRT_EXTERN int fpeekc(FILE * stream);
_CRT_EXTERN int fgetc(FILE * stream);
_CRT_EXTERN int fputc(int character, FILE * stream);
_CRT_EXTERN char *fgets(char * buf, size_t n, FILE * stream);
_CRT_EXTERN int fputs(const char * str, FILE * stream);
_CRT_EXTERN int fungetc (int character, FILE * stream);

#define peekc(stream) fpeekc(stream)
#define getc(stream) fgetc(stream)
#define putc(c, stream) fputc(c, stream)
#define ungetc(c, stream) fungetc(c, stream)

/*******************************
 *         Direct IO           *
 *******************************/
_CRT_EXTERN size_t fread(void * vptr, size_t size, size_t count, FILE * stream);
_CRT_EXTERN size_t fwrite(const void * vptr, size_t size, size_t count, FILE * stream);

/*******************************
 *     File Positioning        *
 *******************************/
_CRT_EXTERN int fgetpos(FILE * stream, fpos_t * pos);
_CRT_EXTERN int fsetpos(FILE * stream, const fpos_t * pos);
_CRT_EXTERN int fseek(FILE * stream, long int offset, int origin);
_CRT_EXTERN long int ftell(FILE * stream);
_CRT_EXTERN void rewind(FILE * stream);
_CRT_EXTERN int feof(FILE * stream);

_CRT_EXTERN off_t ftello(FILE *stream);
_CRT_EXTERN int fseeko(FILE *stream, off_t offset, int origin);

/*******************************
 *       Error Handling        *
 *******************************/
_CRT_EXTERN void clearerr(FILE * stream);
_CRT_EXTERN int ferror(FILE * stream);
_CRT_EXTERN void perror(const char * str);
_CRT_EXTERN char *strerror(int errnum);

#ifdef __cplusplus
}
#endif

#endif
