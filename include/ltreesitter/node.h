#ifndef LTREESITTER_NODE_H
#define LTREESITTER_NODE_H

#include "types.h"
#include <tree_sitter/api.h>

void ltreesitter_create_node_metatable(lua_State *L);
ltreesitter_Node *ltreesitter_check_node(lua_State *L, int idx);
void ltreesitter_push_node(lua_State *L, int rel_parent_idx, TSNode n);

#endif
