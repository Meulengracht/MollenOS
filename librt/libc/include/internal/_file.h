#ifndef __INTERNAL_FILE_H__
#define __INTERNAL_FILE_H__

#include <os/usched/mutex.h>
#include <stdio.h>

typedef struct _FILE {
    // IOD is the underlying io-descriptor. If there is no
    // underlying file-descriptor, this is memory stream.
    int IOD;
    // Flags are used to keep state of the FILE stream.
    uint16_t Flags;
    // BufferMode is used to keep track of buffering.
    uint8_t BufferMode;
    // StreamMode is keeping track of the last operation
    // performed.
    uint8_t StreamMode;
    // Buffer management. When FILE streams are buffered
    // we use the following _base, _ptr and _cnt to keep
    // track of buffering. _ptr is where the buffer pointer
    // currently sits at, _cnt is the number of bytes left
    // and _base is the base of the buffer. _bufsize is the size
    // of the buffer pointed to by _base.
    char* _ptr;
    int   _cnt;
    char* _base;
    int   _bufsiz;

    // Even for unbuffered streams, we keep a small buffer big
    // enough to store an unicode character. In essence this means
    // that _base/_ptr will never be NULL and _bufsize never 0.
    uint32_t _charbuf;

    // undocumented
    char* _tmpfname;

    // Lock is the file lock that can be acquired through the
    // flockfile/funlockfile for concurrency.
    struct usched_mtx Lock;
} FILE;

// Values for FILE::Flags
#define _IOMYBUF   0x0001 // buffer used is allocated by stdio
#define _IOUSRBUF  0x0002 // user-provided buffer
#define _IOEOF     0x0004 // at end of file
#define _IOERR     0x0008 // stream is in error mode

#define _IORD      0x0010 // Read is allowed
#define _IOWR      0x0020 // Write is allowed
#define _IORW      (_IORD | _IOWR)
#define _FWIDE     0x0040
#define _FBYTE     0x0080
#define _IOVRT     0x0100

// Values for FILE::BufferMode are _IONBF/_IOLBF/_IOFBF

// Values for FILE::StreamMode
#define __STREAMMODE_READ  0x01 // currently reading
#define __STREAMMODE_WRITE 0x02 // currently writing

static inline bool __FILE_IsStrange(FILE* stream) {
    return stream->IOD == -1;
}

static inline bool __FILE_ShouldFlush(FILE* stream) {
    return stream->StreamMode == __STREAMMODE_WRITE;
}

static inline void __FILE_SetStreamMode(FILE* stream, uint8_t mode) {
    stream->StreamMode = mode;
}

static inline bool __FILE_StreamModeSupported(FILE* stream, uint8_t mode) {
    if (mode == __STREAMMODE_READ) {
        return (stream->Flags & _IORD) != 0 ? true : false;
    } else if (mode == __STREAMMODE_WRITE) {
        return (stream->Flags & _IOWR) != 0 ? true : false;
    }
    return false;
}

static inline bool __FILE_IsBuffered(FILE* stream) {
    return stream->BufferMode != _IONBF;
}

static inline size_t __FILE_BytesBuffered(FILE* stream) {
    return (size_t)(stream->_ptr - stream->_base);
}

#endif //!__INTERNAL_FILE_H__
