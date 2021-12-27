/* MollenOS
 *
 * Copyright 2017, Philip Meulengracht
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
 * C Standard Library
 * - Standard IO file operation implementations.
 */

//#define __TRACE

#include <sys_file_service_client.h>
#include <ddk/service.h>
#include <ddk/utils.h>
#include <errno.h>
#include <gracht/link/vali.h>
#include <internal/_io.h>
#include <internal/_utils.h>
#include <io.h>
#include <os/mollenos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOM_MAX_LEN 4

static const struct bom_mode {
    const char*   name;
    size_t        len;
    unsigned int  flags;
    unsigned char identifier[BOM_MAX_LEN];
} supported_bom_modes[] = {
        { "UTF32BE", 4, WX_UTF32 | WX_BIGENDIAN, { 0x00, 0x00, 0xFE, 0xFF } },
        { "UTF32LE", 4, WX_UTF32, { 0xFF, 0xFE, 0x00, 0x00 } },
        { "UTF16BE", 2, WX_UTF16 | WX_BIGENDIAN, { 0xFE, 0xFF } },
        { "UTF16LE", 2, WX_UTF16, { 0xFF, 0xFE } },
        { "UTF8", 3, WX_UTF, { 0xEF, 0xBB, 0xBF } },
        { NULL, 0, 0, { 0 }}
};

// Convert O_* flags to WX_* flags
static unsigned int __convert_o_to_wx_flags(unsigned int oflags)
{
    unsigned int wxflags = 0;

    // detect options
    if (oflags & O_APPEND)    wxflags |= WX_APPEND;
    if (oflags & O_NOINHERIT) wxflags |= WX_DONTINHERIT;

    // detect mode
    if (oflags & O_BINARY)       {/* Nothing to do */}
    else if (oflags & O_TEXT)    wxflags |= WX_TEXT;
    else if (oflags & O_WTEXT)   wxflags |= WX_WIDE;
    else if (oflags & O_U16TEXT) wxflags |= WX_UTF16;
    else if (oflags & O_U8TEXT)  wxflags |= WX_UTF;
    else                         wxflags |= WX_TEXT; // default to TEXT
    return wxflags;
}

static unsigned int __detect_filemode(int iod)
{
    char bomBuffer[BOM_MAX_LEN];
    int  bytesRead;

    bytesRead = read(iod, &bomBuffer[0], sizeof(bomBuffer));
    if (bytesRead > 0) {
        int i = 0;

        while (supported_bom_modes[i].name != NULL) {
            if (bytesRead >= supported_bom_modes[i].len &&
                !memcmp(&bomBuffer[0], &supported_bom_modes[i].identifier[0], supported_bom_modes[i].len)) {
                TRACE(STR("[__detect_filemode] mode automatically set to %s"), supported_bom_modes[i].name);
                lseek(iod, supported_bom_modes[i].len, SEEK_SET);
                return supported_bom_modes[i].flags;
            }
            i++;
        }
    }

    // not able to detect mode
    lseek(iod, 0, SEEK_SET);
    return 0;
}

// return -1 on fail and set errno
int open(const char* file, int flags, ...)
{
    struct vali_link_message msg = VALI_MSG_INIT_HANDLE(GetFileService());
    int                      status;
    OsStatus_t               osStatus;
    stdio_handle_t*          object;
    UUId_t                   handle;
    int                      pmode = 0;
    va_list                  ap;

    if (!file) {
        _set_errno(EINVAL);
        return -1;
    }

    // Extract pmode flags
    if (flags & O_CREAT) {
        va_start(ap, flags);
        pmode = va_arg(ap, int);
        va_end(ap);
    }

    // handle pmode flags
    if (pmode) {
        // @todo check permission flags for creation
    }
    
    // Try to open the file by directly communicating with the file-service
    status = sys_file_open(GetGrachtClient(), &msg.base, *__crt_processid_ptr(),
        file, _fopts(flags), _faccess(flags));
    if (status) {
        ERROR("open no communcation channel open");
        _set_errno(ECOMM);
        return -1;
    }

    status = gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
    if (status) {
        ERROR("open failed to wait for answer: %i", status);
        return -1;
    }

    sys_file_open_result(GetGrachtClient(), &msg.base, &osStatus, &handle);
    if (OsStatusToErrno(osStatus)) {
        ERROR("open(path=%s) failed with code: %u", file, osStatus);
        return -1;
    }

    TRACE("open retrieved handle %u", handle);
    if (stdio_handle_create(-1, __convert_o_to_wx_flags((unsigned int) flags), &object)) {
        sys_file_close(GetGrachtClient(), &msg.base, *__crt_processid_ptr(), handle);
        gracht_client_wait_message(GetGrachtClient(), &msg.base, GRACHT_MESSAGE_BLOCK);
        sys_file_close_result(GetGrachtClient(), &msg.base, &osStatus);
        return -1;
    }
    
    stdio_handle_set_handle(object, handle);
    stdio_handle_set_ops_type(object, STDIO_HANDLE_FILE);

    // detect filemode automatically
    if (flags & O_TEXT) {
        unsigned int detectedMode;

        object->wxflag &= ~WX_TEXT;
        detectedMode = __detect_filemode(object->fd);
        object->wxflag |= WX_TEXT;
        if (detectedMode) {
            object->wxflag &= ~WX_TEXT_FLAGS;
            object->wxflag |= detectedMode;
        }
    }
    return object->fd;
}
