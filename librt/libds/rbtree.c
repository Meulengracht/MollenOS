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
    if (!IS_ITEM_NIL(RBTree, RBTreeItem->Parent)) {
        if (RBTreeItem == RBTreeItem->Parent->Left) {
            RBTreeItem->Parent->Left = RBTreeItem->Right;
        }
        else {
            RBTreeItem->Parent->Right = RBTreeItem->Right;
        }
        
        RBTreeItem->Right->Parent = RBTreeItem->Parent;
        RBTreeItem->Parent = RBTreeItem->Right;
        if (!IS_ITEM_NIL(RBTree, RBTreeItem->Right->Left)) {
            RBTreeItem->Right->Left->Parent = RBTreeItem;
        }
        
        RBTreeItem->Right = RBTreeItem->Right->Left;
        RBTreeItem->Parent->Left = RBTreeItem;
    }
    else {
        RBTreeItem_t* Right = RBTree->Root->Right;
        
        RBTree->Root->Right  = Right->Left;
        Right->Left->Parent  = RBTree->Root;
        RBTree->Root->Parent = Right;
        Right->Left          = RBTree->Root;
        Right->Parent        = ITEM_NIL(RBTree);
        RBTree->Root         = Right;
    }
}

static void
RotateRight(
    _In_ RBTree_t*     Tree,
    _In_ RBTreeItem_t* TreeItem)
{
    if (!IS_ITEM_NIL(RBTree, RBTreeItem->Parent)) {
        if (RBTreeItem == RBTreeItem->Parent->Left) {
            RBTreeItem->Parent->Left = RBTreeItem->Left;
        }
        else {
            RBTreeItem->Parent->Right = RBTreeItem->Left;
        }
        
        RBTreeItem->Left->Parent = RBTreeItem->Parent;
        RBTreeItem->Parent = RBTreeItem->Left;
        if (!IS_ITEM_NIL(RBTree, RBTreeItem->Left->Right)) {
            RBTreeItem->Left->Right->Parent = RBTreeItem;
        }
        
        RBTreeItem->Left = RBTreeItem->Left->Right;
        RBTreeItem->Parent->Right = RBTreeItem;
    }
    else {
        RBTreeItem_t* Left = RBTree->Root->Left;
        
        RBTree->Root->Left   = RBTree->Root->Left->Right;
        Left->Right->Parent  = RBTree->Root;
        RBTree->Root->Parent = Left;
        Left->Right          = RBTree->Root;
        Left->Parent         = ITEM_NIL(RBTree);
        RBTree->Root         = Left;
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
            if (!IS_ITEM_NIL(RBTree, Uncle) && Uncle->Color == COLOR_RED) {
                Itr->Parent->Color = COLOR_BLACK;
                Uncle->Color = COLOR_BLACK;
                Itr->Parent->Parent->Color = COLOR_RED;
                Itr = Itr->Parent->Parent;
                continue;
            }
            
            // Check if double rotation is required
            if (Itr == Itr->Parent->Right) {
                Itr = Itr->Parent;
                RotateLeft(RBTreeItem, Itr);
            }
            
            Itr->Parent->Color = COLOR_BLACK;
            Itr->Parent->Parent->Color = COLOR_RED;
            RotateRight(RBTree, Itr->Parent->Parent);
        }
        else {
            Uncle = Itr->Parent->Parent->Left;
            if (!IS_ITEM_NIL(RBTree, Uncle) && Uncle->Color == COLOR_RED) {
                Itr->Parent->Color = COLOR_BLACK;
                Uncle->Color = COLOR_BLACK;
                Itr->Parent->Parent->Color = COLOR_RED;
                Itr = Itr->Parent->Parent;
                continue;
            }
            
            // Check if double rotation is required
            if (Itr == Itr->Parent->Left) {
                Itr = Itr->Parent;
                RotateRight(RBTreeItem, Itr);
            }
            
            Itr->Parent->Color = COLOR_BLACK;
            Itr->Parent->Parent->Color = COLOR_RED;
            RotateLeft(RBTree, Itr->Parent->Parent);
        }
    }
    RBTree->Root->Color = COLOR_BLACK;
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
    
    TreeItem->Left  = ITEM_NIL(RBTree);
    TreeItem->Right = ITEM_NIL(RBTree);
    
    dslock(&Tree->SyncObject);
    if (IS_ITEM_NIL(Tree, Tree->Root)) {
        Tree->Root = TreeItem;
        
        TreeItem->Color  = COLOR_BLACK;
        TreeItem->Parent = ITEM_NIL(RBTree);
    }
    else {
        Itr = RBTree->Root;
        
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
    if (!RBTree) {
        return NULL;
    }
    
    if (IS_ITEM_NIL(RBTree, Tree->Root)) {
        return NULL;
    }
    
    return LookupRecursive(Tree, Tree->Root, Key);
}

RBTreeItem_t*
GetTreeMinimum(
	_In_ RBTree_t*     Tree,
	_In_ RBTreeItem_t* Item)
{
	RBTreeItem_t* Itr = Item;
	while (!IS_ITEM_NIL(Tree, Itr->Left)) {
		Itr = Itr->Left;
	}
	return Itr;
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
    while (Itr != Tree->Root && Itr->Color == BLACK) {
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
        Temp = GetTreeMinimum(Tree, Item->Right);
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
