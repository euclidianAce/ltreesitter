#ifndef LTREESITTER_TREE_CURSOR_H
#define LTREESITTER_TREE_CURSOR_H

#include "types.h"

ltreesitter_TreeCursor *ltreesitter_check_tree_cursor(lua_State *L, int idx);
ltreesitter_TreeCursor *ltreesitter_push_tree_cursor(lua_State *L, int parent_idx, TSNode n);
void ltreesitter_create_tree_cursor_metatable(lua_State *L);

#endif
