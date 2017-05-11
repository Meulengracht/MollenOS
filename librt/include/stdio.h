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
#include <os/osdefs.h>
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
#define stdout						(void*)1 //1
#define stdin						(void*)2 //2
#define	stderr						(void*)3 //3
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
#define _IOEOF				0x1
#define _IOREAD				0x2
#define _IOWRT				0x4
#define _IORW				(_IOREAD | _IOWRT)
#define _IOFBF				0x8
#define _IOLBF				0x10
#define _IONBF				0x20

/*******************************
 *       File Structures       *
 *******************************/
typedef struct _CFILE {
	UUId_t					 fd;
	void					*buffer;
	int						 code;
	Flags_t					 opts;
	Flags_t					 access;
} FILE, *PFILE;

/*******************************
 *       File Access           *
 *******************************/
_CRTIMP Flags_t faccess(__CONST char * mode);
_CRTIMP Flags_t fopts(__CONST char * mode);
_CRTIMP int fclose(FILE * stream);
_CRTIMP FILE *fopen(__CONST char * filename, __CONST char * mode);
_CRTIMP FILE *fdopen(int fd, __CONST char *mode);
_CRTIMP FILE *freopen(__CONST char * filename, __CONST char * mode, FILE * stream);
_CRTIMP int remove(__CONST char * filename);
_CRTIMP int fflush(FILE * stream);
_CRTIMP int _fileno(FILE * stream);

//Rename
//tmpfile
//tmpnam

//extern void setbuf (FILE * stream, char * buffer);
//extern int setvbuf(FILE * stream, char * buffer, int mode, size_t size);

/*******************************
 *       Formatted IO          *
 *******************************/
_CRTIMP int sprintf(char *str, __CONST char *format, ...);
_CRTIMP int snprintf(char *str, size_t size, __CONST char *format, ...);
_CRTIMP int vnprintf(char *str, size_t size, __CONST char *format, ...);
_CRTIMP int vsprintf(char *str, __CONST char *format, va_list ap);
_CRTIMP int vsnprintf(char *str, size_t size, __CONST char *format, va_list ap);
_CRTIMP int vasprintf(char **ret, __CONST char *format, va_list ap);
_CRTIMP int sscanf(__CONST char *ibuf, __CONST char *fmt, ...);
_CRTIMP int vsscanf(__CONST char *inp, char __CONST *fmt0, va_list ap);

_CRTIMP int fprintf(FILE * stream, __CONST char *format, ...);
_CRTIMP int vfprintf(FILE *stream, __CONST char *format, va_list ap);
_CRTIMP int fscanf(FILE *stream, __CONST char *format, ...);
_CRTIMP int vfscanf(FILE * stream, __CONST char *format, va_list arg);

_CRTIMP int printf(__CONST char *format, ...);
_CRTIMP int vprintf(__CONST char *format, va_list ap);
//_CRTIMP int scanf(__CONST char *format, ...);
//_CRTIMP int vscanf(__CONST char * format, va_list arg);

/* Helpers */
_CRTIMP int __svfscanf(FILE *fp, char __CONST * fmt0, va_list ap);

/*******************************
 *       Character IO          *
 *******************************/
_CRTIMP int putchar(int character);
_CRTIMP int getchar(void);
_CRTIMP char *gets(char *sstr);
_CRTIMP int puts(__CONST char *sstr);

_CRTIMP int fpeekc(FILE * stream);
_CRTIMP int fgetc(FILE * stream);
_CRTIMP int fputc(int character, FILE * stream);
_CRTIMP char *fgets(char * buf, size_t n, FILE * stream);
_CRTIMP int fputs(__CONST char * str, FILE * stream);
_CRTIMP int fungetc (int character, FILE * stream);

#define peekc(stream) fpeekc(stream)
#define getc(stream) fgetc(stream)
#define putc(c, stream) fputc(c, stream)
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
_CRTIMP void rewind(FILE * stream);
_CRTIMP int feof(FILE * stream);

_CRTIMP off_t ftello(FILE *stream);
_CRTIMP int fseeko(FILE *stream, off_t offset, int origin);

/*******************************
 *       Error Handling        *
 *******************************/
_CRTIMP void clearerr(FILE * stream);
_CRTIMP int ferror(FILE * stream);
_CRTIMP void perror(__CONST char * str);
_CRTIMP char *strerror(int errnum);

#ifdef __cplusplus
}
#endif

#endif
