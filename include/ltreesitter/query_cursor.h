#ifndef LTREESITTER_QUERY_CURSOR_H
#define LTREESITTER_QUERY_CURSOR_H

#include <lua.h>

void ltreesitter_create_query_cursor_metatable(lua_State *L);
ltreesitter_QueryCursor *ltreesitter_check_query_cursor(lua_State *L, int idx);

#endif
