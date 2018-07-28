////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//      BinaryTrees.c                                                         //
//      -------------                                                         //
//                                                                            //
// Content: The methods of the BinaryTrees library.                           //
// Author:  Carlos Luna-Mota <el.luna@gmail.com>                              //
// Date:    January 2018                                                      //
//                                                                            //
// This is free and unencumbered software released into the public domain.    //
//                                                                            //
// Anyone is free to copy, modify, publish, use, compile, sell, or            //
// distribute this software, either in source code form or as a compiled      //
// binary, for any purpose, commercial or non-commercial, and by any means.   //
//                                                                            //
// In jurisdictions that recognize copyright laws, the author or authors of   //
// this software dedicate any and all copyright interest in the software to   //
// the public domain. We make this dedication for the benefit of the public   //
// at large and to the detriment of our heirs and successors. We intend this  //
// dedication to be an overt act of relinquishment in perpetuity of all       //
// present and future rights to this software under copyright law.            //
//                                                                            //
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,            //
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF         //
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.     //
// IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR          //
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,      //
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR      //
// OTHER DEALINGS IN THE SOFTWARE.                                            //
//                                                                            //
// For more information, please refer to <http://unlicense.org>               //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

// LIBRARIES ///////////////////////////////////////////////////////////////////

#include <stdlib.h> // malloc, free
#include <stdio.h>  // fprintf, fflush, sprintf, stderr, stdout
#include <string.h> // strlen
#include "tree.h"   // BinaryTrees library headers

////////////////////////////////////////////////////////////////////////////////

// CREATION & INSERTION:

// Returns a pointer to a newly created rb_tree.
// You must provide a comparing function "comp" such that:
//  * comp(A,B) = 0  if and only if  A = B
//  * comp(A,B) > 0  if and only if  A > B
//  * comp(A,B) < 0  if and only if  A < B
//
// I recommend to limit the output of the "comp" function to {-1, 0, 1} as in:
//
//  int MyComp(const void *ptr1, const void *ptr2) {
//      MyData *d1 = (MyData *) ptr1;
//      MyData *d2 = (MyData *) ptr2;
//      if      (d1->key < d2->key) { return -1; }
//      else if (d1->key > d2->key) { return +1; }
//      else                        { return  0; }
//  }
//
// However, the code does not assume that so, if you are working with simple
// numeric keys, you can just use something like:
//
//  int MyComp(const void *ptr1, const void *ptr2) {
//      MyData *d1 = (MyData *) ptr1;
//      MyData *d2 = (MyData *) ptr2;
//      return (int) ((d1->key) - (d2->key));
//  }
//
rb_tree *
new_rb_tree(int (*comp)(const void *, const void *))
{

  // Allocate memory:
  rb_tree * tree = (rb_tree *)malloc(sizeof(rb_tree));
  if (tree == NULL)
  {
    fprintf(stderr, "ERROR: Unable to allocate memory for rb_tree\n");
  }

  // Initialize the empty tree:
  else
  {
    tree->root = NULL;
    tree->comp = comp;
  }

  return tree;
}

// Inserts data in tree.
//
// If a node of the tree compares "equal" to data it will get replaced and a
// pointer to the previously stored data will be returned (so you can free it),
// otherwise it will simply return a NULL pointer.
//
void *
rb_tree_insert(rb_tree * tree, void * data)
{

  rb_node * anchor   = NULL; // We need to store the last 4 levels:
  rb_node * granpa   = NULL; //
  rb_node * parent   = NULL; //            anchor
  rb_node * node     = NULL; //              |    <- comp_g
  void *    old_data = NULL; //            granpa
  int       comp_g   = 0;    //              |    <- comp_p
  int       comp_p   = 0;    //            parent
  int       comp_n   = 0;    //              |    <- comp_n
  int       comp     = 0;    //             node

  // Search for the correct place to insert data:
  node = tree->root;
  for (;;)
  {

    // If we reach a leaf we must insert "data" here:
    if (node == NULL)
    {

      // Create a new node:
      node = (rb_node *)malloc(sizeof(rb_node));
      if (node == NULL)
      {
        fprintf(stderr, "ERROR: Unable to allocate rb_node\n");
        break;
      }
      else
      {
        node->data  = data;
        node->left  = NULL;
        node->right = NULL;
        node->color = RED;
        comp        = 0;
      }

      // And attach it bellow "parent":
      if (parent == NULL)
      {
        tree->root = node;
      }
      else if (comp_n < 0)
      {
        parent->left = node;
      }
      else
      {
        parent->right = node;
      }

      // Otherwise "node" is an interior node:
    }
    else
    {

      // Compare "data" with "node->data":
      comp = (tree->comp)(data, node->data);

      // If the data is already there: Update and remember "old_data"
      if (comp == 0)
      {
        old_data   = node->data;
        node->data = data;
      }

      // If "node" has two RED children: Make a color flip
      if (IS_RED(node->left) && IS_RED(node->right))
      {
        node->color        = RED;
        node->left->color  = BLACK;
        node->right->color = BLACK;
      }
    }

    // Repair any violation of the RED property:
    if (IS_RED(node) && IS_RED(parent))
    {

      // Case 1: Single "granpa-parent" left rotation
      if (comp_p > 0 && comp_n > 0)
      {

        granpa->right = parent->left;
        granpa->color = RED;
        parent->left  = granpa;
        parent->color = BLACK;

        if (anchor == NULL)
        {
          tree->root = parent;
        }
        else if (comp_g < 0)
        {
          anchor->left = parent;
        }
        else if (comp_g > 0)
        {
          anchor->right = parent;
        }
        granpa = anchor;
        comp_p = comp_g;

        // Case 2: Single "granpa-parent" right rotation
      }
      else if (comp_p < 0 && comp_n < 0)
      {

        granpa->left  = parent->right;
        granpa->color = RED;
        parent->right = granpa;
        parent->color = BLACK;

        if (anchor == NULL)
        {
          tree->root = parent;
        }
        else if (comp_g < 0)
        {
          anchor->left = parent;
        }
        else if (comp_g > 0)
        {
          anchor->right = parent;
        }
        granpa = anchor;
        comp_p = comp_g;

        // Case 3: Double "granpa-parent-node" rotation
      }
      else
      {

        // Case 3.1: Left-Right
        if (comp_n < 0)
        {
          granpa->right = node->left;
          granpa->color = RED;
          parent->left  = node->right;
          node->left    = granpa;
          node->right   = parent;
          node->color   = BLACK;
          if (comp > 0)
          {
            granpa = parent;
          }
          parent = node;
          node   = granpa;
          if (comp > 0)
          {
            comp_n *= -1;
          }
          if (comp < 0)
          {
            comp_n = -comp_p;
          }

          // Case 3.2: Right-Left
        }
        else
        {
          granpa->left  = node->right;
          granpa->color = RED;
          parent->right = node->left;
          node->right   = granpa;
          node->left    = parent;
          node->color   = BLACK;
          if (comp < 0)
          {
            granpa = parent;
          }
          parent = node;
          node   = granpa;
          if (comp < 0)
          {
            comp_n *= -1;
          }
          if (comp > 0)
          {
            comp_n = -comp_p;
          }
        }

        if (anchor == NULL)
        {
          tree->root = parent;
        }
        else if (comp_g < 0)
        {
          anchor->left = parent;
        }
        else if (comp_g > 0)
        {
          anchor->right = parent;
        }
        granpa = anchor;
        comp_p = comp_g;
        comp *= -1;
      }
    }

    // Advance one step:
    anchor = granpa;
    granpa = parent;
    parent = node;
    if (comp < 0)
    {
      node = node->left;
    } // Data is smaller
    else if (comp > 0)
    {
      node = node->right;
    } // Data is bigger
    else
    {
      break;
    } // We are done!

    // And remember were you come from:
    comp_g = comp_p;
    comp_p = comp_n;
    comp_n = comp;
  }

  // Before leaving: Make sure that the root is BLACK!
  if (tree->root != NULL)
  {
    tree->root->color = BLACK;
  }

  // And return old_data (which will be NULL unless data was already here)
  return old_data;
}

// SEARCH:

// Finds a node that compares "equal" to data. Returns NULL if not found.
//
void *
rb_tree_search(const rb_tree * tree, const void * data)
{

  rb_node * node;
  int       comp;

  // Search:
  node = tree->root;
  while (node != NULL)
  {
    comp = (tree->comp)(data, node->data); // compare data
    if (comp < 0)
    {
      node = node->left;
    } // data is smaller
    else if (comp > 0)
    {
      node = node->right;
    } // data is bigger
    else
    {
      return node->data;
    } // found!
  }

  // Not found:
  return NULL;
}

// REMOVE:

// Removes a node of tree that compares "equal" to data and returns a pointer
// to the previously stored data (so you can free it).
// If such a node is not found, it returns a NULL pointer.
//
// Uses the trick of swapping node->data and successor->data pointers and then
// removes successor. This is safe because the final user has no acces to any
// tree node (only "tree" and "data" pointers are used in all interfaces) and
// no traverser is coded either (so no "internal state" is stored in them).
//
void *
rb_tree_remove(rb_tree * tree, const void * data)
{

  rb_node * granpa   = NULL; // We need to store the last 3 levels //
  rb_node * parent   = NULL; //                                    //
  rb_node * sister   = NULL; //            granpa                  //
  rb_node * node     = NULL; //              |                     //
  rb_node * old_node = NULL; //            parent                  //
  void *    old_data = NULL; //            /    \   <- comp_n      //
  int       comp_n   = 0;    //        sister  node                //
  int       comp     = 0;    //                / \  <- comp        //

  // Initialize the search at the root node:
  node = tree->root;
  if (node == NULL)
  {
    return NULL;
  }

  // Look for a leaf:
  while (node != NULL)
  {

    // At this point node is BLACK, if sister exists is BLACK and if parent
    // exists is RED. We want to paint node RED and repair any violation.

    // Case 1: Node has two BLACK children
    if (IS_BLACK(node->left) && IS_BLACK(node->right))
    {

      // Easy case: the node is the root node
      if (parent == NULL)
      {
        node->color = RED;
      }

      // General case:
      else
      {

        // Case 1.0: Node has no sister
        if (sister == NULL)
        {
          node->color   = RED;
          parent->color = BLACK;

          // Case 1.1: Sister has 2 BLACK children
        }
        else if (IS_BLACK(sister->left) && IS_BLACK(sister->right))
        {
          node->color   = RED;
          sister->color = RED;
          parent->color = BLACK;

          // Case 1.2: Sister has at least 1 RED children
        }
        else
        {

          // If sister->left is RED:
          if (IS_RED(sister->left))
          {

            // If sister == parent->right: Double rotation
            if (comp < 0)
            {

              if (granpa == NULL)
              {
                tree->root = sister->left;
              }
              else if (comp_n < 0)
              {
                granpa->left = sister->left;
              }
              else
              {
                granpa->right = sister->left;
              }
              granpa = sister->left;

              parent->right = granpa->left;
              granpa->left  = parent;

              sister->left  = granpa->right;
              granpa->right = sister;
              sister        = parent->right;

              node->color   = RED;
              parent->color = BLACK;
            }

            // If sister == parent->left: Single rotation
            else
            {

              if (granpa == NULL)
              {
                tree->root = sister;
              }
              else if (comp_n < 0)
              {
                granpa->left = sister;
              }
              else
              {
                granpa->right = sister;
              }
              granpa = sister;

              parent->left  = granpa->right;
              granpa->right = parent;
              sister        = parent->left;

              node->color         = RED;
              granpa->color       = RED;
              parent->color       = BLACK;
              granpa->left->color = BLACK;
            }
          }

          // If sister->right is RED:
          else
          {

            // If sister == parent->left: Double rotation
            if (comp > 0)
            {

              if (granpa == NULL)
              {
                tree->root = sister->right;
              }
              else if (comp_n < 0)
              {
                granpa->left = sister->right;
              }
              else
              {
                granpa->right = sister->right;
              }
              granpa = sister->right;

              parent->left  = granpa->right;
              granpa->right = parent;

              sister->right = granpa->left;
              granpa->left  = sister;
              sister        = parent->left;

              node->color   = RED;
              parent->color = BLACK;
            }

            // If sister == parent->right: Single rotation
            else
            {

              if (granpa == NULL)
              {
                tree->root = sister;
              }
              else if (comp_n < 0)
              {
                granpa->left = sister;
              }
              else
              {
                granpa->right = sister;
              }
              granpa = sister;

              parent->right = granpa->left;
              granpa->left  = parent;
              sister        = parent->right;

              node->color          = RED;
              granpa->color        = RED;
              parent->color        = BLACK;
              granpa->right->color = BLACK;
            }
          }
        }
      }
    }

    // We compare the data now because we need the information for "Case 2"

    // Compare data unless you already know where to go:
    comp_n = comp;
    comp   = (old_data == NULL) ? (tree->comp)(data, node->data) : (-1);

    // If we have found the node to remove: Remember it!
    if (comp == 0)
    {
      old_data = node->data;
      old_node = node;
      comp     = +1; // ...and search the successor
    }

    // Case 2: Node has at least one RED children
    if (IS_RED(node->left) || IS_RED(node->right))
    {

      // Again node is BLACK, if sister exists is BLACK and if parent
      // exists is RED. We paint node RED and repair any violation.

      // Case 2.1: We are moving to the RED node
      if ((comp < 0 && IS_RED(node->left)) || (comp > 0 && IS_RED(node->right)))
      {

        // Move and compare again for free!
        granpa = parent;
        parent = node;
        if (comp < 0)
        {
          node   = parent->left;
          sister = parent->right;
        }
        else if (comp > 0)
        {
          node   = parent->right;
          sister = parent->left;
        }
        comp_n = comp;
        comp   = (old_data == NULL) ? (tree->comp)(data, node->data) : (-1);
        if (comp == 0)
        {
          old_data = node->data;
          old_node = node;
          comp     = +1;
        }
      }

      // Case 2.2: We are moving to the BLACK node
      else
      {
        // If we are moving to the left: Single left-rotation
        if (comp < 0)
        {
          if (parent == NULL)
          {
            tree->root = node->right;
          }
          else if (comp_n < 0)
          {
            parent->left = node->right;
          }
          else
          {
            parent->right = node->right;
          }
          granpa       = parent;
          parent       = node->right;
          sister       = parent->right;
          node->right  = parent->left;
          parent->left = node;

          node->color   = RED;
          parent->color = BLACK;

          comp_n = -1;
        }

        // If we are moving to the right: Single right-rotation
        else
        {
          if (parent == NULL)
          {
            tree->root = node->left;
          }
          else if (comp_n < 0)
          {
            parent->left = node->left;
          }
          else
          {
            parent->right = node->left;
          }
          granpa        = parent;
          parent        = node->left;
          sister        = parent->left;
          node->left    = parent->right;
          parent->right = node;

          node->color   = RED;
          parent->color = BLACK;

          comp_n = 1;
        }
      }
    }

    // ...and finally move!
    granpa = parent;
    parent = node;
    if (comp < 0)
    {
      node   = parent->left;
      sister = parent->right;
    }
    else if (comp > 0)
    {
      node   = parent->right;
      sister = parent->left;
    }
  }

  // Erase "parent", which should be RED:
  if (old_node != NULL)
  {
    old_node->data = parent->data;
    if (granpa == NULL)
    {
      tree->root = parent->right;
    }
    else if (granpa->left == parent)
    {
      granpa->left = parent->right;
    }
    else
    {
      granpa->right = parent->right;
    }
    free(parent);
  }

  // Before leaving: Make sure that the root is BLACK!
  if (tree->root != NULL)
  {
    tree->root->color = BLACK;
  }

  // And return old_data (which will be NULL unless data was already here)
  return old_data;
}

// Removes all the elements from the tree in linear time. This is faster than
// calling "tree_remove_min(tree)" until "tree_is_empty(tree)" returns "YES".
//
// If you provide a "free_data" function it will be used to free the "data"
// inside each node. If "free_data" is NULL, no "data" will be freed. Use this
// later option when "data" is shared between several data structures but be
// aware that this may cause some memory leaks if you are not carefull.
//
// Most of the time, you can use just "free" as "free_data" function.
// However if your "data" struct contains dinamically allocated data
// you may need to provide a more complex "free_data" function like:
//
//  void free_data(void *ptr) {
//      MyData *data = (MyData *) ptr;
//      free(data->some_array);
//      free(data->some_other_array);
//      free(data);
//  }
//
void
rb_tree_remove_all(rb_tree * tree, void (*free_data)(void *))
{

  rb_node * root;
  rb_node * left;
  rb_node * right;

  // Initialize:
  root       = tree->root;
  tree->root = NULL;

  // While the tree is not empty:
  while (root != NULL)
  {

    // Unravel the tree: Rotate right "root" & "left"
    if (root->left != NULL)
    {
      left        = root->left;
      right       = left->right;
      left->right = root;
      root->left  = right;
      root        = left;

      // Erase the current "root" node:
    }
    else
    {
      right = root->right;
      if (free_data != NULL)
      {
        free_data(root->data);
      }
      free(root);
      root = right;
    }
  }
}
