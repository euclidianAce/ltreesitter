#ifndef LTREESITTER_QUERY_H
#define LTREESITTER_QUERY_H

#include "types.h"
#include <lua.h>
#include <tree_sitter/api.h>

void ltreesitter_create_query_metatable(lua_State *L);
ltreesitter_Query *ltreesitter_check_query(lua_State *L, int idx);
void push_query(lua_State *L, const TSLanguage *const lang, const char *const src, const size_t src_len, TSQuery *const q, int parent_idx);
void setup_predicate_tables(lua_State *L);
void handle_query_error(lua_State *L, TSQuery *q, uint32_t err_offset, TSQueryError err_type, const char *query_src);

#endif
