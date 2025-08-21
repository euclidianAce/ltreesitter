#ifndef LTREESITTER_NODE_H
#define LTREESITTER_NODE_H

#include "types.h"
#include <tree_sitter/api.h>

// ( -- table )
void node_init_metatable(lua_State *L);

def_check_assert(TSNode, node, LTREESITTER_NODE_METATABLE_NAME)

// ( -- Node )
void node_push(lua_State *L, int tree_idx, TSNode n);

// ( Node -- Node )
MaybeOwnedString node_get_source(lua_State *);

// ( [node_idx]=Node | -- Tree )
ltreesitter_Tree *node_push_tree(lua_State *L, int node_idx);

#endif
