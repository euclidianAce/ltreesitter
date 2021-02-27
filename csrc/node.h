#ifndef LTREESITTER_NODE_H
#define LTREESITTER_NODE_H

#include <tree_sitter/api.h>

void ltreesitter_create_node_metatable(lua_State *L);
struct ltreesitter_Node *ltreesitter_check_node(lua_State *L, int idx);

#include <stdio.h>
void push_node(lua_State *L, int parent, TSNode n, const TSLanguage *lang);

#endif
