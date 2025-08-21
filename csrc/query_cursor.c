#include "query_cursor.h"
#include "luautils.h"
#include "object.h"
#include "types.h"

ltreesitter_QueryCursor *query_cursor_check(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
}

static int query_cursor_gc(lua_State *L) {
	ltreesitter_QueryCursor *c = query_cursor_check(L, 1);
	ts_query_cursor_delete(c->query_cursor);
	return 0;
}

static const luaL_Reg query_cursor_methods[] = {
	{NULL, NULL}};

static const luaL_Reg query_cursor_metamethods[] = {
	{"__gc", query_cursor_gc},
	{NULL, NULL}};

void query_cursor_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME, query_cursor_metamethods, query_cursor_methods);
}
