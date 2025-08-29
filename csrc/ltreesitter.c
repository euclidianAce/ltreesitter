#include <lauxlib.h>
#include <lua.h>

#include "luautils.h"
#include "object.h"
#include "parser.h"
#include "tree.h"
#include "query.h"
#include "node.h"
#include "query_cursor.h"
#include "tree_cursor.h"
#include "language.h"

// @teal-export version: string
static const char version_str[] = "0.2.0+dev";

static const luaL_Reg lib_funcs[] = {
	{"_reg", push_registry_table},

	{"load", language_load},
	{"require", language_require},

	{NULL, NULL},
};

#ifdef _WIN32
__declspec (dllexport)
#else
__attribute__ ((visibility("default")))
#endif
int luaopen_ltreesitter(lua_State *L);

int luaopen_ltreesitter(lua_State *L) {
	tree_init_metatable(L);
	node_init_metatable(L);
	query_init_metatable(L);
	parser_init_metatable(L);
	tree_cursor_init_metatable(L);
	query_cursor_init_metatable(L);
	source_text_init_metatable(L);
	language_init_metatable(L);
	dynlib_init_metatable(L);

	setup_registry_index(L);
	setup_object_table(L);
	setup_dynlib_cache(L);

	query_setup_predicate_tables(L);

	create_libtable(L, lib_funcs);
	lua_pushstring(L, version_str);
	lua_setfield(L, -2, "version");

	// @teal-export TREE_SITTER_LANGUAGE_VERSION: integer
	lua_pushinteger(L, TREE_SITTER_LANGUAGE_VERSION);
	lua_setfield(L, -2, "TREE_SITTER_LANGUAGE_VERSION");

	// @teal-export TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION: integer
	lua_pushinteger(L, TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
	lua_setfield(L, -2, "TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION");

	// @teal-export tree_sitter_version: string
	lua_pushstring(L, "0.25.8");
	lua_setfield(L, -2, "tree_sitter_version");

	return 1;
}
