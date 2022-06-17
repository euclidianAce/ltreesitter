#ifndef LTREESITTER_TREE_H
#define LTREESITTER_TREE_H

#include "types.h"

void ltreesitter_create_tree_metatable(lua_State *L);
ltreesitter_Tree *ltreesitter_check_tree(lua_State *L, int idx, const char *msg);
ltreesitter_Tree *ltreesitter_check_tree_arg(lua_State *L, int idx);

// the created ltreesitter_Tree takes ownership of the `src`
void ltreesitter_push_tree(
	lua_State *,
	TSTree *,
	size_t src_len,
	const char *src
);

#endif
