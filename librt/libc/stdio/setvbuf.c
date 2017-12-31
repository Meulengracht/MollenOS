/* 
 * Specifies a buffer for stream. The function allows to 
 * specify the mode and size of the buffer (in bytes).
 * If buffer is a null pointer, the function automatically 
 * allocates a buffer (using size as a hint on the size to use). 
 * Otherwise, the array pointed by buffer may be used as 
 * a buffer of size bytes.
 */
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

int setvbuf(
    _In_ FILE* file, 
    _In_ char *buf, 
    _In_ int mode, 
    _In_ size_t size)
{
    // Sanitize parameters and state
    if(file == NULL) return -1;
    if(mode != _IONBF && mode != _IOFBF && mode != _IOLBF) return -1;
    if(mode == _IONBF && (size >= 2 && size <= INT_MAX)) return -1;
    _lock_file(file);

    fflush(file);
    if(file->_flag & _IOMYBUF)
        free(file->_base);
    file->_flag &= ~(_IONBF | _IOMYBUF | _USERBUF);
    file->_cnt = 0;

    if(mode == _IONBF) {
        file->_flag |= _IONBF;
        file->_base = file->_ptr = (char*)&file->_charbuf;
        file->_bufsiz = 2;
    }else if(buf) {
        file->_base = file->_ptr = buf;
        file->_flag |= _USERBUF;
        file->_bufsiz = size;
    }else {
        file->_base = file->_ptr = malloc(size);
        if(!file->_base) {
            file->_bufsiz = 0;
            _unlock_file(file);
            return -1;
        }

        file->_flag |= _IOMYBUF;
        file->_bufsiz = size;
    }
    _unlock_file(file);
    return 0;
}
