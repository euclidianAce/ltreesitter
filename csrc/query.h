#ifndef LTREESITTER_QUERY_H
#define LTREESITTER_QUERY_H

#include "types.h"
#include <lua.h>
#include <tree_sitter/api.h>

// ( -- table )
void query_init_metatable(lua_State *L);

def_check_assert(TSQuery *, query, LTREESITTER_QUERY_METATABLE_NAME)

// ( -- query )
void query_push(lua_State *L, TSQuery *, int language_index);

void query_setup_predicate_tables(lua_State *L);

// returns true when there is no error
bool query_handle_error(
	lua_State *,
	TSQuery *,
	uint32_t err_offset,
	TSQueryError,
	char const *query_src,
	size_t query_src_len);

#endif
