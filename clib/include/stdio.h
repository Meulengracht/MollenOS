
#ifndef __STDIO_INC__
#define __STDIO_INC__

#include <stdint.h>
#include <crtdefs.h>
#include <stddef.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

//Defines
#define EOF				(-1)

#define SEEK_SET        0       /* Seek from beginning of file.  */
#define SEEK_CUR        1       /* Seek from current position.  */
#define SEEK_END        2       /* Set file pointer to EOF plus "offset" */

#define FILE_READ				0x100
#define FILE_WRITE				0x200
#define FILE_BINARY				0x400
#define FILE_APPEND				0x800
#define FILE_PLUS				0x1000
#define FILE_CREATE				0x2000

typedef uint32_t FILE_FLAGS;

#define MOS_FS_FILE					0x0
#define MOS_FS_READONLY				0x1
#define MOS_FS_HIDDEN				0x2
#define MOS_FS_SYSFILE				0x4
#define MOS_FS_DIRECTORY			0x10
#define MOS_FS_ARCHIEVE				0x20
#define MOS_FS_DEVICE				0x40
#define MOS_FS_INVALID				0x80

/* Extended Flags */
#define MOS_FS_PROCESS				0x100
#define MOS_FS_FILESYSTEM			0x200

//Streams
#define stdout						NULL //0
#define stdin						NULL //1
#define	stderr						NULL //2
#define BUFSIZ						(int)512
#define _IOFBF						0x1
#define _IOLBF						0x2
#define _IONBF						0x4

//Error strings
#define _MAX_ERRNO	127
extern char *_errstrings[];

//Structs
#pragma pack(push, 1)
typedef struct FileStream
{
	/* Internal OS Data */
	void *_handle;
	void *_buf;

	/* Public Members */
	uint64_t Size;
	uint32_t Position;
	uint32_t Flags;
	uint32_t BlockSize;
	uint8_t Drive;
	uint8_t IsEndOfFile;
	int32_t Error;

} FILE, *PFILE;
#pragma pack(pop)

/* Operations on Files */
extern int remove(const char * filename);
//Rename
//tmpfile
//tmpnam

/* File Access */
extern int fclose(FILE * stream);
extern int fflush(FILE * stream);
extern FILE *fopen(const char * filename, const char * mode);
extern FILE *freopen(const char * filename, const char * mode, FILE * stream);
extern void setbuf (FILE * stream, char * buffer);
extern int setvbuf(FILE * stream, char * buffer, int mode, size_t size);

/* Formatted IO */
extern int fprintf(FILE * stream, const char * format, ...);
extern int fscanf(FILE *stream, const char *format, ...);
extern int scanf(const char *format, ...);
extern int sscanf(char *out, const char *format, ...);
extern int printf(const char *format, ...);
extern int sprintf(char *str, const char *format, ...);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern int vprintf(const char *format, va_list ap);
extern int vnprintf(char *str, size_t size, const char *format, ...);
extern int vfprintf(FILE *stream, const char *format, va_list ap);
extern int vsprintf(char *str, const char *format, va_list ap);
extern int vsnprintf(char *str, size_t size, const char *format, va_list ap);
extern int vasprintf(char **ret, const char *format, va_list ap);

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
