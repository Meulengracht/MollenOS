/* MollenOS
 *
 * Copyright 2019, Philip Meulengracht
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
 * - Standard IO Support functions
 */
//#define __TRACE

#include <assert.h>
#include <ddk/utils.h>
#include "libc_io.h"
#include <stdlib.h>
#include <string.h>

static int*       stdio_fd_bitmap = NULL;
static spinlock_t stdio_fd_lock   = _SPN_INITIALIZER_NP(spinlock_plain);

int stdio_bitmap_initialize(void)
{
    stdio_fd_bitmap = (int *)malloc(DIVUP(INTERNAL_MAXFILES, 8));
    assert(stdio_fd_bitmap != NULL);
    memset(stdio_fd_bitmap, 0, DIVUP(INTERNAL_MAXFILES, 8));
    return 0;
}

int stdio_bitmap_allocate(int fd)
{
    int result  = -1;
    int i, j;
    TRACE("stdio_bitmap_allocate(%i)", fd);

    // Trying to allocate a specific fd?
    spinlock_acquire(&stdio_fd_lock);
    if (fd >= 0) {
        int Block   = fd / (8 * sizeof(int));
        int Offset  = fd % (8 * sizeof(int));
        if (stdio_fd_bitmap[Block] & (1 << Offset)) {
            result  = -1;
        }
        else {
            stdio_fd_bitmap[Block] |= (1 << Offset);
            result  = fd;
        }
    }
    else {
        // Iterate the bitmap and find a free fd
        for (i = 0; i < DIVUP(INTERNAL_MAXFILES, (8 * sizeof(int))); i++) {
            for (j = 0; j < (8 * sizeof(int)); j++) {
                if (!(stdio_fd_bitmap[i] & (1 << j))) {
                    stdio_fd_bitmap[i] |= (1 << j);
                    result = (i * (8 * sizeof(int))) + j;
                    break;
                }
            }

            // Early breakout?
            if (j != (8 * sizeof(int))) {
                break;
            }
        }
    }
    spinlock_release(&stdio_fd_lock);
    return result;
}

void stdio_bitmap_free(int fd)
{
    int block;
    int offset;

    if (fd > STDERR_FILENO) {
        block  = fd / (8 * sizeof(int));
        offset = fd % (8 * sizeof(int));

        // Set the given fd index to free
        spinlock_acquire(&stdio_fd_lock);
        stdio_fd_bitmap[block] &= ~(1 << offset);
        spinlock_release(&stdio_fd_lock);
    }
}
