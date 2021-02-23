
#include "luautils.h"
#include "types.h"
#include "object.h"

struct ltreesitter_QueryCursor *ltreesitter_check_query_cursor(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
}

static int query_cursor_gc(lua_State *L) {
	struct ltreesitter_QueryCursor *c = ltreesitter_check_query_cursor(L, 1);
	ts_query_cursor_delete(c->query_cursor);
	return 0;
}

static const luaL_Reg query_cursor_methods[] = {
	{NULL, NULL}
};

static const luaL_Reg query_cursor_metamethods[] = {
	{"__gc", query_cursor_gc},
	{NULL, NULL}
};

void ltreesitter_create_query_cursor_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME, query_cursor_metamethods, query_cursor_methods);
}
