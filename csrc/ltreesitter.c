#include <lauxlib.h>
#include <lua.h>

#include "luautils.h"
#include "object.h"
#include <ltreesitter/module.h>
#include <ltreesitter/dynamiclib.h>
#include <ltreesitter/node.h>
#include <ltreesitter/parser.h>
#include <ltreesitter/query.h>
#include <ltreesitter/query_cursor.h>
#include <ltreesitter/tree.h>
#include <ltreesitter/tree_cursor.h>

// @teal-export version: string
static const char version_str[] = "0.0.6+dev";

static const luaL_Reg lib_funcs[] = {
	{"load", ltreesitter_load_parser},
	{"require", ltreesitter_require_parser},
	{"_reg", push_registry_table},
	{NULL, NULL},
};

LTREESITTER_EXPORT int luaopen_ltreesitter(lua_State *L) {
	ltreesitter_create_parser_metatable(L);
	ltreesitter_create_tree_metatable(L);
	ltreesitter_create_tree_cursor_metatable(L);
	ltreesitter_create_node_metatable(L);
	ltreesitter_create_query_metatable(L);
	ltreesitter_create_query_cursor_metatable(L);

	setup_registry_index(L);
	ltreesitter_setup_query_predicate_tables(L);
	setup_object_table(L);
	setup_parser_cache(L);

	create_libtable(L, lib_funcs);
	lua_pushstring(L, version_str);
	lua_setfield(L, -2, "version");

	return 1;
}
