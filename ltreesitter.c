
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dlfcn.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <tree_sitter/api.h>

#define LUA_TSPARSER_METATABLE   "ltreesitter.TSParser"
#define LUA_TSTREE_METATABLE     "ltreesitter.TSTree"
#define LUA_TSNODE_METATABLE     "ltreesitter.TSNode"

struct LuaTSParser {
	const TSLanguage *lang;
	void *dlhandle;
	TSParser *parser;
};

struct LuaTSTree {
	const TSLanguage *lang;
	TSTree *t;
};
struct LuaTSNode {
	const TSLanguage *lang;
	TSNode n;
};
struct LuaTSInputEdit { TSInputEdit *e; };

/* {{{ Utility */
#define GET_NODE(name, idx) TSNode name = ((struct LuaTSNode *)luaL_checkudata(L, (idx), LUA_TSNODE_METATABLE))->n
#define GET_LUA_NODE(name, idx) struct LuaTSNode *const name = luaL_checkudata(L, (idx), LUA_TSNODE_METATABLE)

#define GET_TREE(name, idx) TSTree *const name = ((struct LuaTSTree *)luaL_checkudata(L, (idx), LUA_TSTREE_METATABLE))->t
#define GET_LUA_TREE(name, idx) struct LuaTSTree *const name = luaL_checkudata(L, (idx), LUA_TSTREE_METATABLE)

#define GET_PARSER(name, idx) TSParser *const name = ((struct LuaTSParser *)luaL_checkudata(L, (idx), LUA_TSPARSER_METATABLE))->parser
#define GET_LUA_PARSER(name, idx) struct LuaTSParser *const name = luaL_checkudata(L, (idx), LUA_TSPARSER_METATABLE)
void create_metatable(
	lua_State *L,
	const char *name,
	const luaL_Reg metamethods[],
	const luaL_Reg index[]
) {
	luaL_newmetatable(L, name); // metatable
	luaL_setfuncs(L, metamethods, 0); // metatable
	lua_newtable(L); // metatable, table
	luaL_setfuncs(L, index, 0); // metatable, table
	lua_setfield(L, -2, "__index"); // metatable
}
/* }}}*/
/* {{{ Parser Object */
//@teal function(parser_so_file_name: string, language_name: string): TSParser
int lua_load_parser(lua_State *L) {
	const char *lang_name = luaL_checkstring(L, -1);
	const char *parser_file = luaL_checkstring(L, -2);

	void *handle = dlopen(parser_file, RTLD_GLOBAL | RTLD_NOW); // TODO: are these the necessary flags?
	if (!handle) {
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to dlopen handle: %s", dlerror());
		return 2;
	}
	char buf[128];
	if (snprintf(buf, 128-12, "tree_sitter_%s", lang_name) == 0) {
		dlclose(handle);
		lua_pushnil(L);
		lua_pushstring(L, "Unable to copy language name into buffer");
		return 2;
	}
	TSLanguage *(*tree_sitter_lang)(void);
	
	tree_sitter_lang = (TSLanguage *(*)(void)) dlsym(handle, buf);
	if (!tree_sitter_lang) {
		dlclose(handle);
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to find symbol %s in %s", buf, parser_file);
		return 2;
	}

	TSParser *parser = ts_parser_new();
	const TSLanguage *lang = tree_sitter_lang();
	ts_parser_set_language(parser, lang);

	struct LuaTSParser *const p = lua_newuserdata(L, sizeof(struct LuaTSParser));
	p->dlhandle = handle;
	p->lang = lang;
	p->parser = parser;

	luaL_setmetatable(L, LUA_TSPARSER_METATABLE);
	return 1;
}

int lua_close_parser(lua_State *L) {
	struct LuaTSParser *lp = luaL_checkudata(L, 1, LUA_TSPARSER_METATABLE);
	ts_parser_delete(lp->parser);
	dlclose(lp->dlhandle);
	return 0;
}

// function(TSParser, string): TSTree
int lua_parser_parse_string(lua_State *L) {
	GET_LUA_PARSER(p, 1);
	const char *str = luaL_checkstring(L, 2);
	TSTree *tree = ts_parser_parse_string(
		p->parser,
		NULL,
		str,
		strlen(str)
	);
	struct LuaTSTree *t = lua_newuserdata(L, sizeof(struct LuaTSTree));
	t->t = tree;
	t->lang = p->lang;
	luaL_setmetatable(L, LUA_TSTREE_METATABLE);
	return 1;
}

int lua_parser_set_timeout(lua_State *L) {
	GET_PARSER(p, 1);
	const lua_Number n = luaL_checknumber(L, 2);
	luaL_argcheck(L, n >= 0, 2, "expected non-negative number");
	ts_parser_set_timeout_micros(p, (uint64_t)n);
	return 0;
}

int lua_parser_node_name(lua_State *L) {
	GET_LUA_PARSER(p, 1);
	GET_NODE(n, 2);
	TSSymbol sym = ts_node_symbol(n);
	const char *name = ts_language_symbol_name((const TSLanguage *)p->lang, sym);
	lua_pushstring(L, name);
	return 1;
}

static const luaL_Reg parser_methods[] = {
	{"parse_string", lua_parser_parse_string},
	{"set_timeout", lua_parser_set_timeout},
	{"node_name", lua_parser_node_name},
	{NULL, NULL}
};
static const luaL_Reg parser_metamethods[] = {
	{"__gc", lua_close_parser},
	{NULL, NULL}
};
/* }}}*/
/* {{{ Tree Object */
// function(TSTree): TSNode
int lua_tree_root(lua_State *L) {
	GET_LUA_TREE(t, 1);
	struct LuaTSNode *const n = lua_newuserdata(L, sizeof(struct LuaTSNode));
	n->n = ts_tree_root_node(t->t);
	n->lang = t->lang;
	luaL_setmetatable(L, LUA_TSNODE_METATABLE);
	return 1;
}

int lua_tree_to_string(lua_State *L) {
	GET_TREE(t, 1);
	const TSNode root = ts_tree_root_node(t);
	char *s = ts_node_string(root);
	lua_pushlstring(L, (const char *)s, strlen(s));
	free(s);
	return 1;
}

int lua_tree_copy(lua_State *L) {
	GET_TREE(t, 1);
	struct LuaTSTree *const t_copy = lua_newuserdata(L, sizeof(struct LuaTSTree));
	t_copy->t = ts_tree_copy(t);
	luaL_setmetatable(L, LUA_TSTREE_METATABLE);
	return 1;
}

bool is_non_negative(lua_State *L, int i) {
	return lua_tonumber(L, i) >= 0;
}

void expect_arg_field(lua_State *L, int idx, const char *field_name, int expected_type) {
	const int actual_type = lua_getfield(L, idx, field_name);
	if (actual_type != expected_type) {
		luaL_error(
			L,
			"expected field `%s' to be a %s (got %s)",
			field_name,
			lua_typename(L, expected_type),
			lua_typename(L, actual_type)
		);
	}
}

void expect_nested_arg_field(lua_State *L, int idx, const char *parent_name, const char *field_name, int expected_type) {
	const int actual_type = lua_getfield(L, idx, field_name);
	if (actual_type != expected_type) {
		luaL_error(
			L,
			"expected field `%s.%s' to be a %s (got %s)",
			parent_name,
			field_name,
			lua_typename(L, expected_type),
			lua_typename(L, actual_type)
		);
	}
}

int lua_tree_edit(lua_State *L) {
	lua_settop(L, 2);
	GET_TREE(t, 1);

	// get the edit struct from table
	luaL_argcheck(L, lua_type(L, 2) == LUA_TTABLE, 2, "expected table");

	expect_arg_field(L, 2, "start_byte", LUA_TNUMBER);
	expect_arg_field(L, 2, "old_end_byte", LUA_TNUMBER);
	expect_arg_field(L, 2, "new_end_byte", LUA_TNUMBER);

	expect_arg_field(L, 2, "start_point", LUA_TTABLE);
	expect_arg_field(L, -1, "row", LUA_TNUMBER);
	expect_arg_field(L, -2, "column", LUA_TNUMBER);

	expect_arg_field(L, 2, "old_end_point", LUA_TTABLE);
	expect_nested_arg_field(L, -1, "old_end_point", "row", LUA_TNUMBER);
	expect_nested_arg_field(L, -2, "old_end_point", "column", LUA_TNUMBER);

	expect_arg_field(L, 2, "new_end_point", LUA_TTABLE);
	expect_nested_arg_field(L, -1, "new_end_point", "row", LUA_TNUMBER);
	expect_nested_arg_field(L, -2, "new_end_point", "column", LUA_TNUMBER);

	// stack
	// 1.   tree
	// 2.   table argument
	// 3.   start_byte (u32)
	// 4.   old_end_byte (u32)
	// 5.   new_end_byte (u32)
	// 6.   start_point { .row (u32), .column (u32) }
	// 7.   start_point.row (u32)
	// 8.   start_point.col (u32)
	// 9.   old_end_point { .row (u32), .column (u32) }
	// 10.  old_end_point.row (u32)
	// 11.  old_end_point.col (u32)
	// 12.  new_end_point { .row (u32), .column (u32) }
	// 13.  new_end_point.row (u32)
	// 14.  new_end_point.col (u32)

	ts_tree_edit(t, &(const TSInputEdit){
		.start_byte    = lua_tonumber(L, 3),
		.old_end_byte  = lua_tonumber(L, 4),
		.new_end_byte  = lua_tonumber(L, 5),

		.start_point   = { .row = lua_tonumber(L, 7),  .column = lua_tonumber(L, 8)  },
		.old_end_point = { .row = lua_tonumber(L, 10), .column = lua_tonumber(L, 11) },
		.new_end_point = { .row = lua_tonumber(L, 13), .column = lua_tonumber(L, 14) },
	});
	return 0;
}

int lua_tree_gc(lua_State *L) {
	GET_TREE(t, 1);
	ts_tree_delete(t);
	return 0;
}

static const luaL_Reg tree_methods[] = {
	{"get_root", lua_tree_root},
	{"copy", lua_tree_copy},
	{"edit", lua_tree_edit},
	{NULL, NULL}
};
static const luaL_Reg tree_metamethods[] = {
	{"__gc", lua_tree_gc},
	{"__tostring", lua_tree_to_string},
	{NULL, NULL}
};
/* }}}*/
/* {{{ Node Object */
#define PUSH_LUA_NODE(_name, _node, _lang) \
	struct LuaTSNode *const _name = lua_newuserdata(L, sizeof(struct LuaTSNode)) ; \
	(_name)->lang = (_lang) ; \
	(_name)->n = (_node) ; \
	luaL_setmetatable(L, LUA_TSNODE_METATABLE)

//@teal function(TSNode): string
int lua_node_type(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushstring(L, ts_node_type(n));
	return 1;
}
/* int lua_node_symbol(lua_State *L); // TODO: function(TSNode): TSSymbol (u16)*/
//@teal function(TSNode): u32
int lua_node_start_byte(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushnumber(L, ts_node_start_byte(n));
	return 1;
}

//@teal function(TSNode): u32
int lua_node_end_byte(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushnumber(L, ts_node_end_byte(n));
	return 1;
}

//@teal function(TSNode): { row: u32, column: u32 }
int lua_node_start_point(lua_State *L) {
	GET_NODE(n, 1);
	TSPoint p = ts_node_start_point(n);
	lua_newtable(L);

	lua_pushnumber(L, p.row);
	lua_setfield(L, -2, "row");

	lua_pushnumber(L, p.column);
	lua_setfield(L, -2, "column");

	return 1;
}
//@teal function(TSNode): u32*/
int lua_node_end_point(lua_State *L) {
	GET_NODE(n, 1);
	TSPoint p = ts_node_end_point(n);
	lua_newtable(L);

	lua_pushnumber(L, p.row);
	lua_setfield(L, -2, "row");

	lua_pushnumber(L, p.column);
	lua_setfield(L, -2, "column");
	return 1;
}

//@teal function(TSNode): boolean
int lua_node_is_named(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushboolean(L, ts_node_is_named(n));
	return 1;
}

//@teal function(TSNode): boolean
int lua_node_is_missing(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushboolean(L, ts_node_is_missing(n));
	return 1;
}

//@teal function(TSNode): boolean
int lua_node_is_extra(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushboolean(L, ts_node_is_extra(n));
	return 1;
}

//@teal function(TSNode, u32): TSNode
int lua_node_child(lua_State *L) {
	GET_LUA_NODE(parent, 1);
	const uint32_t idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_child_count(parent->n)) {
		lua_pushnil(L);
	} else {
		/* const TSLanguage *lang = parent->lang;*/
		PUSH_LUA_NODE(
			child,
			ts_node_child(parent->n, (uint32_t)luaL_checknumber(L, 2)),
			parent->lang
		);
	}
	return 1;
}

 //@teal function(TSNode): u32
int lua_node_child_count(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushnumber(L, ts_node_child_count(n));
	return 1;
}

//@teal function(TSNode, u32): TSNode
int lua_node_named_child(lua_State *L) {
	GET_LUA_NODE(parent, 1);
	const uint32_t idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_child_count(parent->n)) {
		lua_pushnil(L);
	} else {
		PUSH_LUA_NODE(
			_,
			ts_node_named_child(parent->n, idx),
			parent->lang
		);
	}
	return 1;
}

//@teal function(TSNode): u32
int lua_node_named_child_count(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushnumber(L, ts_node_named_child_count(n));
	return 1;
}

int lua_node_children_iterator(lua_State *L) {
	GET_LUA_NODE(n, lua_upvalueindex(1));

	const uint32_t idx = lua_tonumber(L, lua_upvalueindex(2));
	lua_pushnumber(L, idx + 1);
	lua_replace(L, lua_upvalueindex(2));

	if (idx >= ts_node_child_count(n->n)) {
		lua_pushnil(L);
	} else {
		PUSH_LUA_NODE(
			_,
			ts_node_child(n->n, idx),
			n->lang
		);
	}

	return 1;
}

int lua_node_named_children_iterator(lua_State *L) {
	GET_LUA_NODE(n, lua_upvalueindex(1));

	const uint32_t idx = lua_tonumber(L, lua_upvalueindex(2));
	lua_pushnumber(L, idx + 1);
	lua_replace(L, lua_upvalueindex(2));

	if (idx >= ts_node_named_child_count(n->n)) {
		lua_pushnil(L);
	} else {
		PUSH_LUA_NODE(_, ts_node_named_child(n->n, idx), n->lang);
	}

	return 1;
}

int lua_node_children(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushnumber(L, 0);
	lua_pushcclosure(L, lua_node_children_iterator, 2);
	return 1;
}

int lua_node_named_children(lua_State *L) {
	GET_NODE(n, 1);
	lua_pushnumber(L, 0);
	lua_pushcclosure(L, lua_node_named_children_iterator, 2);
	return 1;
}

//@teal function(TSNode): TSNode
int lua_node_next_sibling(lua_State *L) {
	GET_LUA_NODE(n, 1);
	PUSH_LUA_NODE(_, ts_node_next_sibling(n->n), n->lang);
	return 1;
}

//@teal function(TSNode): TSNode
int lua_node_prev_sibling(lua_State *L) {
	GET_LUA_NODE(n, 1);
	PUSH_LUA_NODE(_, ts_node_prev_sibling(n->n), n->lang);
	return 1;
}

int lua_node_string(lua_State *L) {
	GET_NODE(n, 1);
	char *s = ts_node_string(n);
	lua_pushstring(L, s);
	free(s);
	return 1;
}

int lua_node_name(lua_State *L) {
	GET_LUA_NODE(n, 1);
	TSSymbol sym = ts_node_symbol(n->n);
	const char *name = ts_language_symbol_name(n->lang, sym);
	lua_pushstring(L, name);
	return 1;
}

static const luaL_Reg node_methods[] = {
	{"type", lua_node_type},

	{"name", lua_node_name},

	{"get_child", lua_node_child},
	{"get_child_count", lua_node_child_count},

	{"get_named_child", lua_node_named_child},
	{"get_named_child_count", lua_node_named_child_count},

	{"get_start_byte", lua_node_start_byte},
	{"get_end_byte", lua_node_end_byte},

	{"get_start_point", lua_node_start_point},
	{"get_end_point", lua_node_end_point},

	{"get_next_sibling", lua_node_next_sibling},
	{"get_prev_sibling", lua_node_prev_sibling},

	{"children", lua_node_children},
	{"named_children", lua_node_named_children},
	{NULL, NULL}
};
static const luaL_Reg node_metamethods[] = {
	{"__tostring", lua_node_string},
	{NULL, NULL}
};

/* }}}*/

// TODO: Cursor stuff

static const luaL_Reg lib_funcs[] = {
	{"load", lua_load_parser},
	{NULL, NULL}
};

LUA_API int luaopen_ltreesitter(lua_State *L) {
	create_metatable(L, LUA_TSPARSER_METATABLE, parser_metamethods, parser_methods);
	create_metatable(L, LUA_TSTREE_METATABLE, tree_metamethods, tree_methods);
	create_metatable(L, LUA_TSNODE_METATABLE, node_metamethods, node_methods);
	luaL_newlib(L, lib_funcs);
	return 1;
}
