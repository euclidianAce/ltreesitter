#ifndef LTREESITTER_TREE_CURSOR_H
#define LTREESITTER_TREE_CURSOR_H

#include "types.h"

// ( -- table )
void tree_cursor_init_metatable(lua_State *L);

def_check_assert(TSTreeCursor, tree_cursor, LTREESITTER_TREE_CURSOR_METATABLE_NAME)

// ( -- tree_cursor )
TSTreeCursor *tree_cursor_push(lua_State *L, int parent_idx, TSNode n);

#endif
