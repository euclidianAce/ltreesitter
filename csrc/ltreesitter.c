
// #define LTREESITTER_DEBUG

#include <lauxlib.h>
#include <lua.h>

#include <ltreesitter/dynamiclib.h>
#include "luautils.h"
#include <ltreesitter/node.h>
#include "object.h"
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

#ifdef LTREESITTER_DEBUG
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
static void dump_stacktrace() {
	void *callstack[128];
	int frames = backtrace(callstack, 128);
	char **strs = backtrace_symbols(callstack, frames);
	printf("STACKTRACE:\n");
	for (int i = 0; i < frames; ++i) {
		printf("   %s\n", strs[i]);
	}
	free(strs);
	printf("DONE STACKTRACE\n");
	signal(SIGSEGV, NULL);
}
#endif

LUA_API int luaopen_ltreesitter(lua_State *L) {
#ifdef LTREESITTER_DEBUG
	signal(SIGSEGV, dump_stacktrace);
#endif
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
