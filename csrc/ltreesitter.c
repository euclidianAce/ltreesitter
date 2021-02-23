
#include <lua.h>
#include <lauxlib.h>

#include "dynamiclib.h"
#include "luautils.h"
#include "node.h"
#include "object.h"
#include "parser.h"
#include "tree.h"
#include "tree_cursor.h"
#include "query.h"
#include "query_cursor.h"

// @teal-export version: string
static const char version_str[] = "0.0.6+dev";

static const luaL_Reg lib_funcs[] = {
	{"load", ltreesitter_load_parser},
	{"require", ltreesitter_require_parser},
	{"_reg", push_registry_table},
	{NULL, NULL},
};

LUA_API int luaopen_ltreesitter(lua_State *L) {
	ltreesitter_create_parser_metatable(L);
	ltreesitter_create_tree_metatable(L);
	ltreesitter_create_tree_cursor_metatable(L);
	ltreesitter_create_node_metatable(L);
	ltreesitter_create_query_metatable(L);
	ltreesitter_create_query_cursor_metatable(L);

	setup_registry_index(L);
	setup_predicate_tables(L);
	setup_object_table(L);
	setup_parser_cache(L);

	create_libtable(L, lib_funcs);
	lua_pushstring(L, version_str);
	lua_setfield(L, -2, "version");

	return 1;
}
