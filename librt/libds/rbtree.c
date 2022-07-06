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
 * Red-Black tree Implementation
 *  - Implements a binary tree with the red-black distribution
 */

#include <assert.h>
#include <ddk/barrier.h>
#include <ds/rbtree.h>
#include <string.h>

#define COLOR_BLACK 0
#define COLOR_RED   1

#define ITEM_NIL(tree)          &tree->nil
#define IS_ITEM_NIL(tree, leaf) (leaf == ITEM_NIL(tree))

#define TREE_LOCK   SYNC_LOCK(tree)
#define TREE_UNLOCK SYNC_UNLOCK(tree)

// return 0 on equal
int
rb_tree_cmp_default(void* leaf_key, void* key)
{
    uintptr_t lh = (uintptr_t)leaf_key;
    uintptr_t rh = (uintptr_t)key;
    if (lh > rh) {
        return 1;
    }
    else if (lh < rh) {
        return -1;
    }
    else {
        return 0;
    }
}

int
rb_tree_cmp_string(void* leaf_key, void* key)
{
    return strcmp((const char*)leaf_key, (const char*)key);
}

void
rb_tree_construct(
    _In_ rb_tree_t* tree)
{
    rb_tree_construct_cmp(tree, rb_tree_cmp_default);
}

void
rb_tree_construct_cmp(
    _In_ rb_tree_t*     tree,
    _In_ rb_tree_cmp_fn cmp_fn)
{
    assert(tree != NULL);
    assert(cmp_fn != NULL);
    
    RB_LEAF_INIT(ITEM_NIL(tree), 0, NULL);
    tree->root = ITEM_NIL(tree);
    tree->cmp  = cmp_fn;
    SYNC_INIT_FN(tree);
}

static void
rotate_left(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* leaf)
{
    if (!IS_ITEM_NIL(tree, leaf->parent)) {
        if (leaf == leaf->parent->left) {
            leaf->parent->left = leaf->right;
        }
        else {
            leaf->parent->right = leaf->right;
        }
        
        leaf->right->parent = leaf->parent;
        leaf->parent = leaf->right;
        if (!IS_ITEM_NIL(tree, leaf->right->left)) {
            leaf->right->left->parent = leaf;
        }
        
        leaf->right = leaf->right->left;
        leaf->parent->left = leaf;
    }
    else {
        rb_leaf_t* right = tree->root->right;
        
        tree->root->right   = right->left;
        right->left->parent = tree->root;
        tree->root->parent  = right;
        right->left         = tree->root;
        right->parent       = ITEM_NIL(tree);
        tree->root          = right;
    }
}

static void
rotate_right(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* leaf)
{
    if (!IS_ITEM_NIL(tree, leaf->parent)) {
        if (leaf == leaf->parent->left) {
            leaf->parent->left = leaf->left;
        }
        else {
            leaf->parent->right = leaf->left;
        }
        
        leaf->left->parent = leaf->parent;
        leaf->parent = leaf->left;
        if (!IS_ITEM_NIL(tree, leaf->left->right)) {
            leaf->left->right->parent = leaf;
        }
        
        leaf->left          = leaf->left->right;
        leaf->parent->right = leaf;
    }
    else {
        rb_leaf_t* left = tree->root->left;
        
        tree->root->left    = tree->root->left->right;
        left->right->parent = tree->root;
        tree->root->parent  = left;
        left->right         = tree->root;
        left->parent        = ITEM_NIL(tree);
        tree->root          = left;
    }
}

static void
fixup_tree(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* leaf)
{
    rb_leaf_t* i = leaf;
    
    while (i->parent->color == COLOR_RED) {
        rb_leaf_t* uncle;
        if (i->parent == i->parent->parent->left) {
            uncle = i->parent->parent->right;
            if (!IS_ITEM_NIL(tree, uncle) && uncle->color == COLOR_RED) {
                i->parent->color = COLOR_BLACK;
                uncle->color = COLOR_BLACK;
                i->parent->parent->color = COLOR_RED;
                i = i->parent->parent;
                continue;
            }
            
            // Check if double rotation is required
            if (i == i->parent->right) {
                i = i->parent;
                rotate_left(tree, i);
            }
            
            i->parent->color = COLOR_BLACK;
            i->parent->parent->color = COLOR_RED;
            rotate_right(tree, i->parent->parent);
        }
        else {
            uncle = i->parent->parent->left;
            if (!IS_ITEM_NIL(tree, uncle) && uncle->color == COLOR_RED) {
                i->parent->color = COLOR_BLACK;
                uncle->color = COLOR_BLACK;
                i->parent->parent->color = COLOR_RED;
                i = i->parent->parent;
                continue;
            }
            
            // Check if double rotation is required
            if (i == i->parent->left) {
                i = i->parent;
                rotate_right(tree, i);
            }
            
            i->parent->color         = COLOR_BLACK;
            i->parent->parent->color = COLOR_RED;
            rotate_left(tree, i->parent->parent);
        }
    }
    tree->root->color = COLOR_BLACK;
}

oscode_t
rb_tree_append(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* leaf)
{
    rb_leaf_t* i;
    int        result;
    
    if (!tree || !leaf) {
        return OsInvalidParameters;
    }
    
    leaf->left  = ITEM_NIL(tree);
    leaf->right = ITEM_NIL(tree);
    
    TREE_LOCK;
    if (IS_ITEM_NIL(tree, tree->root)) {
        tree->root = leaf;
        
        leaf->color  = COLOR_BLACK;
        leaf->parent = ITEM_NIL(tree);
    }
    else {
        leaf->color = COLOR_RED;
        smp_mb();
        i = tree->root;
        while (1) {
            result = tree->cmp(i->key, leaf->key);
            if (result == 1) {
                if (IS_ITEM_NIL(tree, i->left)) {
                    i->left = leaf;
                    leaf->parent = i;
                    break;
                }
                else {
                    i = i->left;
                }
            }
            else if (result == -1) {
                if (IS_ITEM_NIL(tree, i->right)) {
                    i->right = leaf;
                    leaf->parent = i;
                    break;
                }
                else {
                    i = i->right;
                }
            }
            else {
                TREE_UNLOCK;
                return OsExists;
            }
        }
        fixup_tree(tree, leaf);
    }
    TREE_UNLOCK;
    return OsOK;
}

static rb_leaf_t*
lookup_recursive(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* leaf,
    _In_ void*      key)
{
    int result = tree->cmp(leaf->key, key);
    if (!result) {
        return leaf;
    }
    
    if (result == 1) {
        if (!IS_ITEM_NIL(tree, leaf->left)) {
            return lookup_recursive(tree, leaf->left, key);
        }
    }
    else {
        if (!IS_ITEM_NIL(tree, leaf->right)) {
            return lookup_recursive(tree, leaf->right, key);
        }
    }
    return NULL;
}

rb_leaf_t*
rb_tree_lookup(
    _In_ rb_tree_t* tree,
    _In_ void*      key)
{
    rb_leaf_t* leaf;
    assert(tree != NULL);
    assert(key != NULL);
    
    TREE_LOCK;
    if (IS_ITEM_NIL(tree, tree->root)) {
        TREE_UNLOCK;
        return NULL;
    }
    
    leaf = lookup_recursive(tree, tree->root, key);
    TREE_UNLOCK;
    return leaf;
}

void*
rb_tree_lookup_value(
    _In_ rb_tree_t* tree,
    _In_ void*      key)
{
    rb_leaf_t* leaf = rb_tree_lookup(tree, key);
    return (leaf != NULL) ? leaf->value : NULL;
}

static rb_leaf_t*
get_minimum_leaf(
	_In_ rb_tree_t* tree,
	_In_ rb_leaf_t* leaf)
{
	rb_leaf_t* i = leaf;
	while (!IS_ITEM_NIL(tree, i->left)) {
		i = i->left;
	}
	return i;
}

rb_leaf_t*
rb_tree_minimum(
	_In_ rb_tree_t* tree)
{
    rb_leaf_t* leaf;
    assert(tree != NULL);
    
    TREE_LOCK;
	if (IS_ITEM_NIL(tree, tree->root)) {
	    TREE_UNLOCK;
	    return NULL;
	}
	
	leaf = get_minimum_leaf(tree, tree->root);
	TREE_UNLOCK;
	return leaf;
}

static void
transplant_nodes(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* target,
    _In_ rb_leaf_t* with)
{
    if (IS_ITEM_NIL(tree, target->parent)) {
        tree->root = with;
    }
    else if (target == target->parent->left) {
        target->parent->left = with;
    }
    else {
        target->parent->right = with;
    }
    
    with->parent = target->parent;
}

static void
fixup_tree_after_delete(
    _In_ rb_tree_t* tree,
    _In_ rb_leaf_t* leaf)
{
    rb_leaf_t* i = leaf;
    while (i != tree->root && i->color == COLOR_BLACK) {
        if (i == i->parent->left) {
            rb_leaf_t* temp = i->parent->right;
            if (temp->color == COLOR_RED) {
                temp->color = COLOR_BLACK;
                i->parent->color = COLOR_RED;
                rotate_left(tree, i->parent);
                temp = i->parent->right;
            }
            
            if (temp->left->color == COLOR_BLACK && temp->right->color == COLOR_BLACK) {
                temp->color = COLOR_RED;
                i = i->parent;
                continue;
            }
            else if (temp->right->color == COLOR_BLACK) {
                temp->left->color = COLOR_BLACK;
                temp->color = COLOR_RED;
                rotate_right(tree, temp);
                temp = i->parent->right;
            }
            
            if (temp->right->color == COLOR_RED) {
                temp->color = i->parent->color;
                i->parent->color = COLOR_BLACK;
                temp->right->color = COLOR_BLACK;
                rotate_left(tree, i->parent);
                i = tree->root;
            }
        }
        else {
            rb_leaf_t* temp = i->parent->left;
            if (temp->color == COLOR_RED) {
                temp->color = COLOR_BLACK;
                i->parent->color = COLOR_RED;
                rotate_right(tree, i->parent);
                temp = i->parent->left;
            }
            
            if (temp->right->color == COLOR_BLACK && temp->left->color == COLOR_BLACK) {
                temp->color = COLOR_RED;
                i = i->parent;
                continue;
            }
            else if (temp->left->color == COLOR_BLACK) {
                temp->right->color = COLOR_BLACK;
                temp->color = COLOR_RED;
                rotate_left(tree, temp);
                temp = i->parent->left;
            }
            
            if (temp->left->color == COLOR_RED) {
                temp->color = i->parent->color;
                i->parent->color = COLOR_BLACK;
                temp->left->color = COLOR_BLACK;
                rotate_right(tree, i->parent);
                i = tree->root;
            }
        }
    }
    i->color = COLOR_BLACK;
}

rb_leaf_t*
rb_tree_remove(
    _In_ rb_tree_t* tree,
    _In_ void*      key)
{
    rb_leaf_t* leaf = rb_tree_lookup(tree, key);
    rb_leaf_t* temp;
    rb_leaf_t* i;
    int        temp_color;
    
    if (!leaf) {
        return NULL;
    }
    
    TREE_LOCK;
    temp = leaf;
    temp_color = temp->color;
    if (IS_ITEM_NIL(tree, leaf->left)) {
        i = leaf->right;
        transplant_nodes(tree, leaf, leaf->right);
    }
    else if (IS_ITEM_NIL(tree, leaf->right)) {
        i = leaf->left;
        transplant_nodes(tree, leaf, leaf->left);
    }
    else {
        temp = get_minimum_leaf(tree, leaf->right);
        temp_color = temp->color;
        i = temp->right;
        if (temp->parent == leaf) {
            i->parent = temp;
        }
        else {
            transplant_nodes(tree, temp, temp->right);
            temp->right = leaf->right;
            temp->right->parent = temp;
        }
        
        transplant_nodes(tree, leaf, temp);
        temp->left = leaf->left;
        temp->left->parent = temp;
        temp->color = leaf->color;
    }
    
    if (temp_color == COLOR_BLACK) {
        fixup_tree_after_delete(tree, i);
    }
    TREE_UNLOCK;
    return leaf;
}
