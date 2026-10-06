/** Firmware platform devices carry descriptions, not PCI configuration or IO grants. */
#ifndef __DDK_PLATFORMDEVICE_H__
#define __DDK_PLATFORMDEVICE_H__

#include <ddk/device.h>
#include <string.h>

#define PLATFORM_DEVICE_VERSION 1
#define PLATFORM_DEVICE_MAX_COMPATIBLES 512
#define PLATFORM_DEVICE_MAX_REGISTERS 8
#define PLATFORM_DEVICE_MAX_INTERRUPTS 8
#define PLATFORM_DEVICE_PENDING_INTERRUPTS 0x1U
#define PLATFORM_DEVICE_PENDING_DMA 0x2U

/** Physical register extent. A driver still needs an authorized IO mapping. */
struct PlatformRegister {
    uint64_t Base;
    uint64_t Length;
};

/** Firmware controller identity and local source; never a CPU interrupt line. */
struct PlatformInterrupt {
    uint32_t Controller;
    uint32_t Number;
    uint32_t Type;
};

/** Self-contained platform description sent at driver binding. Compatible strings
 * are a bounded NUL-separated list in firmware preference order. FirmwareNode is
 * an opaque node offset in the boot firmware tree, not an address or IO handle.
 * A nonzero Pending field requires the driver to defer hardware initialization.
 */
typedef struct PlatformDevice {
    Device_t Base;
    uint32_t Version;
    uint32_t Pending;
    uint32_t FirmwareNode;
    uint32_t CompatibleLength;
    char Compatibles[PLATFORM_DEVICE_MAX_COMPATIBLES];
    uint32_t RegisterCount;
    struct PlatformRegister Registers[PLATFORM_DEVICE_MAX_REGISTERS];
    uint32_t InterruptCount;
    struct PlatformInterrupt Interrupts[PLATFORM_DEVICE_MAX_INTERRUPTS];
} PlatformDevice_t;

/** @brief Checks descriptor bounds, complete compatible strings and resource extents. */
static inline int
PlatformDeviceValidate(
    _In_ const PlatformDevice_t* device)
{
    const char* end;
    size_t offset = 0;
    unsigned int i;

    if (device->Base.Length != sizeof(*device) || device->Version != PLATFORM_DEVICE_VERSION) {
        return 0;
    }
    if (!device->CompatibleLength || device->CompatibleLength > PLATFORM_DEVICE_MAX_COMPATIBLES) {
        return 0;
    }
    if (device->RegisterCount > PLATFORM_DEVICE_MAX_REGISTERS ||
        device->InterruptCount > PLATFORM_DEVICE_MAX_INTERRUPTS) {
        return 0;
    }
    while (offset < device->CompatibleLength) {
        end = memchr(device->Compatibles + offset, 0, device->CompatibleLength - offset);
        if (end == NULL || end == device->Compatibles + offset) {
            return 0;
        }
        offset = (size_t)(end - device->Compatibles) + 1;
    }
    for (i = 0; i < device->RegisterCount; i++) {
        if (!device->Registers[i].Length ||
            device->Registers[i].Length - 1 > UINT64_MAX - device->Registers[i].Base) {
            return 0;
        }
    }
    for (i = 0; i < device->InterruptCount; i++) {
        if (!device->Interrupts[i].Controller ||
            (device->Interrupts[i].Type != 1 && device->Interrupts[i].Type != 4)) {
            return 0;
        }
    }
    return 1;
}

#endif
