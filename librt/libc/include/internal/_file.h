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
    uint16_t BufferMode;
    // Buffer management. When FILE streams are buffered
    // we use the following _base, _ptr and _cnt to keep
    // track of buffering. _ptr is where the buffer pointer
    // currently sits at, _cnt is the number of bytes left
    // and _base is the base of the buffer. _bufsize is the size
    // of the buffer pointed to by _base.
    char* Base;
    char* Current;
    int   BufferSize;

    // BytesValid denotes how many bytes of the buffer has valid
    // data. This is important for shared buffering where we may need
    // to flush it still if the data was modified.
    int BytesValid;

    // Even for unbuffered streams, we keep a small buffer big
    // enough to store a unicode character. In essence this means
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
#define _IOAPPEND  0x0040

#define _FWIDE     0x0080
#define _FBYTE     0x0100
#define _IOVRT     0x0200
#define _IOMOD     0x0400 // Buffer was modified.
#define _IOFILLED  0x0800 // Buffer has been filled.

// Values for FILE::BufferMode are _IONBF/_IOLBF/_IOFBF

static inline bool __FILE_IsStrange(FILE* stream) {
    return stream->IOD == -1;
}

static inline bool __FILE_IsBuffered(FILE* stream) {
    return stream->BufferMode != _IONBF;
}

static inline int __FILE_BufferPosition(FILE* stream) {
    return (int)(stream->Current - stream->Base);
}

static inline int __FILE_BufferBytesForReading(FILE* stream) {
    int position = __FILE_BufferPosition(stream);
    if (position >= stream->BytesValid) {
        return 0;
    }
    return stream->BytesValid - position;
}

static inline int __FILE_BufferBytesForWriting(FILE* stream) {
    return stream->BufferSize - __FILE_BufferPosition(stream);
}

static inline void __FILE_UpdateBytesValid(FILE* stream) {
    int position = __FILE_BufferPosition(stream);
    if (position > stream->BytesValid) {
        stream->BytesValid = position;
    }
}

static inline bool __FILE_CanRead(FILE* stream) {
    return (stream->Flags & _IORD) != 0 ? true : false;
}

static inline bool __FILE_CanWrite(FILE* stream) {
    return (stream->Flags & _IOWR) != 0 ? true : false;
}

static inline bool __FILE_CanSeek(FILE* stream) {
    return (stream->Flags & _IOAPPEND) != 0 ? true : false;
}

static inline void __FILE_ResetBuffer(FILE* stream) {
    stream->BytesValid = 0;
    stream->Current = stream->Base;
}

static inline void __FILE_Streamout(FILE* stream, void* buffer, size_t count) {
    stream->IOD = -1;
    stream->Flags = _IOWR;
    stream->Base = (char*)buffer;
    stream->Current = (char*)buffer;
    stream->BufferSize = (int)count;
    stream->BytesValid = 0;
    usched_mtx_init(&stream->Lock, USCHED_MUTEX_RECURSIVE);
}

#endif //!__INTERNAL_FILE_H__
