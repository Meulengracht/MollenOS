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
 * Red-Black Tree Implementation
 *  - Implements a binary tree with the red-black distribution
 */

#ifndef __LIBDS_RBTREE_H__
#define __LIBDS_RBTREE_H__

#include <ds/ds.h>

typedef struct _RBTreeItem {
    DataKey_t           Key;
    int                 Color;
    struct _RBTreeItem* Parent;
    struct _RBTreeItem* Left;
    struct _RBTreeItem* Right;
} RBTreeItem_t;

typedef struct _RBTree {
    SafeMemoryLock_t    SyncObject;
    KeyType_t           KeyType;
    RBTreeItem_t*       Root;
    RBTreeItem_t        NilItem;
} RBTree_t;

/** 
 * RBTreeConstruct
 * * Constructs and initializes a new red-black tree.
 * @param RBTree  [In] The red-black tree to initialize, must be allocated.
 * @param KeyType [In] The type of index key that will be used.
 */
CRTDECL(void,
RBTreeConstruct(
    _In_ RBTree_t* Tree,
    _In_ KeyType_t KeyType));

/** 
 * RBTreeAppend
 * * Appends a new item to the tree, the key of the item must not exist already.
 * @param RBTree     [In] The red-black tree to append the item to.
 * @param RBTreeItem [In] The item to append to the tree, the key must not exist.
 */
CRTDECL(OsStatus_t,
RBTreeAppend(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* TreeItem));

/** 
 * RBTreeLookupKey
 * * Looks up an item in the tree based on the provided key.
 * @param RBTree [In] The red-black tree to perform the lookup in.
 * @param Key    [In] The key to lookup.
 */
CRTDECL(RBTreeItem_t*,
RBTreeLookupKey(
    _In_ RBTree_t* Tree,
    _In_ DataKey_t Key));

/** 
 * RBTreeGetMinimum
 * * Retrieves the item with the lowest value.
 * @param RBTree [In] The red-black tree to perform the lookup in.
 */
CRTDECL(RBTreeItem_t*,
RBTreeGetMinimum(
	_In_ RBTree_t* Tree));

/** 
 * RBTreeRemove
 * * Removes and returns the item by the key provided.
 * @param RBTree [In] The red-black tree to perform the lookup in.
 * @param Key    [In] The key to lookup.
 */
CRTDECL(RBTreeItem_t*,
RBTreeRemove(
    _In_ RBTree_t* Tree,
    _In_ DataKey_t Key));

#endif //!__LIBDS_RBTREE_H__
