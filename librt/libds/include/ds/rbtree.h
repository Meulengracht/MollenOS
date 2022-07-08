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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Red-Black Tree Implementation
 *  - Implements a binary tree with the red-black distribution
 */

#ifndef __LIBDS_RBTREE_H__
#define __LIBDS_RBTREE_H__

#include <ds/dsdefs.h>
#include <ds/shared.h>

typedef int (*rb_tree_cmp_fn)(void*, void*);

typedef struct rb_leaf {
    struct rb_leaf* parent;
    struct rb_leaf* left;
    struct rb_leaf* right;
    int             color;
    
    void*           key;
    void*           value;
} rb_leaf_t;

#define RB_LEAF_INIT(leaf, _key, _value) (leaf)->left = NULL; (leaf)->right = NULL; (leaf)->parent = NULL; (leaf)->color = 0; (leaf)->key = (void*)(uintptr_t)_key; (leaf)->value = _value

typedef struct rb_tree {
    rb_leaf_t*     root;
    rb_tree_cmp_fn cmp;
    syncobject_t   lock;
    rb_leaf_t      nil;
} rb_tree_t;

#define RB_TREE_INIT { NULL, rb_tree_cmp_default, SYNC_INIT, { NULL, NULL, NULL, 0, NULL, NULL } }

// Default provided comparators
DSDECL(int, rb_tree_cmp_default(void*,void*));
DSDECL(int, rb_tree_cmp_string(void*,void*));

/** 
 * rb_tree_construct
 * * Constructs and initializes a new red-black tree.
 * @param RBTree  [In] The red-black tree to initialize, must be allocated.
 * @param KeyType [In] The type of index key that will be used.
 */
DSDECL(void,
rb_tree_construct(
    _In_ rb_tree_t*));

DSDECL(void,
rb_tree_construct_cmp(
    _In_ rb_tree_t*,
    _In_ rb_tree_cmp_fn));

/** 
 * rb_tree_append
 * * Appends a new item to the tree, the key of the item must not exist already.
 * @param RBTree     [In] The red-black tree to append the item to.
 * @param RBTreeItem [In] The item to append to the tree, the key must not exist.
 */
DSDECL(oserr_t,
       rb_tree_append(
    _In_ rb_tree_t*,
    _In_ rb_leaf_t*));

/** 
 * rb_tree_lookup
 * * Looks up an item in the tree based on the provided key.
 * @param RBTree [In] The red-black tree to perform the lookup in.
 * @param Key    [In] The key to lookup.
 */
DSDECL(rb_leaf_t*,
rb_tree_lookup(
    _In_ rb_tree_t*,
    _In_ void*));
    
DSDECL(void*,
rb_tree_lookup_value(
    _In_ rb_tree_t*,
    _In_ void*));

/** 
 * rb_tree_minimum
 * * Retrieves the item with the lowest value.
 * @param RBTree [In] The red-black tree to perform the lookup in.
 */
DSDECL(rb_leaf_t*,
rb_tree_minimum(
	_In_ rb_tree_t*));

/** 
 * rb_tree_remove
 * * Removes and returns the item by the key provided.
 * @param RBTree [In] The red-black tree to perform the lookup in.
 * @param Key    [In] The key to lookup.
 */
DSDECL(rb_leaf_t*,
rb_tree_remove(
    _In_ rb_tree_t*,
    _In_ void*));

#endif //!__LIBDS_RBTREE_H__
