#ifndef LTREESITTER_TREE_CURSOR_H
#define LTREESITTER_TREE_CURSOR_H

#include "types.h"

// ( -- table )
void tree_cursor_init_metatable(lua_State *L);

// ( any -- any )
TSTreeCursor *tree_cursor_check(lua_State *L, int idx);

// ( -- tree_cursor )
TSTreeCursor *tree_cursor_push(lua_State *L, int parent_idx, TSNode n);

#endif
