/* MollenOS
*
* Copyright 2011 - 2014, Philip Meulengracht
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
* MollenOS Common - List Implementation
*/

#ifndef _LIST_H_
#define _LIST_H_

/* List Includes */
#include <arch.h>
#include <crtdefs.h>
#include <stddef.h>

/* List Structures */
typedef struct _list_node 
{
	/* Link */
	struct _list_node *link;
	
	/* Identifier */
	int identifier;

	/* Payload */
	void *data;
	
} list_node_t;


typedef struct _list_main 
{
	/* Head and Tail */
	list_node_t *head, **tailp;

	/* Attributes */
	int attributes;

	/* Length */
	int length;

	/* Perhaps we use a lock */
	spinlock_t lock;

} list_t;

/* List Definitions */
#define LIST_NORMAL		0x0
#define LIST_SAFE		0x1

/* Foreach Macro */
#define foreach(i, list) list_node_t *i; for (i = list->head; i != NULL; i = i->link)


/* List Prototypes */
_CRT_EXTERN list_t *list_create(int attributes);
_CRT_EXTERN void list_destroy(list_t *list);

_CRT_EXTERN list_node_t *list_create_node(int id, void *data);

_CRT_EXTERN void list_insert(list_t *list, list_node_t *node, int position);
_CRT_EXTERN void list_insert_front(list_t *list, list_node_t *node);
_CRT_EXTERN void list_append(list_t *list, list_node_t *node);

_CRT_EXTERN list_node_t *list_pop(list_t *list);
_CRT_EXTERN list_node_t *list_dequeue(list_t *list);

_CRT_EXTERN int list_get_index_by_data(list_t *list, void *data);
_CRT_EXTERN int list_get_index_by_id(list_t *list, int id);
_CRT_EXTERN int list_get_index_by_node(list_t *list, list_node_t *node);

_CRT_EXTERN list_node_t *list_get_node_by_id(list_t *list, int id, int n);
_CRT_EXTERN void *list_get_data_by_id(list_t *list, int id, int n);

_CRT_EXTERN void list_execute_on_id(list_t *list, void(*func)(void*, int), int id);
_CRT_EXTERN void list_execute_all(list_t *list, void(*func)(void*, int));

_CRT_EXTERN void list_remove_by_node(list_t *list, list_node_t* node);
_CRT_EXTERN void list_remove_by_index(list_t *list, int index);
_CRT_EXTERN void list_remove_by_id(list_t *list, int id);

#endif // !_LIST_H_
