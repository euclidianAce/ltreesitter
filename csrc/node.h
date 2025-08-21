#ifndef LTREESITTER_NODE_H
#define LTREESITTER_NODE_H

#include "types.h"
#include <tree_sitter/api.h>

// ( -- table )
void node_init_metatable(lua_State *L);

// ( any -- any )
TSNode *node_check(lua_State *L, int idx);

// ( -- Node )
void node_push(lua_State *L, int tree_idx, TSNode n);

// ( Node -- Node )
MaybeOwnedString node_get_source(lua_State *);

#endif
