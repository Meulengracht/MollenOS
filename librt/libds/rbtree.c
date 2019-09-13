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

#include <assert.h>
#include <ds/rbtree.h>
#include <string.h>

#define COLOR_BLACK 0
#define COLOR_RED   1

#define ITEM_NIL(Tree)          &Tree->NilItem
#define IS_ITEM_NIL(Tree, Item) (Item == ITEM_NIL(Tree))

void
RBTreeConstruct(
    _In_ RBTree_t* Tree,
    _In_ KeyType_t KeyType)
{
    assert(Tree != NULL);
    
    memset(Tree, 0, sizeof(RBTree_t));
    Tree->KeyType = KeyType;
    Tree->Root    = &Tree->NilItem;
}

static void
RotateLeft(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* TreeItem)
{
    if (!IS_ITEM_NIL(Tree, TreeItem->Parent)) {
        if (TreeItem == TreeItem->Parent->Left) {
            TreeItem->Parent->Left = TreeItem->Right;
        }
        else {
            TreeItem->Parent->Right = TreeItem->Right;
        }
        
        TreeItem->Right->Parent = TreeItem->Parent;
        TreeItem->Parent = TreeItem->Right;
        if (!IS_ITEM_NIL(Tree, TreeItem->Right->Left)) {
            TreeItem->Right->Left->Parent = TreeItem;
        }
        
        TreeItem->Right = TreeItem->Right->Left;
        TreeItem->Parent->Left = TreeItem;
    }
    else {
        RBTreeItem_t* Right = Tree->Root->Right;
        
        Tree->Root->Right   = Right->Left;
        Right->Left->Parent = Tree->Root;
        Tree->Root->Parent  = Right;
        Right->Left         = Tree->Root;
        Right->Parent       = ITEM_NIL(Tree);
        Tree->Root          = Right;
    }
}

static void
RotateRight(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* TreeItem)
{
    if (!IS_ITEM_NIL(Tree, TreeItem->Parent)) {
        if (TreeItem == TreeItem->Parent->Left) {
            TreeItem->Parent->Left = TreeItem->Left;
        }
        else {
            TreeItem->Parent->Right = TreeItem->Left;
        }
        
        TreeItem->Left->Parent = TreeItem->Parent;
        TreeItem->Parent = TreeItem->Left;
        if (!IS_ITEM_NIL(Tree, TreeItem->Left->Right)) {
            TreeItem->Left->Right->Parent = TreeItem;
        }
        
        TreeItem->Left          = TreeItem->Left->Right;
        TreeItem->Parent->Right = TreeItem;
    }
    else {
        RBTreeItem_t* Left = Tree->Root->Left;
        
        Tree->Root->Left    = Tree->Root->Left->Right;
        Left->Right->Parent = Tree->Root;
        Tree->Root->Parent  = Left;
        Left->Right         = Tree->Root;
        Left->Parent        = ITEM_NIL(Tree);
        Tree->Root          = Left;
    }
}

static void
FixupTree(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* TreeItem)
{
    RBTreeItem_t* Itr = TreeItem;
    
    while (Itr->Parent->Color == COLOR_RED) {
        RBTreeItem_t* Uncle;
        if (Itr->Parent == Itr->Parent->Parent->Left) {
            Uncle = Itr->Parent->Parent->Right;
            if (!IS_ITEM_NIL(Tree, Uncle) && Uncle->Color == COLOR_RED) {
                Itr->Parent->Color = COLOR_BLACK;
                Uncle->Color = COLOR_BLACK;
                Itr->Parent->Parent->Color = COLOR_RED;
                Itr = Itr->Parent->Parent;
                continue;
            }
            
            // Check if double rotation is required
            if (Itr == Itr->Parent->Right) {
                Itr = Itr->Parent;
                RotateLeft(Tree, Itr);
            }
            
            Itr->Parent->Color = COLOR_BLACK;
            Itr->Parent->Parent->Color = COLOR_RED;
            RotateRight(Tree, Itr->Parent->Parent);
        }
        else {
            Uncle = Itr->Parent->Parent->Left;
            if (!IS_ITEM_NIL(Tree, Uncle) && Uncle->Color == COLOR_RED) {
                Itr->Parent->Color = COLOR_BLACK;
                Uncle->Color = COLOR_BLACK;
                Itr->Parent->Parent->Color = COLOR_RED;
                Itr = Itr->Parent->Parent;
                continue;
            }
            
            // Check if double rotation is required
            if (Itr == Itr->Parent->Left) {
                Itr = Itr->Parent;
                RotateRight(Tree, Itr);
            }
            
            Itr->Parent->Color         = COLOR_BLACK;
            Itr->Parent->Parent->Color = COLOR_RED;
            RotateLeft(Tree, Itr->Parent->Parent);
        }
    }
    Tree->Root->Color = COLOR_BLACK;
}

OsStatus_t
RBTreeAppend(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* TreeItem)
{
    RBTreeItem_t* Itr;
    
    if (!Tree || !TreeItem) {
        return OsInvalidParameters;
    }
    
    TreeItem->Left  = ITEM_NIL(Tree);
    TreeItem->Right = ITEM_NIL(Tree);
    
    dslock(&Tree->SyncObject);
    if (IS_ITEM_NIL(Tree, Tree->Root)) {
        Tree->Root = TreeItem;
        
        TreeItem->Color  = COLOR_BLACK;
        TreeItem->Parent = ITEM_NIL(Tree);
    }
    else {
        Itr = Tree->Root;
        
        TreeItem->Color = COLOR_RED;
        while (1) {
            int Comparison = dssortkey(Tree->KeyType, TreeItem->Key, Itr->Key);
            if (Comparison == 1) {
                if (IS_ITEM_NIL(Tree, Itr->Left)) {
                    Itr->Left = TreeItem;
                    TreeItem->Parent = Itr;
                    break;
                }
                else {
                    Itr = Itr->Left;
                }
            }
            else if (Comparison == -1) {
                if (IS_ITEM_NIL(Tree, Itr->Right)) {
                    Itr->Right = TreeItem;
                    TreeItem->Parent = Itr;
                    break;
                }
                else {
                    Itr = Itr->Right;
                }
            }
            else {
                dsunlock(&Tree->SyncObject);
                return OsExists;
            }
        }
        FixupTree(Tree, TreeItem);
    }
    dsunlock(&Tree->SyncObject);
    return OsSuccess;
}

static RBTreeItem_t*
LookupRecursive(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* Item,
    _In_ DataKey_t     Key)
{
    int Comparison = dssortkey(Tree->KeyType, Item->Key, Key);
    if (!Comparison) {
        return Item;
    }
    
    if (Comparison == 1) {
        if (!IS_ITEM_NIL(Tree, Item->Left)) {
            return LookupRecursive(Tree, Item->Left, Key);
        }
    }
    else {
        if (!IS_ITEM_NIL(Tree, Item->Right)) {
            return LookupRecursive(Tree, Item->Right, Key);
        }
    }
    return NULL;
}

RBTreeItem_t*
RBTreeLookupKey(
    _In_ RBTree_t* Tree,
    _In_ DataKey_t Key)
{
    if (!Tree) {
        return NULL;
    }
    
    if (IS_ITEM_NIL(Tree, Tree->Root)) {
        return NULL;
    }
    
    return LookupRecursive(Tree, Tree->Root, Key);
}

static RBTreeItem_t*
GetMinimumItem(
	_In_ RBTree_t*     Tree,
	_In_ RBTreeItem_t* Item)
{
	RBTreeItem_t* Itr = Item;
	while (!IS_ITEM_NIL(Tree, Itr->Left)) {
		Itr = Itr->Left;
	}
	return Itr;
}

RBTreeItem_t*
RBTreeGetMinimum(
	_In_ RBTree_t* Tree)
{
    if (!Tree) {
        return NULL;
    }
    
	if (IS_ITEM_NIL(Tree, Tree->Root)) {
	    return NULL;
	}
	
	return GetMinimumItem(Tree, Tree->Root);
}

static void
TransplantNodes(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* Target,
    _In_ RBTreeItem_t* With)
{
    if (IS_ITEM_NIL(Tree, Target->Parent)) {
        Tree->Root = With;
    }
    else if (Target == Target->Parent->Left) {
        Target->Parent->Left = With;
    }
    else {
        Target->Parent->Right = With;
    }
    
    With->Parent = Target->Parent;
}

static void
FixupTreeAfterDelete(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* Item)
{
    RBTreeItem_t* Itr = Item;
    while (Itr != Tree->Root && Itr->Color == COLOR_BLACK) {
        if (Itr == Itr->Parent->Left) {
            RBTreeItem_t* Temp = Itr->Parent->Right;
            if (Temp->Color == COLOR_RED) {
                Temp->Color = COLOR_BLACK;
                Itr->Parent->Color = COLOR_RED;
                RotateLeft(Tree, Itr->Parent);
                Temp = Itr->Parent->Right;
            }
            
            if (Temp->Left->Color == COLOR_BLACK && Temp->Right->Color == COLOR_BLACK) {
                Temp->Color = COLOR_RED;
                Itr = Itr->Parent;
                continue;
            }
            else if (Temp->Right->Color == COLOR_BLACK) {
                Temp->Left->Color = COLOR_BLACK;
                Temp->Color = COLOR_RED;
                RotateRight(Tree, Temp);
                Temp = Itr->Parent->Right;
            }
            
            if (Temp->Right->Color == COLOR_RED) {
                Temp->Color = Itr->Parent->Color;
                Itr->Parent->Color = COLOR_BLACK;
                Temp->Right->Color = COLOR_BLACK;
                RotateLeft(Tree, Itr->Parent);
                Itr = Tree->Root;
            }
        }
        else {
            RBTreeItem_t* Temp = Itr->Parent->Left;
            if (Temp->Color == COLOR_RED) {
                Temp->Color = COLOR_BLACK;
                Itr->Parent->Color = COLOR_RED;
                RotateRight(Tree, Itr->Parent);
                Temp = Itr->Parent->Left;
            }
            
            if (Temp->Right->Color == COLOR_BLACK && Temp->Left->Color == COLOR_BLACK) {
                Temp->Color = COLOR_RED;
                Itr = Itr->Parent;
                continue;
            }
            else if (Temp->Left->Color == COLOR_BLACK) {
                Temp->Right->Color = COLOR_BLACK;
                Temp->Color = COLOR_RED;
                RotateLeft(Tree, Temp);
                Temp = Itr->Parent->Left;
            }
            
            if (Temp->Left->Color == COLOR_RED) {
                Temp->Color = Itr->Parent->Color;
                Itr->Parent->Color = COLOR_BLACK;
                Temp->Left->Color = COLOR_BLACK;
                RotateRight(Tree, Itr->Parent);
                Itr = Tree->Root;
            }
        }
    }
    Itr->Color = COLOR_BLACK;
}

RBTreeItem_t*
RBTreeRemove(
    _In_ RBTree_t* Tree,
    _In_ DataKey_t Key)
{
    RBTreeItem_t* Item = RBTreeLookupKey(Tree, Key);
    RBTreeItem_t* Temp;
    RBTreeItem_t* Itr;
    int           TempColor;
    
    if (!Item) {
        return NULL;
    }
    
    dslock(&Tree->SyncObject);
    Temp = Item;
    TempColor = Temp->Color;
    if (IS_ITEM_NIL(Tree, Item->Left)) {
        Itr = Item->Right;
        TransplantNodes(Tree, Item, Item->Right);
    }
    else if (IS_ITEM_NIL(Tree, Item->Right)) {
        Itr = Item->Left;
        TransplantNodes(Tree, Item, Item->Left);
    }
    else {
        Temp = GetMinimumItem(Tree, Item->Right);
        TempColor = Temp->Color;
        Itr = Temp->Right;
        if (Temp->Parent == Item) {
            Itr->Parent = Temp;
        }
        else {
            TransplantNodes(Tree, Temp, Temp->Right);
            Temp->Right = Item->Right;
            Temp->Right->Parent = Temp;
        }
        
        TransplantNodes(Tree, Item, Temp);
        Temp->Left = Item->Left;
        Temp->Left->Parent = Temp;
        Temp->Color = Item->Color;
    }
    
    if (TempColor == COLOR_BLACK) {
        FixupTreeAfterDelete(Tree, Itr);
    }
    dsunlock(&Tree->SyncObject);
    return Item;
}
