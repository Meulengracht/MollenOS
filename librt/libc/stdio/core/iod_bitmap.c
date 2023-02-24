/**
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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

//#define __TRACE

#include "assert.h"
#include "ddk/utils.h"
#include "errno.h"
#include "internal/_io.h"
#include "os/spinlock.h"
#include "stdlib.h"
#include "string.h"

static int*       g_stdioHandles     = NULL;
static spinlock_t g_stdioHandlesLock = _SPN_INITIALIZER_NP;

int stdio_bitmap_initialize(void)
{
    g_stdioHandles = (int *)malloc(DIVUP(INTERNAL_MAXFILES, 8));
    assert(g_stdioHandles != NULL);

    // reset all to 0 (available)
    memset(g_stdioHandles, 0, DIVUP(INTERNAL_MAXFILES, 8));
    return 0;
}

int stdio_bitmap_allocate(int fd)
{
    int result = -1;
    int i, j;

    TRACE("stdio_bitmap_allocate(%i)", fd);
    assert(g_stdioHandles != NULL);
    
    if (fd >= INTERNAL_MAXFILES) {
        _set_errno(ENOENT);
        return -1;
    }

    // Trying to allocate a specific fd?
    spinlock_acquire(&g_stdioHandlesLock);
    if (fd >= 0) {
        int Block  = fd / (8 * sizeof(int));
        int Offset = fd % (8 * sizeof(int));
        if (g_stdioHandles[Block] & (1 << Offset)) {
            result = -1;
        }
        else {
            g_stdioHandles[Block] |= (1 << Offset);
            result = fd;
        }
    }
    else {
        // due to initialization might try to allocate fd's before parsing the inheritation
        // we would like to reserve some of the lower fds for STDOUT, STDERR, STDIN
        j = 3;

        for (i = 0; i < DIVUP(INTERNAL_MAXFILES, (8 * sizeof(int))); i++) {
            for (; j < (8 * sizeof(int)); j++) {
                if (!(g_stdioHandles[i] & (1 << j))) {
                    g_stdioHandles[i] |= (1 << j);
                    result = (i * (8 * sizeof(int))) + j;
                    break;
                }
            }

            // Early breakout?
            if (j != (8 * sizeof(int))) {
                break;
            }
            j = 0;
        }
    }
    spinlock_release(&g_stdioHandlesLock);
    return result;
}

void stdio_bitmap_free(int fd)
{
    int block;
    int offset;

    assert(g_stdioHandles != NULL);

    if (fd > STDERR_FILENO && fd < INTERNAL_MAXFILES) {
        block  = fd / (8 * sizeof(int));
        offset = fd % (8 * sizeof(int));

        // Set the given fd index to free
        spinlock_acquire(&g_stdioHandlesLock);
        g_stdioHandles[block] &= ~(1 << offset);
        spinlock_release(&g_stdioHandlesLock);
    }
}
