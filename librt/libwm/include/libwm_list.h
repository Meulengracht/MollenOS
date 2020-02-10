/**
 * MollenOS
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
 * Wm List Type Definitions & Structures
 * - This header describes the base list-structure, prototypes
 *   and functionality, refer to the individual things for descriptions
 */

#ifndef __LIBWM_LIST_H__
#define __LIBWM_LIST_H__

#include "libwm_types.h"

typedef struct wm_list {
    struct wm_object_header* head;
} wm_list_t;

static struct wm_object_header*
wm_list_lookup(struct wm_list* list, int id)
{
    struct wm_object_header* item;
    
    item = list->head;
    while (item) {
        if (item->id == id) {
            return item;
        }
        item = item->link;
    }
    return NULL;
}

static void
wm_list_append(struct wm_list* list, struct wm_object_header* item)
{
    if (!list->head) {
        list->head = item;
    }
    else {
        struct wm_object_header* itr = list->head;
        while (itr->link) {
            itr = itr->link;
        }
        itr->link = item;
    }
}

static void
wm_list_remove(struct wm_list* list, struct wm_object_header* item)
{
    if (list->head == item) {
        list->head = item->link;
    }
    else {
        struct wm_object_header* itr = list->head;
        while (itr->link != item) {
            itr = itr->link;
        }
        itr->link = item->link;
    }
}

#endif // !__LIBWM_LIST_H__
