#include <lauxlib.h>
#include <lua.h>

#include "language.h"
#include "luautils.h"
#include "node.h"
#include "object.h"
#include "parser.h"
#include "query.h"
#include "query_cursor.h"
#include "tree.h"
#include "tree_cursor.h"

// @teal-export version: string [[The version of ltreesitter]]
static const char version_str[] = "0.3.0+dev";

static const luaL_Reg lib_funcs[] = {
	{"_reg", push_registry_table},

	{"load", language_load},
	{"require", language_require},

	{NULL, NULL},
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
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
	language_setup_registry_table(L);

	query_setup_predicate_tables(L);

	create_libtable(L, lib_funcs);
	lua_pushstring(L, version_str);
	lua_setfield(L, -2, "version");

	// @teal-export TREE_SITTER_LANGUAGE_VERSION: integer [[The current language ABI version supported by the used version of tree-sitter]]
	lua_pushinteger(L, TREE_SITTER_LANGUAGE_VERSION);
	lua_setfield(L, -2, "TREE_SITTER_LANGUAGE_VERSION");

	// @teal-export TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION: integer [[The minimum language ABI version supported by the used version of tree-sitter]]
	lua_pushinteger(L, TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION);
	lua_setfield(L, -2, "TREE_SITTER_MIN_COMPATIBLE_LANGUAGE_VERSION");

	// @teal-export tree_sitter_version: string [[The version of the tree-sitter library ltreesitter was built with]]
	lua_pushstring(L, "0.26.12");
	lua_setfield(L, -2, "tree_sitter_version");

	return 1;
}
