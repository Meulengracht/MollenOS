/**
 * Copyright, Philip Meulengracht
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <arch/output.h>
#include "private.h"

static BootTerminal_t     g_terminal;
static volatile uint32_t* g_uart;

oserr_t
SerialPortInitialize(void)
{
    if (!g_arm64Platform.Uart) {
        return OS_ENOENT;
    }
    
    if (!g_uart) {
        g_uart = (void*)Arm64MapDevice(
            g_arm64Platform.Uart,
            g_arm64Platform.UartLength
        );
        if (!g_uart) {
            return OS_EOOM;
        }
    }
    
    // Preserve the firmware's pinmux, divisor and clock setup. The dedicated
    // BCM2712 debug PL011 and BCM2711 PL011 share this register interface.
    g_terminal.AvailableOutputs |= VIDEO_UART;
    return OS_EOK;
}

void
SerialPutCharacter(
    _In_ int character)
{
    if (!g_uart) {
        return;
    }
    
    // Do not write DR until FR.TXFF clears: a full FIFO cannot accept a byte.
    while (g_uart[ARM64_UART_FLAGS / ARM64_MMIO_REGISTER_SIZE] & ARM64_UART_TX_FULL) {
        __asm__ volatile("yield");
    }
    
    g_uart[ARM64_UART_DATA / ARM64_MMIO_REGISTER_SIZE] = (unsigned char)character;
    __asm__ volatile("dsb sy" ::: "memory");
}

BootTerminal_t*
VideoGetTerminal(void)
{
    return &g_terminal;
}

oserr_t
InitializeFramebufferOutput(void)
{
    // Native firmware supplies no VBoot framebuffer; display drivers can add
    // one after their DT-described display pipeline has been initialized.
    return OS_ENOTSUPPORTED;
}

void
VideoClear(
    _In_ uint32_t color)
{
    (void)color;
}

void
VideoFlush(void)
{
}

void
VideoDrawPixel(
    _In_ unsigned int x,
    _In_ unsigned int y,
    _In_ uint32_t     color)
{
    (void)x;
    (void)y;
    (void)color;
}

oserr_t
VideoDrawCharacter(
    _In_ unsigned int x,
    _In_ unsigned int y,
    _In_ int          character,
    _In_ uint32_t     background,
    _In_ uint32_t     foreground)
{
    (void)x;
    (void)y;
    (void)character;
    (void)background;
    (void)foreground;
    return OS_ENOTSUPPORTED;
}

void
VideoPutCharacter(
    _In_ int character)
{
    SerialPutCharacter(character);
}
