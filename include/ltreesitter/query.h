#ifndef LTREESITTER_QUERY_H
#define LTREESITTER_QUERY_H

#include "types.h"
#include <lua.h>
#include <tree_sitter/api.h>

void ltreesitter_create_query_metatable(lua_State *L);
ltreesitter_Query *ltreesitter_check_query(lua_State *L, int idx);
void ltreesitter_push_query(lua_State *L, const TSLanguage *, const char *src, const size_t src_len, TSQuery *, int parent_idx);
void ltreesitter_setup_query_predicate_tables(lua_State *L);
bool ltreesitter_handle_query_error(
	lua_State *,
	TSQuery *,
	uint32_t err_offset,
	TSQueryError,
	const char *query_src,
	size_t query_src_len);

#endif
