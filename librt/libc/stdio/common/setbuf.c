/* 
 * Specifies the buffer to be used by the stream for I/O 
 * operations, which becomes a fully buffered stream. Or, 
 * alternatively, if buffer is a null pointer, buffering is 
 * disabled for the stream, which becomes an unbuffered stream.
 */
#include <stdio.h>

void setbuf(
    _In_ FILE* file, 
    _In_ char *buf)
{
    setvbuf(file, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}