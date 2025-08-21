#ifndef LTREESITTER_QUERY_CURSOR_H
#define LTREESITTER_QUERY_CURSOR_H

#include <lua.h>
#include "types.h"

// ( -- table )
void query_cursor_init_metatable(lua_State *L);

// ( any -- any )
ltreesitter_QueryCursor *query_cursor_check(lua_State *L, int idx);

#endif
