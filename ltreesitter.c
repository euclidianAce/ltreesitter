
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#error To be implemented :D
#else
#include <dlfcn.h>
#endif

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <tree_sitter/api.h>

#define LUA_TSPARSER_METATABLE         "ltreesitter.TSParser"
#define LUA_TSTREE_METATABLE           "ltreesitter.TSTree"
#define LUA_TSTREECURSOR_METATABLE     "ltreesitter.TSTreeCursor"
#define LUA_TSNODE_METATABLE           "ltreesitter.TSNode"
#define LUA_TSQUERY_METATABLE          "ltreesitter.TSQuery"
#define LUA_TSQUERYCURSOR_METATABLE    "ltreesitter.TSQueryCursor"
static const char registry_index[] = "ltreesitter_registry";

struct LuaTSParser {
	const TSLanguage *lang;
	void *dlhandle;
	TSParser *parser;
};
struct LuaTSTree {
	const TSLanguage *lang;
	TSTree *t;
};
struct LuaTSTreeCursor {
	const TSLanguage *lang;
	TSTreeCursor c;
};
struct LuaTSNode {
	const TSLanguage *lang;
	TSNode n;
};
struct LuaTSInputEdit { TSInputEdit *e; };
struct LuaTSQuery {
	const TSLanguage *lang;
	TSQuery *q;
};
struct LuaTSQueryCursor {
	struct LuaTSQuery *q;
	TSQueryCursor *c;
};

#define TREE_SITTER_SYM "tree_sitter_"

/* {{{ Utility */

/* @teal-export _get_registry_entry: function(): table [[
   ltreesitter uses a table in the Lua registry to keep references alive and prevent Lua's garbage collection from collecting things that the library needs internally.
   The behavior nor existence of this function should not be relied upon and is included strictly for memory debugging purposes

   Though, if you are looking to debug a segfault/garbage collection bug, this is a useful tool in addition to the lua inspect module
]]*/
int push_registry_table(lua_State *L) {
	lua_getfield(L, LUA_REGISTRYINDEX, registry_index);
	return 1;
}

TSNode get_node(lua_State *L, int idx) { return ((struct LuaTSNode *)luaL_checkudata(L, (idx), LUA_TSNODE_METATABLE))->n; }
struct LuaTSNode *const get_lua_node(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSNODE_METATABLE); }

void set_parent(lua_State *L, int child_idx, int parent_idx) {
	push_registry_table(L); // { <ltreesitter_registry> }
	lua_pushvalue(L, child_idx);
	lua_pushvalue(L, parent_idx); // { <ltreesitter_registry> }, <Child>, <Parent>
	lua_settable(L, -3); // {<ltreesitter_registry> [<Child>] = <Parent>}
	lua_pop(L, 1);
}

// parent_idx is the *absolute* index of the lua object that needs to stay alive for the node to be valid
// this is usually the tree itself
struct LuaTSNode *const push_lua_node(lua_State *L, int parent_idx, TSNode node, const TSLanguage *lang) {
	struct LuaTSNode *const ln = lua_newuserdata(L, sizeof(struct LuaTSNode));
	ln->n = node;
	ln->lang = lang;
	luaL_setmetatable(L, LUA_TSNODE_METATABLE);
	set_parent(L, lua_gettop(L), parent_idx);
	return ln;
}

void push_parent(lua_State *L, int obj_idx) {
	push_registry_table(L); // { <ltreesitter_registry> }
	lua_pushvalue(L, obj_idx); // { <ltreesitter_registry> }, <obj>
	lua_gettable(L, -2); // { <ltreesitter_registry> }, <parent>
	lua_rotate(L, -2, 1); // <parent>, { <ltreesitter_registry> }
	lua_pop(L, 1); // <parent>
}

TSTree *const get_tree(lua_State *L, int idx) { return ((struct LuaTSTree *)luaL_checkudata(L, (idx), LUA_TSTREE_METATABLE))->t; }
struct LuaTSTree *const get_lua_tree(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSTREE_METATABLE); }

TSTreeCursor get_tree_cursor(lua_State *L, int idx) { return ((struct LuaTSTreeCursor *)luaL_checkudata(L, (idx), LUA_TSTREECURSOR_METATABLE))->c; }
struct LuaTSTreeCursor *const get_lua_tree_cursor(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSTREECURSOR_METATABLE); }
struct LuaTSTreeCursor *const push_lua_tree_cursor(lua_State *L, const TSLanguage *lang, TSNode n) {
	TSTreeCursor c = ts_tree_cursor_new(n);
	struct LuaTSTreeCursor *const lc = lua_newuserdata(L, sizeof(struct LuaTSTreeCursor));
	lc->lang = lang;
	lc->c = c;
	luaL_setmetatable(L, LUA_TSTREECURSOR_METATABLE);

	return lc;
}

TSParser *const get_parser(lua_State *L, int idx) { return ((struct LuaTSParser *)luaL_checkudata(L, (idx), LUA_TSPARSER_METATABLE))->parser; }
struct LuaTSParser *const get_lua_parser(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSPARSER_METATABLE); }

TSQuery *const get_query(lua_State *L, int idx) { return ((struct LuaTSQuery *)luaL_checkudata(L, (idx), LUA_TSQUERY_METATABLE))->q; }
struct LuaTSQuery *const get_lua_query(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSQUERY_METATABLE); }

TSQueryCursor *const get_query_cursor(lua_State *L, int idx) { return ((struct LuaTSQueryCursor *)luaL_checkudata(L, (idx), LUA_TSQUERYCURSOR_METATABLE))->c; }
struct LuaTSQueryCursor *const get_lua_query_cursor(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSQUERYCURSOR_METATABLE); }

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
/* {{{ Query Object */
/* @teal-export Parser.query: function(Parser, string): Query [[
   Create a query out of the given string for the language of the given parser
]] */
int lua_make_query(lua_State *L) {
	struct LuaTSParser *p = get_lua_parser(L, 1);
	const char *query_src = luaL_checkstring(L, 2);
	uint32_t err_offset;
	TSQueryError err_type;
	TSQuery *q = ts_query_new(
		p->lang,
		query_src,
		strlen(query_src),
		&err_offset,
		&err_type
	);
	if (!q) {
		char slice[16] = { 0 };
		strncpy(slice, &query_src[
			err_offset >= 10 ? err_offset - 10 : err_offset
		], 15);
		slice[15] = 0;

		switch (err_type) {
		case TSQueryErrorSyntax:    lua_pushfstring(L, "Query syntax error: around '%s' (at offset %d)",    slice, (int)err_offset); break;
		case TSQueryErrorNodeType:  lua_pushfstring(L, "Query node type error: around '%s' (at offset %d)", slice, (int)err_offset); break;
		case TSQueryErrorField:     lua_pushfstring(L, "Query field error: around '%s' (at offset %d)",     slice, (int)err_offset); break;
		case TSQueryErrorCapture:   lua_pushfstring(L, "Query capture error: around '%s' (at offset %d)",   slice, (int)err_offset); break;
		case TSQueryErrorStructure: lua_pushfstring(L, "Query structure error: around '%s' (at offset %d)", slice, (int)err_offset); break;
		default: return luaL_error(L, "unreachable, this is a bug");
		}

		return lua_error(L);
	}

	struct LuaTSQuery *lq = lua_newuserdata(L, sizeof(struct LuaTSQuery));
	luaL_setmetatable(L, LUA_TSQUERY_METATABLE);
	lq->lang = p->lang;
	lq->q = q;
	return 1;
}

int lua_query_gc(lua_State *L) {
	struct LuaTSQuery *q = get_lua_query(L, 1);
#ifdef LOG_GC
	printf("Query %p is being garbage collected\n", q);
#endif
	ts_query_delete(q->q);
	return 1;
}

int lua_query_pattern_count(lua_State *L) {
	TSQuery *q = get_query(L, 1);
	lua_pushnumber(L, ts_query_pattern_count(q));
	return 1;
}
int lua_query_capture_count(lua_State *L) {
	TSQuery *q = get_query(L, 1);
	lua_pushnumber(L, ts_query_capture_count(q));
	return 1;
}
int lua_query_string_count(lua_State *L) {
	TSQuery *q = get_query(L, 1);
	lua_pushnumber(L, ts_query_string_count(q));
	return 1;
}

int lua_query_cursor_gc(lua_State *L) {
	struct LuaTSQueryCursor *c = get_lua_query_cursor(L, 1);
#ifdef LOG_GC
	printf("Query Cursor %p is being garbage collected\n", c);
#endif
	ts_query_cursor_delete(c->c);
	return 0;
}


// TODO: find a better way to do this, @teal-inline wont work since it needs to be nested
/* @teal-export Query.Match.id : number */
/* @teal-export Query.Match.pattern_index : number */
/* @teal-export Query.Match.capture_count : number */
/* @teal-export Query.Match.captures : {string|number:Node} */

/* @teal-export Query.match: function(Query, Node): function(): Match [[
   Iterate over the matches of a given query

   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for match in q:match(node) do
      print(match.my_match)
   end
   </pre>

   The match object is a record populated with all the information given by treesitter
   <pre>
   type Query.Match = record
      id: number
      pattern_index: number
      capture_count: number
      captures: {string|number:Node}
   end
   </pre>
 ]] */
int lua_query_match(lua_State *L) {
	struct LuaTSQueryCursor *c = get_lua_query_cursor(L, lua_upvalueindex(3));
	TSQueryMatch m;
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	if (ts_query_cursor_next_match(c->c, &m)) {
		lua_createtable(L, 0, 5); // { <match> }
		lua_pushnumber(L, m.id); lua_setfield(L, -2, "id"); // { <match> }
		lua_pushnumber(L, m.pattern_index); lua_setfield(L, -2, "pattern_index"); // { <match> }
		lua_pushnumber(L, m.capture_count); lua_setfield(L, -2, "capture_count"); // { <match> }
		lua_createtable(L, m.capture_count, m.capture_count); // { <match> }, { <arraymap> }
		lua_createtable(L, 0, m.capture_count); // { <match> }, { <arraymap> }
		for (uint16_t i = 0; i < m.capture_count; ++i) {
			push_lua_node(
				L, parent_idx,
				m.captures[i].node,
				c->q->lang
			); // {<match>}, {<arraymap>}, <Node>
			lua_pushvalue(L, -1); // {<match>}, {<arraymap>}, <Node>, <Node>
			lua_seti(L, -3, i+1); // {<match>}, {<arraymap> <Node>}, <Node>
			uint32_t len;
			const char *name = ts_query_capture_name_for_id(c->q->q, i, &len);
			lua_setfield(L, -2, name); // {<match>}, {<arraymap> <Node>, [name]=<Node>}
		}
		lua_setfield(L, -3, "captures"); // {<match> captures=<arraymap>}
		return 1;
	}
	return 0;
}

/* @teal-export Query.capture: function(Query, Node): function(): Node, string [[
   Iterate over the captures of a given query

   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for capture, name in q:capture(node) do
      print(capture, name) -- => (comment), "my_match"
   end
   </pre>
]] */
int lua_query_capture(lua_State *L) {
	struct LuaTSQuery *const q = get_lua_query(L, lua_upvalueindex(1));
	TSQueryCursor *c = get_query_cursor(L, lua_upvalueindex(3));
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	TSQueryMatch m;
	uint32_t capture_index;
	if (ts_query_cursor_next_capture(c, &m, &capture_index)) {
		push_lua_node(
			L, parent_idx,
			m.captures[capture_index].node,
			q->lang
		);
		uint32_t len;
		const char *name = ts_query_capture_name_for_id(q->q, capture_index, &len);
		lua_pushlstring(L, name, len);
		return 2;
	}
	return 0;
}

int lua_query_match_factory(lua_State *L) {
	lua_settop(L, 2);
	struct LuaTSQuery *const q = get_lua_query(L, 1);
	TSNode n = get_node(L, 2);
	TSQueryCursor *c = ts_query_cursor_new();
	struct LuaTSQueryCursor *lc = lua_newuserdata(L, sizeof(struct LuaTSQueryCursor));
	luaL_setmetatable(L, LUA_TSQUERYCURSOR_METATABLE);
	lc->c = c;
	lc->q = q;
	ts_query_cursor_exec(c, q->q, n);
	lua_pushcclosure(L, lua_query_match, 3); // prevent the node + query from being gc'ed
	return 1;
}

int lua_query_capture_factory(lua_State *L) {
	lua_settop(L, 2);
	struct LuaTSQuery *const q = get_lua_query(L, 1);
	TSNode n = get_node(L, 2);
	TSQueryCursor *c = ts_query_cursor_new();
	struct LuaTSQueryCursor *lc = lua_newuserdata(L, sizeof(struct LuaTSQueryCursor));
	luaL_setmetatable(L, LUA_TSQUERYCURSOR_METATABLE);
	lc->c = c;
	lc->q = q;
	ts_query_cursor_exec(c, q->q, n);
	lua_pushcclosure(L, lua_query_capture, 3); // prevent the node + query from being gc'ed
	return 1;
}

static const luaL_Reg query_methods[] = {
	{"pattern_count", lua_query_pattern_count},
	{"capture_count", lua_query_capture_count},
	{"string_count", lua_query_string_count},
	{"match", lua_query_match_factory},
	{"capture", lua_query_capture_factory},
	{NULL, NULL}
};

static const luaL_Reg query_metamethods[] = {
	{"__gc", lua_query_gc},
	{NULL, NULL}
};

static const luaL_Reg query_cursor_methods[] = {
	{NULL, NULL}
};

static const luaL_Reg query_cursor_metamethods[] = {
	{"__gc", lua_query_cursor_gc},
	{NULL, NULL}
};
/* }}}*/
/* {{{ Parser Object */
/* @teal-export load: function(file_name: string, language_name: string): Parser, string [[
   Load a parser from a given file

   On unix this uses dlopen, so if a path without a path separator is given, dlopen has its own path's that it will search for your file in.
   So if in doubt use a file path like
   <pre>
   local my_parser = ltreesitter.load("./my_parser.so", "my_language")
   </pre>

   Currently this does not work on Windows
   (The entire library doesn't work on Windows since this is the entry point to any of the functionality)
]] */
int lua_load_parser(lua_State *L) {
	lua_settop(L, 2);
	const char *parser_file = luaL_checkstring(L, 1);
	const char *lang_name = luaL_checkstring(L, 2);

	void *handle = dlopen(parser_file, RTLD_NOW | RTLD_LOCAL); // TODO: are these the necessary flags?
	if (!handle) {
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to dlopen handle: %s", dlerror());
		return 2;
	}
	char buf[128];
	if (snprintf(buf, sizeof(buf) - sizeof(TREE_SITTER_SYM), TREE_SITTER_SYM "%s", lang_name) == 0) {
		dlclose(handle);
		lua_pushnil(L);
		lua_pushstring(L, "Unable to copy language name into buffer");
		return 2;
	}
	/* tree_sitter_lang = (TSLanguage *(*)(void)) dlsym(handle, buf)*/
	TSLanguage *(*tree_sitter_lang)(void) = dlsym(handle, buf);
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

int lua_parser_gc(lua_State *L) {
	struct LuaTSParser *lp = luaL_checkudata(L, 1, LUA_TSPARSER_METATABLE);
#ifdef LOG_GC
	printf("Parser %p is being garbage collected\n", lp);
#endif
	ts_parser_delete(lp->parser);
	dlclose(lp->dlhandle);
	return 0;
}

/* @teal-export Parser.parse_string: function(Parser, string): Tree [[
   Uses the given parser to parse the string
]] */
int lua_parser_parse_string(lua_State *L) {
	struct LuaTSParser *p = get_lua_parser(L, 1);
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

/* @teal-export Parser.set_timeout: function(Parser, number) [[
   Sets how long the parser is allowed to take in microseconds
]] */
int lua_parser_set_timeout(lua_State *L) {
	TSParser *const p = get_parser(L, 1);
	const lua_Number n = luaL_checknumber(L, 2);
	luaL_argcheck(L, n >= 0, 2, "expected non-negative number");
	ts_parser_set_timeout_micros(p, (uint64_t)n);
	return 0;
}

static const luaL_Reg parser_methods[] = {
	{"parse_string", lua_parser_parse_string},
	{"set_timeout", lua_parser_set_timeout},
	{"query", lua_make_query},
	{NULL, NULL}
};
static const luaL_Reg parser_metamethods[] = {
	{"__gc", lua_parser_gc},
	{NULL, NULL}
};
/* }}}*/
/* {{{ Tree Object */
/* @teal-export Tree.root: function(Tree): Node [[
   Returns the root node of the given parse tree
]] */
int lua_tree_root(lua_State *L) {
	struct LuaTSTree *const t = get_lua_tree(L, 1);
	struct LuaTSNode *const n = lua_newuserdata(L, sizeof(struct LuaTSNode));
	push_lua_node(L, 1, ts_tree_root_node(t->t), t->lang);
	return 1;
}

int lua_tree_to_string(lua_State *L) {
	TSTree *t = get_tree(L, 1);
	const TSNode root = ts_tree_root_node(t);
	char *s = ts_node_string(root);
	lua_pushlstring(L, (const char *)s, strlen(s));
	free(s);
	return 1;
}

/* @teal-export Tree.copy: function(Tree): Tree [[
   Creates a copy of the tree. Tree-sitter recommends to create copies if you are going to use multithreading since tree accesses are not thread-safe, but copying them is cheap and quick
]] */
int lua_tree_copy(lua_State *L) {
	TSTree *t = get_tree(L, 1);
	struct LuaTSTree *const t_copy = lua_newuserdata(L, sizeof(struct LuaTSTree));
	t_copy->t = ts_tree_copy(t);
	luaL_setmetatable(L, LUA_TSTREE_METATABLE);
	return 1;
}

bool is_non_negative(lua_State *L, int i) { return lua_tonumber(L, i) >= 0; }

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

/* @teal-inline [[
   record TreeEdit
      start_byte: number
      old_end_byte: number
      new_end_byte: number
      start_point: Point
      old_point_byte: Point
      new_point_byte: Point
   end
]]*/
/* @teal-export Tree.edit: function(Tree, TreeEdit) [[
   Create an edit to the given tree
]] */
int lua_tree_edit(lua_State *L) {
	lua_settop(L, 2);
	lua_checkstack(L, 15);
	TSTree *t = get_tree(L, 1);

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
	struct LuaTSTree *t = get_lua_tree(L, 1);
#ifdef LOG_GC
	printf("Tree %p is being garbage collected\n", t);
#endif
	ts_tree_delete(t->t);
	return 0;
}

static const luaL_Reg tree_methods[] = {
	{"root", lua_tree_root},
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
/* {{{ Tree Cursor Object */
// TODO: should this be exposed, or only used internally
// Porque no los dos?

void push_tree_cursor(lua_State *L, int parent_idx, const TSLanguage *lang, TSNode n) {
	struct LuaTSTreeCursor *c = lua_newuserdata(L, sizeof(struct LuaTSTreeCursor));
	c->c = ts_tree_cursor_new(n);
	c->lang = lang;
	luaL_setmetatable(L, LUA_TSTREECURSOR_METATABLE);
	set_parent(L, lua_gettop(L), parent_idx);
}

/* @teal-export Node.create_cursor: function(Node): Cursor [[
   Create a new cursor at the given node
]] */
int lua_tree_cursor_create(lua_State *L) {
	lua_settop(L, 1);
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	push_tree_cursor(L, 2, n->lang, n->n);
	return 1;
}

/* @teal-export Cursor.current_node: function(Cursor): Node [[
   Get the current node under the cursor
]] */
int lua_tree_cursor_current_node(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	push_parent(L, 1);
	push_lua_node(
		L, 2,
		ts_tree_cursor_current_node(&c->c),
		c->lang
	);
	return 1;
}

/* @teal-export Cursor.current_field_name: function(Cursor): string [[
   Get the field name of the current node under the cursor
]] */
int lua_tree_cursor_current_field_name(lua_State *L) {
	TSTreeCursor c = get_tree_cursor(L, 1);
	const char *field_name = ts_tree_cursor_current_field_name(&c);
	if (field_name) {
		lua_pushstring(L, field_name);
	} else {
		lua_pushnil(L);
	}
	return 1;
}

/* @teal-export Cursor.reset: function(Cursor, Node) [[
   Position the cursor at the given node
]] */
int lua_tree_cursor_reset(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	TSNode n = get_node(L, 2);
	ts_tree_cursor_reset(&c->c, n);
	return 0;
}

/* @teal-export Cursor.goto_parent: function(Cursor): boolean [[
   Position the cursor at the parent of the current node
]] */
int lua_tree_cursor_goto_parent(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_parent(&c->c));
	return 1;
}

/* @teal-export Cursor.goto_next_sibling: function(Cursor): boolean [[
   Position the cursor at the sibling of the current node
]] */
int lua_tree_cursor_goto_next_sibling(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(&c->c));
	return 1;
}

/* @teal-export Cursor.goto_first_child: function(Cursor): boolean [[
   Position the cursor at the first child of the current node
]] */
int lua_tree_cursor_goto_first_child(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_first_child(&c->c));
	return 1;
}

int lua_tree_cursor_gc(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
#ifdef LOG_GC
	printf("Tree Cursor %p is being garbage collected\n", c);
#endif
	ts_tree_cursor_delete(&c->c);
	return 0;
}

static const luaL_Reg tree_cursor_methods[] = {
	{"current_node", lua_tree_cursor_current_node},
	{"current_field_name", lua_tree_cursor_current_field_name},
	{"goto_parent", lua_tree_cursor_goto_parent},
	{"goto_first_child", lua_tree_cursor_goto_first_child},
	{"goto_next_sibling", lua_tree_cursor_goto_next_sibling},
	{"reset", lua_tree_cursor_reset},
	{NULL, NULL}
};
static const luaL_Reg tree_cursor_metamethods[] = {
	{"__gc", lua_tree_cursor_gc},
	{NULL, NULL}
};

/* }}}*/
/* {{{ Node Object */
/* @teal-export Node.type: function(Node): string [[
   Get the type of the given node
]] */
int lua_node_type(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushstring(L, ts_node_type(n));
	return 1;
}

/* @teal-export Node.start_byte: function(Node): number [[
   Get the byte of the source string that the given node starts at
]] */
int lua_node_start_byte(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_start_byte(n));
	return 1;
}

/* @teal-export Node.end_byte: function(Node): number [[
   Get the byte of the source string that the given node ends at
]] */
int lua_node_end_byte(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_end_byte(n));
	return 1;
}

/* @teal-export Node.range: function(Node): number, number [[
   Get both the start and end bytes of the source string
   for easy use with string.sub
   <pre> print( source_string:sub( my_node:range() ) ) </pre>
]] */
int lua_node_byte_range(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_start_byte(n));
	lua_pushnumber(L, ts_node_end_byte(n) + 1);
	return 2;
}

/* @teal-inline [[
   record Point
      row: number
      column: number
   end
]]*/

/* @teal-export Node.start_point: function(Node): Point [[
   Get the row and column of where the given node starts
]] */
int lua_node_start_point(lua_State *L) {
	TSNode n = get_node(L, 1);
	TSPoint p = ts_node_start_point(n);
	lua_newtable(L);

	lua_pushnumber(L, p.row);
	lua_setfield(L, -2, "row");

	lua_pushnumber(L, p.column);
	lua_setfield(L, -2, "column");

	return 1;
}

/* @teal-export Node.end_point: function(Node): Point [[
   Get the row and column of where the given node ends
]] */
int lua_node_end_point(lua_State *L) {
	TSNode n = get_node(L, 1);
	TSPoint p = ts_node_end_point(n);
	lua_newtable(L);

	lua_pushnumber(L, p.row);
	lua_setfield(L, -2, "row");

	lua_pushnumber(L, p.column);
	lua_setfield(L, -2, "column");
	return 1;
}

/* @teal-export Node.is_named: function(Node): boolean [[
   Get whether or not the current node is named
]] */
int lua_node_is_named(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushboolean(L, ts_node_is_named(n));
	return 1;
}

/* @teal-export Node.is_missing: function(Node): boolean [[
   Get whether or not the current node is missing
]] */
int lua_node_is_missing(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushboolean(L, ts_node_is_missing(n));
	return 1;
}

/* @teal-export Node.is_extra: function(Node): boolean [[
   Get whether or not the current node is missing
]] */
int lua_node_is_extra(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushboolean(L, ts_node_is_extra(n));
	return 1;
}

/* @teal-export Node.child: function(Node, idx: number): Node [[
   Get the node's idx'th child (0-indexed)
]] */
int lua_node_child(lua_State *L) {
	struct LuaTSNode *const parent = get_lua_node(L, 1);
	const uint32_t idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_child_count(parent->n)) {
		lua_pushnil(L);
	} else {
		push_parent(L, 1);
		push_lua_node(
			L, lua_gettop(L),
			ts_node_child(parent->n, (uint32_t)luaL_checknumber(L, 2)),
			parent->lang
		);
	}
	return 1;
}

/* @teal-export Node.child_count: function(Node): number [[
   Get the number of children a node has
]] */
int lua_node_child_count(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_child_count(n));
	return 1;
}

/* @teal-export Node.named_child: function(Node, idx: number): Node [[
   Get the node's idx'th named child (0-indexed)
]] */
int lua_node_named_child(lua_State *L) {
	struct LuaTSNode *const parent = get_lua_node(L, 1);
	const uint32_t idx = luaL_checknumber(L, 2);
	if (idx >= ts_node_child_count(parent->n)) {
		lua_pushnil(L);
	} else {
		push_parent(L, 1);
		push_lua_node(
			L, 3,
			ts_node_named_child(parent->n, idx),
			parent->lang
		);
	}
	return 1;
}

/* @teal-export Node.named_child_count: function(Node): number [[
   Get the number of named children a node has
]] */
int lua_node_named_child_count(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_named_child_count(n));
	return 1;
}

// TODO: this should probably use a cursor
int lua_node_children_iterator(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);

	const uint32_t idx = lua_tonumber(L, lua_upvalueindex(2));
	lua_pushnumber(L, idx + 1);
	lua_replace(L, lua_upvalueindex(2));

	if (idx < ts_node_child_count(n->n)) {
		push_lua_node(
			L, 1,
			ts_node_child(n->n, idx),
			n->lang
		);
	} else {
		lua_pushnil(L);
	}

	return 1;
}

int lua_node_named_children_iterator(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, lua_upvalueindex(1));

	const uint32_t idx = lua_tonumber(L, lua_upvalueindex(3));
	lua_pushnumber(L, idx + 1);
	lua_replace(L, lua_upvalueindex(3));

	if (idx >= ts_node_named_child_count(n->n)) {
		lua_pushnil(L);
	} else {
		push_lua_node(
			L, lua_upvalueindex(2),
			ts_node_named_child(n->n, idx),
			n->lang
		);
	}

	return 1;
}

/* @teal-export Node.children: function(Node): function(): Node [[
   Iterate over a node's children
]] */
int lua_node_children(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_lua_tree_cursor(L, n->lang, n->n);
	lua_pushcclosure(L, lua_node_children_iterator, 2);
	return 1;
}

/* @teal-export Node.named_children: function(Node): function(): Node [[
   Iterate over a node's named children
]] */
int lua_node_named_children(lua_State *L) {
	TSNode n = get_node(L, 1);
	push_parent(L, 1);
	lua_pushnumber(L, 0);
	
	lua_pushcclosure(L, lua_node_named_children_iterator, 3);
	return 1;
}

/* @teal-export Node.next_sibling: function(Node): Node [[
   Get a node's next sibling
]] */
int lua_node_next_sibling(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	TSNode sibling = ts_node_next_sibling(n->n);
	if (ts_node_is_null(sibling)) { lua_pushnil(L); return 1; }
	push_lua_node(
		L, 2,
		sibling,
		n->lang
	);
	return 1;
}

/* @teal-export Node.prev_sibling: function(Node): Node [[
   Get a node's previous sibling
]] */
int lua_node_prev_sibling(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	TSNode sibling = ts_node_prev_sibling(n->n);
	if (ts_node_is_null(sibling)) { lua_pushnil(L); return 1; }
	push_lua_node(
		L, 2,
		sibling,
		n->lang
	);
	return 1;
}

/* @teal-export Node.next_named_sibling: function(Node): Node [[
   Get a node's next named sibling
]] */
int lua_node_next_named_sibling(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	TSNode sibling = ts_node_next_named_sibling(n->n);
	if (ts_node_is_null(sibling)) { lua_pushnil(L); return 1; }
	push_lua_node(
		L, 2,
		sibling,
		n->lang
	);
	return 1;
}

/* @teal-export Node.prev_named_sibling: function(Node): Node [[
   Get a node's previous named sibling
]] */
int lua_node_prev_named_sibling(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	TSNode sibling = ts_node_prev_named_sibling(n->n);
	if (ts_node_is_null(sibling)) { lua_pushnil(L); return 1; }
	push_lua_node(
		L, 2,
		sibling,
		n->lang
	);
	return 1;
}

int lua_node_string(lua_State *L) {
	TSNode n = get_node(L, 1);
	char *s = ts_node_string(n);
	lua_pushstring(L, s);
	free(s);
	return 1;
}

/* @teal-export Node.name: function(Node): string [[
   Returns the name of a given node
   <pre>
   print(node) -- => (comment)
   print(node:name()) -- => comment
   </pre>
]] */

int lua_node_name(lua_State *L) {
	struct LuaTSNode *const n = get_lua_node(L, 1);
	if (ts_node_is_null(n->n) || !ts_node_is_named(n->n)) { lua_pushnil(L); return 1; }
	TSSymbol sym = ts_node_symbol(n->n);
	const char *name = ts_language_symbol_name(n->lang, sym);
	lua_pushstring(L, name);
	return 1;
}

/* @teal-export Node.child_by_field_name: function(Node, string): Node [[
   Get a node's child given a field name
]] */
int lua_node_child_by_field_name(lua_State *L) {
	lua_settop(L, 2);
	struct LuaTSNode *const n = get_lua_node(L, 1);
	const char *name = luaL_checkstring(L, 2);

	TSNode child = ts_node_child_by_field_name(n->n, name, strlen(name));
	if (ts_node_is_null(child)) {
		lua_pushnil(L);
	} else {
		push_parent(L, 1);
		push_lua_node(
			L, 3,
			child,
			n->lang
		);
	}
	return 1;
}

static const luaL_Reg node_methods[] = {
	{"type", lua_node_type},
	{"name", lua_node_name},
	{"range", lua_node_byte_range},

	{"child", lua_node_child},
	{"child_count", lua_node_child_count},

	{"named_child", lua_node_named_child},
	{"named_child_count", lua_node_named_child_count},
	{"child_by_field_name", lua_node_child_by_field_name},

	{"start_byte", lua_node_start_byte},
	{"end_byte", lua_node_end_byte},

	{"start_point", lua_node_start_point},
	{"end_point", lua_node_end_point},

	{"next_sibling", lua_node_next_sibling},
	{"prev_sibling", lua_node_prev_sibling},

	{"next_named_sibling", lua_node_next_named_sibling},
	{"prev_named_sibling", lua_node_prev_named_sibling},

	{"children", lua_node_children},
	{"named_children", lua_node_named_children},

	{"create_cursor", lua_tree_cursor_create},
	{NULL, NULL}
};
static const luaL_Reg node_metamethods[] = {
	{"__tostring", lua_node_string},
	{NULL, NULL}
};

/* }}}*/

static const luaL_Reg lib_funcs[] = {
	{"load", lua_load_parser},
	{"_get_registry_entry", push_registry_table},
	{NULL, NULL}
};

LUA_API int luaopen_ltreesitter(lua_State *L) {
	create_metatable(L, LUA_TSPARSER_METATABLE, parser_metamethods, parser_methods);
	create_metatable(L, LUA_TSTREE_METATABLE, tree_metamethods, tree_methods);
	create_metatable(L, LUA_TSTREECURSOR_METATABLE, tree_cursor_metamethods, tree_cursor_methods);
	create_metatable(L, LUA_TSNODE_METATABLE, node_metamethods, node_methods);
	create_metatable(L, LUA_TSQUERY_METATABLE, query_metamethods, query_methods);
	create_metatable(L, LUA_TSQUERYCURSOR_METATABLE, query_cursor_metamethods, query_cursor_methods);

	lua_newtable(L); // {}

	lua_newtable(L); // {}, {}
	lua_pushstring(L, "k"); // {}, {}, "v"
	lua_setfield(L, -2, "__mode"); // {}, { __mode = "k" }
	lua_setmetatable(L, -2); // { <metatable { __mode = "k" }> }

	lua_setfield(L, LUA_REGISTRYINDEX, registry_index);

	luaL_newlib(L, lib_funcs);
	return 1;
}
