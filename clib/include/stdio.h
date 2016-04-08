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

/* Definitions */
#define EOF				(-1)

/* Seek Set(s) */
#define SEEK_SET        0       /* Seek from beginning of file.  */
#define SEEK_CUR        1       /* Seek from current position.  */
#define SEEK_END        2       /* Set file pointer to EOF plus "offset" */

/* Standard I/O */
#define stdout						NULL //0
#define stdin						NULL //1
#define	stderr						NULL //2
#define BUFSIZ						(int)512
#define _IOFBF						0x1
#define _IOLBF						0x2
#define _IONBF						0x4

/* ErrNo */
#define _MAX_ERRNO	127
extern char *_errstrings[];

/* FileStream */
typedef struct _cLibFileStream
{
	/* Internal OS Data */
	void *_handle;

} FILE, *PFILE;

//Rename
//tmpfile
//tmpnam

/* File Access */
extern int fflags(const char * mode);
extern int fclose(FILE * stream);
extern FILE *fopen(const char * filename, const char * mode);
extern FILE *freopen(const char * filename, const char * mode, FILE * stream);
extern int remove(const char * filename);

//extern void setbuf (FILE * stream, char * buffer);
//extern int setvbuf(FILE * stream, char * buffer, int mode, size_t size);
//extern int fflush(FILE * stream);

/* Formatted IO */
extern int fprintf(FILE * stream, const char * format, ...);
extern int printf(const char *format, ...);
extern int sprintf(char *str, const char *format, ...);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int vprintf(const char *format, va_list ap);
extern int vnprintf(char *str, size_t size, const char *format, ...);
extern int vfprintf(FILE *stream, const char *format, va_list ap);
extern int vsprintf(char *str, const char *format, va_list ap);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int vasprintf(char **ret, const char *format, va_list ap);

//extern int fscanf(FILE *stream, const char *format, ...);
//extern int scanf(const char *format, ...);
//extern int sscanf(char *out, const char *format, ...);

/* Character IO */
extern int cflush(uint32_t color);
extern int fgetc(FILE * stream);
extern int fputc(int character, FILE * stream);
extern char *fgets(char * buf, int bsize, FILE * stream);
extern int fputs(const char * str, FILE * stream);
extern int getc(FILE *stream);
extern int putc(int character, FILE * stream);
extern int getchar(void);
extern char *gets(char *sstr);
extern int putchar(int character);
extern int puts(char *sstr);
//int ungetc ( int character, FILE * stream );

/* Direct IO */
extern size_t fread(void * vptr, size_t size, size_t count, FILE * stream);
extern size_t fwrite(const void * vptr, size_t size, size_t count, FILE * stream);

/* File Positioning */
//int fgetpos ( FILE * stream, fpos_t * pos );
extern int fseek(FILE * stream, long int offset, int origin);
//int fsetpos ( FILE * stream, const fpos_t * pos );
extern long int ftell(FILE * stream);
extern void rewind(FILE * stream);
extern int feof(FILE * stream);

/* Error Handling */
extern void clearerr(FILE * stream);
extern int ferror(FILE * stream);
extern void perror(const char * str); //Print errorno
extern char *strerror(int errnum); //Makes a string out of err no

#ifdef __cplusplus
}
#endif

#endif
