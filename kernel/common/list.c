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

#include <list.h>
#include <heap.h>
#include <stddef.h>

/* Create a new list, empty, with given attributes */
list_t *list_create(int attributes)
{
	list_t *list = (list_t*)kmalloc(sizeof(list_t));
	list->attributes = attributes;
	list->head = NULL;
	list->tailp = &list->head;
	list->length = 0;

	/* Do we use a lock? */
	if (attributes & LIST_SAFE)
		spinlock_reset(&list->lock);

	return list;
}

/* Create a new node */
list_node_t *list_create_node(int id, void *data)
{
	/* Allocate a new node */
	list_node_t *node = (list_node_t*)kmalloc(sizeof(list_node_t));
	node->data = data;
	node->identifier = id;
	node->link = NULL;

	return node;
}

/* Inserts a node at the beginning of this list. */
void list_insert_front(list_t *list, list_node_t *node)
{
	/* Get lock */
	interrupt_status_t int_state;

	if (list->attributes & LIST_SAFE)
	{
		int_state = interrupt_disable();
		spinlock_acquire(&list->lock);
	}

	/* Set new node's link to the original head */
	node->link = list->head;

	/* Set new head */
	list->head = node;

	/* Update tail pointer */
	if (list->tailp == &list->head) {
		list->tailp = &node->link;
	}

	/* Increase Count */
	list->length++;

	/* Release Lock */
	if (list->attributes & LIST_SAFE)
	{
		spinlock_release(&list->lock);
		interrupt_set_state(int_state);
	}
}

/* Appends a node at the end of this list. */
void list_append(list_t *list, list_node_t *node) 
{
	/* Get lock */
	interrupt_status_t int_state = 0;

	if (list->attributes & LIST_SAFE)
	{
		int_state = interrupt_disable();
		spinlock_acquire(&list->lock);
	}

	/* Update tail pointer */
	*list->tailp = node;
	list->tailp = &node->link;

	/* Set last link NULL (EOL) */
	node->link = NULL;

	/* Increase Count */
	list->length++;

	/* Release Lock */
	if (list->attributes & LIST_SAFE)
	{
		spinlock_release(&list->lock);
		interrupt_set_state(int_state);
	}
}

/* Removes a node from this list. */
void list_remove_by_node(list_t *list, list_node_t* node) 
{
	/* Traverse the list to find the next pointer of the
	* node that comes before the one to be removed. */
	list_node_t *curr, **pnp = &list->head;
	interrupt_status_t int_state = 0;
	
	/* Get lock */
	if (list->attributes & LIST_SAFE)
	{
		int_state = interrupt_disable();
		spinlock_acquire(&list->lock);
	}


	while ((curr = *pnp) != NULL) 
	{
		if (curr == node) 
		{
			/* We found the node, so remove it. */
			*pnp = node->link;
			if (list->tailp == &node->link) 
			{
				list->tailp = pnp;
			}

			/* Decrease Count */
			list->length--;

			/* Set last link NULL */
			node->link = NULL;
			break;
		}

		/* Tail pointer */
		pnp = &curr->link;
	}

	/* Release Lock */
	if (list->attributes & LIST_SAFE)
	{
		spinlock_release(&list->lock);
		interrupt_set_state(int_state);
	}
}

/* Removes a node from this list. */
list_node_t *list_pop(list_t *list)
{
	/* Traverse the list to find the next pointer of the
	* node that comes before the one to be removed. */
	list_node_t *curr = NULL, **pnp = &list->head;
	interrupt_status_t int_state = 0;

	/* Get lock */
	if (list->attributes & LIST_SAFE)
	{
		int_state = interrupt_disable();
		spinlock_acquire(&list->lock);
	}

	if (*pnp != NULL)
	{
		/* We found a node so remove it. */
		curr = *pnp;
		*pnp = curr->link;

		/* EoL */
		if (list->tailp == &curr->link)
		{
			list->tailp = pnp;
		}

		/* Decrease Count */
		list->length--;

		/* Set last link NULL */
		curr->link = NULL;
	}

	/* Release Lock */
	if (list->attributes & LIST_SAFE)
	{
		spinlock_release(&list->lock);
		interrupt_set_state(int_state);
	}

	/* Return */
	return curr;
}

/* Get a node (n = indicates in case
 * we want more than one by that same ID */
list_node_t *list_get_node_by_id(list_t *list, int id, int n)
{
	int counter = n;
	foreach(i, list)
	{
		if (i->identifier == id)
		{
			if (counter == 0)
				return i;
			else
				counter--;
		}
	}

	/* If we reach here, not enough of id */
	return NULL;
}

/* Get data conatined in node (n = indicates in case
* we want more than one by that same ID */
void *list_get_data_by_id(list_t *list, int id, int n)
{
	int counter = n;
	foreach(i, list)
	{
		if (i->identifier == id)
		{
			if (counter == 0)
				return i->data;
			else
				counter--;
		}
	}

	/* If we reach here, not enough of id */
	return NULL;
}

/* Go through members and execute a function 
 * on each member matching the given id */
void list_execute_on_id(list_t *list, void(*func)(void*, int), int id)
{
	int n = 0;
	foreach(i, list)
	{
		/* Check */
		if (i->identifier == id)
		{
			/* Execute */
			func(i->data, n);

			/* Increase */
			n++;
		}
	}
}

/* Go through members and execute a function
* on each member matching the given id */
void list_execute_all(list_t *list, void(*func)(void*, int))
{
	int n = 0;
	foreach(i, list)
	{
		/* Execute */
		func(i->data, n);

		/* Increase */
		n++;
	}
}