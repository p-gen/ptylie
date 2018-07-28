////////////////////////////////////////////////////////////////////////////////
//                                                                            //
//      BinaryTrees.h                                                         //
//      -------------                                                         //
//                                                                            //
// Content: The headers of the BinaryTrees library.                           //
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

// BINARY_TREES LIBRARY ////////////////////////////////////////////////////////

#ifndef TREE_H

#define TREE_H

// GENERAL MACROS //////////////////////////////////////////////////////////

#ifndef YES
#define YES 1
#endif

#ifndef NO
#define NO 0
#endif

////////////////////////////////////////////////////////////////////////////

// RED BLACK MACROS:

#define RED 1
#define BLACK 0
#define IS_RED(p) (((p) != NULL) && ((p)->color == RED))
#define IS_BLACK(p) (((p) == NULL) || ((p)->color == BLACK))

// STRUCTS:

typedef struct rb_node
{
  void *           data;  // Generic pointer to the content (never NULL)
  struct rb_node * left;  // Left subtree  (NULL if empty)
  struct rb_node * right; // Right subtree (NULL if empty)
  char             color; // Either RED (= 1) or BLACK (= 0)
} rb_node;

typedef struct rb_tree
{
  struct rb_node * root;                   // Root node of the tree
  int (*comp)(const void *, const void *); // Comparing function
} rb_tree;

// CREATION & INSERTION:

rb_tree *
new_rb_tree(int (*comp)(const void *, const void *));

void *
rb_tree_insert(rb_tree * tree, void * data);

// SEARCH:

void *
rb_tree_search(const rb_tree * tree, const void * data);

// REMOVE:

void *
rb_tree_remove(rb_tree * tree, const void * data);

void
rb_tree_remove_all(rb_tree * tree, void (*free_data)(void *));

#endif
