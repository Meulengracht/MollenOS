/* Portable implementation of the aPLib API used by Vali's image tools.
 * The payload is an apultra bitstream; the AP32 container remains unchanged.
 */
#include "aplib.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "libapultra.h"

#define AP32_HEADER_SIZE 24u
#define AP32_MAGIC 0x32335041u

unsigned int aplib_depack_ap32(const void*, unsigned int, void*, unsigned int);

static uint32_t read32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void write32(unsigned char* p, uint32_t value)
{
    p[0] = (unsigned char)value;
    p[1] = (unsigned char)(value >> 8);
    p[2] = (unsigned char)(value >> 16);
    p[3] = (unsigned char)(value >> 24);
}

unsigned int aP_crc32(const void* source, unsigned int length)
{
    const unsigned char* p = source;
    uint32_t crc = UINT32_MAX;
    unsigned int bit;
    if (!source && length) {
        return APLIB_ERROR;
    }
    while (length--) {
        crc ^= *p++;
        for (bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

unsigned int aP_max_packed_size(unsigned int inputsize)
{
    /* apultra's bound, evaluated in 64 bits before any size_t arithmetic.
     * Its compressor also uses signed int input/output sizes internally.
     * Empty streams cannot be read by the existing boot decompressors.
     */
    uint64_t size = ((uint64_t)inputsize * 9u + 18u) / 8u + AP32_HEADER_SIZE;
    if (!inputsize || size > INT_MAX) {
        return APLIB_ERROR;
    }
    return (unsigned int)size;
}

unsigned int aP_workmem_size(unsigned int inputsize)
{
    /* apultra owns its scratch allocations. Preserve malloc/free callers. */
    return aP_max_packed_size(inputsize) == APLIB_ERROR ? APLIB_ERROR : 1u;
}

unsigned int aP_pack(const void* source, void* destination, unsigned int length,
                    void* workmem,
                    int (*callback)(unsigned int, unsigned int, unsigned int, void*),
                    void* cbparam)
{
    unsigned int capacity = aP_max_packed_size(length);
    size_t packed;
    (void)workmem;
    if (!source || !destination || capacity == APLIB_ERROR) {
        return APLIB_ERROR;
    }
    /* The upstream progress callback has no context or cancellation support.
     * Notify at entry/exit rather than introduce shared callback state.
     */
    if (callback && !callback(length, 0, 0, cbparam)) {
        return APLIB_ERROR;
    }
    packed = apultra_compress(source, destination, length,
        capacity - AP32_HEADER_SIZE, 0, 0, 0, NULL, NULL);
    if (packed == (size_t)-1 || packed > capacity - AP32_HEADER_SIZE) {
        return APLIB_ERROR;
    }
    if (callback && !callback(length, length, (unsigned int)packed, cbparam)) {
        return APLIB_ERROR;
    }
    return (unsigned int)packed;
}

unsigned int aPsafe_pack(const void* source, void* destination, unsigned int length,
                        void* workmem,
                        int (*callback)(unsigned int, unsigned int, unsigned int, void*),
                        void* cbparam)
{
    unsigned char* output = destination;
    unsigned int packed;
    if (!source || !output || aP_max_packed_size(length) == APLIB_ERROR) {
        return APLIB_ERROR;
    }
    packed = aP_pack(source, output + AP32_HEADER_SIZE, length,
                    workmem, callback, cbparam);
    if (packed == APLIB_ERROR) {
        return APLIB_ERROR;
    }
    write32(output, AP32_MAGIC);
    write32(output + 4, AP32_HEADER_SIZE);
    write32(output + 8, packed);
    write32(output + 12, aP_crc32(output + AP32_HEADER_SIZE, packed));
    write32(output + 16, length);
    write32(output + 20, aP_crc32(source, length));
    return AP32_HEADER_SIZE + packed;
}

unsigned int aPsafe_get_orig_size(const void* source)
{
    const unsigned char* input = source;
    /* This legacy API has no buffer length: callers must provide a complete
     * header. Use aPsafe_depack to validate a bounded input buffer.
     */
    if (!input || read32(input) != AP32_MAGIC ||
        read32(input + 4) < AP32_HEADER_SIZE) {
        return APLIB_ERROR;
    }
    return read32(input + 16);
}

unsigned int aPsafe_check(const void* source)
{
    const unsigned char* input = source;
    unsigned int original = aPsafe_get_orig_size(source);
    if (original == APLIB_ERROR) {
        return APLIB_ERROR;
    }
    /* Like the original API, this requires a complete, trusted-size buffer. */
    if (aP_crc32(input + read32(input + 4), read32(input + 8)) != read32(input + 12)) {
        return APLIB_ERROR;
    }
    return original;
}

unsigned int aPsafe_depack(const void* source, unsigned int srclen,
                         void* destination, unsigned int dstlen)
{
    const unsigned char* input = source;
    unsigned int header, packed, original, decoded;
    if (!input || !destination || srclen < AP32_HEADER_SIZE || read32(input) != AP32_MAGIC) {
        return APLIB_ERROR;
    }
    header = read32(input + 4);
    packed = read32(input + 8);
    original = read32(input + 16);
    if (header < AP32_HEADER_SIZE || header > srclen || !packed ||
        packed > srclen - header || !original || original > dstlen || original == APLIB_ERROR) {
        return APLIB_ERROR;
    }
    if (aP_crc32(input + header, packed) != read32(input + 12)) {
        return APLIB_ERROR;
    }
    /* The production decoder skips 24 header bytes. Allow legacy extended
     * headers by positioning its input exactly 24 bytes before the payload.
     */
    decoded = aplib_depack_ap32(input + header - AP32_HEADER_SIZE,
        packed + AP32_HEADER_SIZE, destination, original);
    if (decoded != original || aP_crc32(destination, decoded) != read32(input + 20)) {
        return APLIB_ERROR;
    }
    return decoded;
}
