
#define __TEST

#define __FILE_defined 1
#include <wchar.h>
#include <locale.h>
#include <langinfo.h>
int printf(const char *format, ...);
int wprintf (const wchar_t* format, ...);

typedef unsigned long off64_t;

#include "common.h"
#include "stdio_mock.h"

#define _set_errno(err)

#include "../librt/libc/stdio/io/fread.c"

static const unsigned char asciiData[] = {
        0x48, 0x65, 0x6a, 0x20, 0x6d, 0x65, 0x64, 0x20, 0x64, 0x69, 0x67, 0x2c, 0x20, 0x6d,
        0x69, 0x74, 0x20, 0x6e, 0x61, 0x76, 0x6e, 0x20, 0x65, 0x72, 0x20, 0x70, 0x68, 0x69,
        0x6c, 0x69, 0x70, 0x2c, 0x20, 0x6a, 0x65, 0x67, 0x20, 0x65, 0x6c, 0x73, 0x6b, 0x65,
        0x72, 0x20, 0x61, 0x74, 0x20, 0x68, 0x61, 0x76, 0x65, 0x20, 0x64, 0x65, 0x74, 0x20,
        0x73, 0x6a, 0x6f, 0x76, 0x74
};

static const unsigned char utf8Data[] = {
        0x52, 0xc3, 0xb8, 0x64, 0x67, 0x72, 0xc3, 0xb8, 0x64,
        0x20, 0x6d, 0x65, 0x64, 0x20, 0x66, 0x6c, 0xc3, 0xb8, 0x64, 0x65
};

static const unsigned short utf16Data[] = {
        0x5200, 0xf800, 0x6400, 0x6700, 0x7200, 0xf800, 0x6400, 0x2000, 0x6d00, 0x6500, 0x6400, 0x2000,
        0x6600, 0x6c00, 0xf800, 0x6400, 0x6500, 0x2c00, 0x2000, 0x6400, 0x6500, 0x7400, 0x2000, 0x6500,
        0x7200, 0x2000, 0x6400, 0x6500, 0x6a00, 0x6c00, 0x6900, 0x6700, 0x7400,
};

// read context
static int   globalTestIndex;
static int   globalTestMaxIndex;
static void* data;

static void test_initialize_data(void* dataToUse, size_t dataSize) {
    globalTestIndex = 0;
    globalTestMaxIndex = (int)dataSize;
    data  = dataToUse;
}

static oscode_t test_read(stdio_handle_t* handle, void* buffer, size_t len, size_t* lenOut)
{
    unsigned char* dest   = buffer;
    unsigned char* source = data;
    int i = globalTestIndex;
    int j = 0;

    printf(STR("[test_read] length %" PRIuIN), len);

    while (j < len && i < globalTestMaxIndex) {
        dest[j++] = source[i++];
    }

    *lenOut = (size_t)(i - globalTestIndex);
    globalTestIndex = i;
    return OsSuccess;
}

static oscode_t test_seek(stdio_handle_t* handle, int origin, off64_t offset, long long* posOut)
{
    printf(STR("[test_seek] origin %i, offset %" PRIuIN), origin, offset);
    if ((int)offset > globalTestMaxIndex) {
        globalTestIndex = globalTestMaxIndex;
    }
    else {
        globalTestIndex = (int)offset;
    }

    *posOut = (long long)globalTestIndex;
    return OsSuccess;
}

static void test_binary(stdio_handle_t* handle)
{
    char buffer[512] = { 0 };
    int  status;

    test_initialize_data((void*)&asciiData[0], sizeof(asciiData));
    handle->wxflag = 0;

    printf(STR("[test_binary] Testing file reading as binary"));
    status = __read_as_binary(handle, &buffer[0], sizeof(buffer));
    printf(STR("[test_binary] Result: %i"), status);
}

static void test_text(stdio_handle_t* handle)
{
    char buffer[512] = { 0 };
    int  status;

    test_initialize_data((void*)&asciiData[0], sizeof(asciiData));
    handle->wxflag = WX_TEXT;

    printf(STR("[test_text] Testing file reading as binary"));
    status = __read_as_text_or_wide(handle, &buffer[0], sizeof(buffer));
    printf(STR("[test_text] Result: %i"), status);
    printf(STR("[test_text] %s"), &buffer[0]);
}

static int test_utf8(stdio_handle_t* handle)
{
    char buffer[512] = { 0 };
    int  status;

    test_initialize_data((void*)&utf8Data[0], sizeof(utf8Data));

    handle->wxflag = WX_UTF;

    printf(STR("[test_utf8] Testing file reading as utf8"));
    status = __read_as_utf8(handle, (wchar_t*)&buffer[0], sizeof(buffer));
    printf(STR("[test_utf8] Result: %i"), status);
    wprintf(L"[test_utf8] %s\n", (wchar_t*)&buffer[0]);
}

static int test_utf16(stdio_handle_t* handle)
{
    char buffer[512] = { 0 };
    int  status;

    test_initialize_data((void*)&utf16Data[0], sizeof(utf16Data));

    handle->wxflag = WX_UTF | WX_WIDE;

    printf(STR("[test_utf16] Testing file reading as utf16"));
    status = __read_as_text_or_wide(handle, &buffer[0], sizeof(buffer));
    printf(STR("[test_utf16] Result: %i"), status);
    wprintf(L"[test_utf16] %s\n", (wchar_t*)&buffer[0]);
}

// ./fread_tests.c
int main(int argc, char **argv)
{
    // initialize the test stdio handle
    stdio_handle_t testHandle = {
        .fd = 1,
        .object = {
            .handle = 1,
            .type = 1
        },
        .ops = {
            .read = test_read,
            .seek = test_seek
        },
        .wxflag = 0,
        .lookahead = { '\n', '\n', '\n' },
        .buffered_stream = NULL
    };

    if (NULL == setlocale(LC_CTYPE, "") || 0 != strcmp(nl_langinfo(CODESET), "UTF-8")) {
        return -1;
    }

    test_binary(&testHandle);
    test_text(&testHandle);
    test_utf8(&testHandle);
    test_utf16(&testHandle);
    return 0;
}
