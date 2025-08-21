#ifndef LTREESITTER_TREE_H
#define LTREESITTER_TREE_H

#include "types.h"

// ( -- table )
void tree_init_metatable(lua_State *L);

// ( any -- any )
ltreesitter_Tree *tree_check(lua_State *L, int idx, char const *msg);

// ( any -- any )
ltreesitter_Tree *tree_check_assert(lua_State *L, int idx);

// ( -- tree )
// this function copies `src`
void tree_push(
	lua_State *,
	TSTree *,
	size_t src_len,
	char const *src);

// ( [reader_function_index]=function | -- tree )
void tree_push_with_reader(
	lua_State *,
	TSTree *,
	int reader_function_index);

#endif
