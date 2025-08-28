#ifndef LTREESITTER_QUERY_CURSOR_H
#define LTREESITTER_QUERY_CURSOR_H

#include <lua.h>
#include "types.h"

// ( -- table )
void query_cursor_init_metatable(lua_State *L);

def_check_assert(TSQueryCursor *, query_cursor, LTREESITTER_QUERY_CURSOR_METATABLE_NAME)

#endif
