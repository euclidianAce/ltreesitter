
// Debugging macros
// #define DEBUG_ASSERTIONS
// #define LOG_GC
// #define PREVENT_GC

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define DL_EXT "dll"
#else
#include <dlfcn.h>
#define DL_EXT "so"
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
static const char registry_index = 'k';
static const char objects_index[] = "objects";
static const char default_predicates_index[] = "default_predicates";
static const char query_predicates_index[] = "query_predicates";

// @teal-export version: string
static const char version_str[] = "0.0.4+dev";

struct LuaTSParser {
	const TSLanguage *lang;
	void *dlhandle;
	TSParser *parser;
};
struct LuaTSTree {
	const TSLanguage *lang;
	TSTree *t;
	bool own_str;
	const char *src;
	size_t src_len;
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
	const char *src;
	size_t src_len;
	TSQuery *q;
};
struct LuaTSQueryCursor {
	struct LuaTSQuery *q;
	TSQueryCursor *c;
};

#define TREE_SITTER_SYM "tree_sitter_"

/* {{{ Utility */

static inline void *open_dynamic_lib(const char *name) {
#ifdef _WIN32
	return LoadLibrary(name);
#else
	return dlopen(name, RTLD_NOW | RTLD_LOCAL);
#endif
}

static inline void close_dynamic_lib(void *handle) {
#ifdef _WIN32
	FreeLibrary(handle);
#else
	dlclose(handle);
#endif
}

#define UNREACHABLE(L) luaL_error(L, "%s line %d UNREACHABLE", __FILE__, __LINE__)
#define ALLOC_FAIL(L) luaL_error(L, "%s line %d Memory allocation failed!", __FILE__, __LINE__)

// Some compatability shims
#if LUA_VERSION_NUM < 502
#define LUA_OK 0
#endif

static void libtable(lua_State *L, const luaL_Reg l[]) {
	lua_createtable(L, 0, 0);
	for (; l->name != NULL; ++l) {
		lua_pushcfunction(L, l->func);
		lua_setfield(L, -2, l->name);
	}
}

static void create_metatable(
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
	// lua <=5.2 doesn't set the __name field which we rely upon for the tests to pass
#if LUA_VERSION_NUM < 503
	lua_pushstring(L, name);
	lua_setfield(L, -2, "__name");
#endif
}

/* @teal-export _get_registry_entry: function(): table [[
   ltreesitter uses a table in the Lua registry to keep references alive and prevent Lua's garbage collection from collecting things that the library needs internally.
   The behavior nor existence of this function should not be relied upon and is included strictly for memory debugging purposes

   Though, if you are looking to debug a segfault/garbage collection bug, this is a useful tool in addition to the lua inspect module
]]*/
static int push_registry_table(lua_State *L) {
	lua_pushvalue(L, LUA_REGISTRYINDEX); // { <Registry> }
	lua_pushlightuserdata(L, (void *)&registry_index); // { <Registry> }, <void *>
	lua_rawget(L, -2); //  { <Registry> }, { <ltreesitter Registry> }
	lua_remove(L, -2); // { <ltreesitter Registry> }
	return 1;
}

static int push_registry_object_table(lua_State *L) {
	push_registry_table(L);
	lua_getfield(L, -1, objects_index);
	lua_remove(L, -2);
	return 1;
}

static int push_default_predicate_table(lua_State *L) {
	push_registry_table(L);
	lua_getfield(L, -1, default_predicates_index);
	lua_remove(L, -2);
	return 1;
}

static int push_registry_query_predicate_table(lua_State *L) {
	push_registry_table(L);
	lua_getfield(L, -1, query_predicates_index);
	lua_remove(L, -2);
	return 1;
}

// This should only be used for only true indexes, i.e. no lua_upvalueindex, registry stuff, etc.
static inline int make_non_relative(lua_State *L, int idx) {
	if (idx < 0) {
		return lua_gettop(L) + idx + 1;
	} else {
		return idx;
	}
}

static inline void setmetatable(lua_State *L, const char *mt_name) {
	luaL_getmetatable(L, mt_name);
	lua_setmetatable(L, -2);
}

static void set_parent(lua_State *L, int child_idx, int parent_idx) {
	const int abs_child_idx = make_non_relative(L, child_idx);
	const int abs_parent_idx = make_non_relative(L, parent_idx);
	push_registry_object_table(L); // { <objects> }
	lua_pushvalue(L, abs_child_idx);
	lua_pushvalue(L, abs_parent_idx); // { <objects> }, <Child>, <Parent>
	lua_settable(L, -3); // {<objects> [<Child>] = <Parent>}
	lua_pop(L, 1);
}

// parent_idx is the index of the lua object that needs to stay alive for the node to be valid
// this is usually the tree itself
static struct LuaTSNode *push_lua_node(lua_State *L, int parent_idx, TSNode node, const TSLanguage *lang) {
	struct LuaTSNode *const ln = lua_newuserdata(L, sizeof(struct LuaTSNode));
	ln->n = node;
	ln->lang = lang;
	setmetatable(L, LUA_TSNODE_METATABLE);
	set_parent(L, -1, parent_idx);
	return ln;
}

static void push_parent(lua_State *L, int obj_idx) {
	lua_pushvalue(L, obj_idx); // <obj>
	push_registry_object_table(L); // <obj>, { <ltreesitter_registry> }
	lua_insert(L, -2); // { <ltreesitter_registry> }, <obj>
	lua_gettable(L, -2); // { <ltreesitter_registry> }, <parent>
#ifdef DEBUG_ASSERTIONS
	if (!lua_toboolean(L, -1)) { luaL_error(L, "Object has no parent"); }
#endif
	lua_insert(L, -2); // <parent>, { <ltreesitter_registry> }
	lua_pop(L, 1); // <parent>
}

static void push_lua_tree(
	lua_State *L,
	struct TSTree *const tree,
	const struct TSLanguage *lang,
	const char *str,
	const size_t str_len,
	const bool own_str // Does this tree own the string? i.e. should it be free'd when this tree is gc'ed?
) {
	struct LuaTSTree *const t = lua_newuserdata(L, sizeof(struct LuaTSTree));
	t->t = tree;
	t->lang = lang;
	t->src = str;
	t->src_len = str_len;
	t->own_str = own_str;
	setmetatable(L, LUA_TSTREE_METATABLE);
}

static inline TSNode get_node(lua_State *L, int idx) { return ((struct LuaTSNode *)luaL_checkudata(L, (idx), LUA_TSNODE_METATABLE))->n; }
static inline struct LuaTSNode *get_lua_node(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSNODE_METATABLE); }

static inline TSTree *get_tree(lua_State *L, int idx) { return ((struct LuaTSTree *)luaL_checkudata(L, (idx), LUA_TSTREE_METATABLE))->t; }
static inline struct LuaTSTree *get_lua_tree(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSTREE_METATABLE); }

static inline TSTreeCursor get_tree_cursor(lua_State *L, int idx) { return ((struct LuaTSTreeCursor *)luaL_checkudata(L, (idx), LUA_TSTREECURSOR_METATABLE))->c; }
static inline struct LuaTSTreeCursor *get_lua_tree_cursor(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSTREECURSOR_METATABLE); }

static inline TSParser *get_parser(lua_State *L, int idx) { return ((struct LuaTSParser *)luaL_checkudata(L, (idx), LUA_TSPARSER_METATABLE))->parser; }
static inline struct LuaTSParser *get_lua_parser(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSPARSER_METATABLE); }

static inline TSQuery *get_query(lua_State *L, int idx) { return ((struct LuaTSQuery *)luaL_checkudata(L, (idx), LUA_TSQUERY_METATABLE))->q; }
static inline struct LuaTSQuery *get_lua_query(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSQUERY_METATABLE); }

static inline TSQueryCursor *get_query_cursor(lua_State *L, int idx) { return ((struct LuaTSQueryCursor *)luaL_checkudata(L, (idx), LUA_TSQUERYCURSOR_METATABLE))->c; }
static inline struct LuaTSQueryCursor *get_lua_query_cursor(lua_State *L, int idx) { return luaL_checkudata(L, (idx), LUA_TSQUERYCURSOR_METATABLE); }

/* }}}*/
/* {{{ Query Object */

static void handle_query_error(
	lua_State *L,
	TSQuery *q,
	uint32_t err_offset,
	TSQueryError err_type,
	const char *query_src
) {
	if (q) return;
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
	default: UNREACHABLE(L);
	}

	lua_error(L);
}

/* @teal-export Parser.query: function(Parser, string): Query [[
   Create a query out of the given string for the language of the given parser
]] */
static int lua_make_query(lua_State *L) {
	lua_settop(L, 2);
	struct LuaTSParser *p = get_lua_parser(L, 1);
	size_t len;
	const char *query_src = luaL_checklstring(L, 2, &len);
	uint32_t err_offset;
	TSQueryError err_type;
	TSQuery *q = ts_query_new(
		p->lang,
		query_src,
		len,
		&err_offset,
		&err_type
	);
	handle_query_error(L, q, err_offset, err_type, query_src);

	struct LuaTSQuery *lq = lua_newuserdata(L, sizeof(struct LuaTSQuery));
	set_parent(L, 3, 1);
	setmetatable(L, LUA_TSQUERY_METATABLE);
	lq->lang = p->lang;
	lq->src = query_src;
	lq->src_len = len;
	lq->q = q;

	return 1;
}

static void push_query_copy(lua_State *L, int query_idx) {
	struct LuaTSQuery *orig = get_lua_query(L, query_idx);
	push_parent(L, query_idx); // <parent>

	uint32_t err_offset;
	TSQueryError err_type;
	TSQuery *q = ts_query_new(
		orig->lang,
		orig->src,
		orig->src_len,
		&err_offset,
		&err_type
	);

	handle_query_error(L, q, err_offset, err_type, orig->src);

	struct LuaTSQuery *new = lua_newuserdata(L, sizeof(struct LuaTSQuery)); // <Parent>, <Query>
	set_parent(L, -1, -2);
	setmetatable(L, LUA_TSQUERY_METATABLE);
	new->lang = orig->lang;
	new->src = orig->src;
	new->src_len = orig->src_len;
	new->q = q;
	lua_remove(L, -2); // <Query>
}

static int lua_query_gc(lua_State *L) {
	struct LuaTSQuery *q = get_lua_query(L, 1);
#ifdef LOG_GC
	printf("Query %p is being garbage collected\n", q);
#endif
	ts_query_delete(q->q);
	return 1;
}

static int lua_query_pattern_count(lua_State *L) {
	TSQuery *q = get_query(L, 1);
	lua_pushnumber(L, ts_query_pattern_count(q));
	return 1;
}
static int lua_query_capture_count(lua_State *L) {
	TSQuery *q = get_query(L, 1);
	lua_pushnumber(L, ts_query_capture_count(q));
	return 1;
}
static int lua_query_string_count(lua_State *L) {
	TSQuery *q = get_query(L, 1);
	lua_pushnumber(L, ts_query_string_count(q));
	return 1;
}

static int lua_query_cursor_gc(lua_State *L) {
	struct LuaTSQueryCursor *c = get_lua_query_cursor(L, 1);
#ifdef LOG_GC
	printf("Query Cursor %p is being garbage collected\n", c);
#endif
	ts_query_cursor_delete(c->c);
	return 0;
}

static void push_query_predicates(lua_State *L, int query_idx) {
	lua_pushvalue(L, query_idx); // <Query>
	push_registry_query_predicate_table(L); // <Query>, { <Predicates> }
	lua_insert(L, -2); // { <Predicates> }, <Query>
	lua_gettable(L, -2); // { <Predicates> }, <Predicate>
	lua_remove(L, -2); // <Predicate>
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1); // (nothing)
		push_default_predicate_table(L); // <Default Predicate>
	}
}

static bool do_predicates(
	lua_State *L,
	const int query_idx,
	const TSQuery *const q,
	const struct LuaTSTree *const t,
	const TSQueryMatch *const m
) {
	const uint32_t num_patterns = ts_query_pattern_count(q);
	for (uint32_t i = 0; i < num_patterns; ++i) {
		uint32_t num_steps;
		const TSQueryPredicateStep *const s = ts_query_predicates_for_pattern(q, i, &num_steps);
		bool is_question = false; // if a predicate is a question then the query should only match if it results in a truthy value
		/* TODO: Nov 20 02:00 2020
			Currently queries are evaluated in order
			So if a question comes after something with a side effect,
			the side effect happens even if the query doesn't match
			*/
		bool need_func_name = true;
		const char *func_name = NULL;
		size_t num_args = 0;
		for (uint32_t j = 0; j < num_steps; ++j) {
			uint32_t len;
			switch (s[j].type) {
			case TSQueryPredicateStepTypeString: {
				// literal strings

				const char *pred_name = ts_query_string_value_for_id(q, s[j].value_id, &len);
				if (need_func_name) {
					// Find predicate
					push_query_predicates(L, query_idx);
					lua_getfield(L, -1, pred_name);
					if (lua_isnil(L, -1)) {
						lua_pop(L, 1);
						// try to grab default value
						push_default_predicate_table(L);
						lua_getfield(L, -1, pred_name);
						lua_remove(L, -2);
						if (lua_isnil(L, -1)) {
							return luaL_error(L, "Query doesn't have predicate '%s'", pred_name);
						}
					}
					need_func_name = false;
					func_name = pred_name;
					if (func_name[len-1] == '?') {
						is_question = true;
					}
				} else {
					lua_pushlstring(L, pred_name, len);
					++num_args;
				}
				break;
			}
			case TSQueryPredicateStepTypeCapture: {
				// The name of a capture

				const TSNode n = m->captures[s[j].value_id].node;
				const uint32_t start = ts_node_start_byte(n);
				const uint32_t end = ts_node_end_byte(n);
				lua_pushlstring(L, t->src + start, end - start);

				++num_args;
				break;
			}
			case TSQueryPredicateStepTypeDone:
				if (lua_pcall(L, num_args, 1, 0) != 0) {
					lua_pushfstring(L, "Error calling predicate '%s': ", func_name);
					lua_insert(L, -2);
					lua_concat(L, 2);
					lua_error(L);
				}

				if (is_question && !lua_toboolean(L, -1)) {
					return false;
				}

				num_args = 0;
				need_func_name = true;
				is_question = false;
				break;
			}
		}
	}
	return true;
}

// TODO: find a better way to do this, @teal-inline wont work since it needs to be nested
/* @teal-export Query.Match.id : number */
/* @teal-export Query.Match.pattern_index : number */
/* @teal-export Query.Match.capture_count : number */
/* @teal-export Query.Match.captures : {string|number:Node} */

static int lua_query_match(lua_State *L) {
	struct LuaTSQuery *const q = get_lua_query(L, lua_upvalueindex(1));
	struct LuaTSQueryCursor *c = get_lua_query_cursor(L, lua_upvalueindex(3));
	TSQueryMatch m;
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	struct LuaTSTree *const t = get_lua_tree(L, -1);

try_again:
	if (ts_query_cursor_next_match(c->c, &m)) {
		if (!do_predicates(
			L,
			lua_upvalueindex(1),
			q->q,
			t,
			&m
		)) { goto try_again; }

		lua_createtable(L, 0, 5); // { <match> }
		lua_pushnumber(L, m.id); lua_setfield(L, -2, "id"); // { <match> }
		lua_pushnumber(L, m.pattern_index); lua_setfield(L, -2, "pattern_index"); // { <match> }
		lua_pushnumber(L, m.capture_count); lua_setfield(L, -2, "capture_count"); // { <match> }
		lua_createtable(L, m.capture_count, m.capture_count); // { <match> }, { <arraymap> }
		for (uint16_t i = 0; i < m.capture_count; ++i) {
			push_lua_node(
				L, parent_idx,
				m.captures[i].node,
				c->q->lang
			); // {<match>}, {<arraymap>}, <Node>
			lua_pushvalue(L, -1); // {<match>}, {<arraymap>}, <Node>, <Node>
			lua_rawseti(L, -3, i+1); // {<match>}, {<arraymap> <Node>}, <Node>
			uint32_t len;
			const char *name = ts_query_capture_name_for_id(c->q->q, i, &len);
			lua_setfield(L, -2, name); // {<match>}, {<arraymap> <Node>, [name]=<Node>}
		}
		lua_setfield(L, -2, "captures"); // {<match> captures=<arraymap>}
		return 1;
	}
	return 0;
}

static int lua_query_capture(lua_State *L) {
	// stack: Query, Node, Cursor
	struct LuaTSQuery *const q = get_lua_query(L, lua_upvalueindex(1));
	TSQueryCursor *c = get_query_cursor(L, lua_upvalueindex(3));
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	struct LuaTSTree *const t = get_lua_tree(L, -1);
	TSQueryMatch m;
	uint32_t capture_index;

try_again:
	if (ts_query_cursor_next_capture(c, &m, &capture_index)) {
		if (!do_predicates(
			L,
			lua_upvalueindex(1),
			q->q,
			t,
			&m
		)) { goto try_again; }

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

/* @teal-export Query.match: function(Query, Node): function(): Match [[
   Iterate over the matches of a given query

   The match object is a record populated with all the information given by treesitter
   <pre>
   type Query.Match = record
      id: number
      pattern_index: number
      capture_count: number
      captures: {string|number:Node}
   end
   </pre>

   Example:
   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for match in q:match(node) do
      print(match.captures.my_match)
   end
   </pre>
]]*/
static int lua_query_match_factory(lua_State *L) {
	lua_settop(L, 2);
	struct LuaTSQuery *const q = get_lua_query(L, 1);
	TSNode n = get_node(L, 2);
	TSQueryCursor *c = ts_query_cursor_new();
	struct LuaTSQueryCursor *lc = lua_newuserdata(L, sizeof(struct LuaTSQueryCursor));
	setmetatable(L, LUA_TSQUERYCURSOR_METATABLE);
	lc->c = c;
	lc->q = q;
	ts_query_cursor_exec(c, q->q, n);
	lua_pushcclosure(L, lua_query_match, 3);
	return 1;
}

/* @teal-export Query.capture: function(Query, Node): function(): Node, string [[
   Iterate over the captures of a given query in <code>Node</code>, <code>name</code> pairs

   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for capture, name in q:capture(node) do
      print(capture, name) -- => (comment), "my_match"
   end
   </pre>
]]*/
static int lua_query_capture_factory(lua_State *L) {
	lua_settop(L, 2);
	struct LuaTSQuery *const q = get_lua_query(L, 1);
	TSNode n = get_node(L, 2);
	TSQueryCursor *c = ts_query_cursor_new();
	struct LuaTSQueryCursor *lc = lua_newuserdata(L, sizeof(struct LuaTSQueryCursor));
	setmetatable(L, LUA_TSQUERYCURSOR_METATABLE);
	lc->c = c;
	lc->q = q;
	ts_query_cursor_exec(c, q->q, n);
	lua_pushcclosure(L, lua_query_capture, 3); // prevent the node + query from being gc'ed
	return 1;
}

/* @teal-export Query.with: function(Query, {string:function(...: string): any...}): Query [[
   Creates a new query equipped with predicates defined in the <code>{string:function}</code> map given

   Predicates that end in a <code>'?'</code> character will be seen as conditions that must be met for the pattern to be matched.
   Predicates that don't will be seen just as functions to be executed given the matches provided.

   Additionally, you will not have access to the return values of these functions, if you'd like to keep the results of a computation, make your functions have side-effects to write somewhere you can access.

   By default the following predicates are provided.
      <code> (#eq? ...) </code> will match if all arguments provided are equal
      <code> (#match? text pattern) </code> will match the provided <code>text</code> matches the given <code>pattern</code>. Matches are determined by Lua's standard <code>string.match</code> function.
      <code> (#find? text substring) </code> will match if <code>text</code> contains <code>substring</code>. The substring is found with Lua's standard <code>string.find</code>, but the search always starts from the beginning, and pattern matching is disabled. This is equivalent to <code>string.find(text, substring, 0, true)</code>

   Example:
   The following snippet will match lua functions that have a single LDoc/EmmyLua style comment above them
   <pre>
   local parser = ltreesitter.require("lua")

   -- grab a node to query against
   local root_node = parser:parse_string[[
      ---@Doc this does stuff
      local function stuff_doer()
         do_stuff()
      end
   ]]:root()

   for match in parser
      :query[[(
         (comment) @the-comment
         .
         (function_definition
            (function_name) @the-function-name)
         (#is_doc_comment? @the-comment)
      )]]
      :with{
         ["is_doc_comment?"] = function(str)
            return str:sub(1, 4) == "---@"
         end
      }
      :match(root_node)
   do
      print("Function: " .. match.captures["the-function-name"] .. " has documentation")
      print("   " .. match.captures["the-comment"])
   end
   </pre>
]]*/

static int lua_query_copy_with_predicates(lua_State *L) {
	lua_settop(L, 2); // <Orig>, <Predicates>
	push_registry_query_predicate_table(L); // <Orig>, <Predicates>, { <RegistryPredicates> }
	push_query_copy(L, 1); // <Orig>, <Predicates>, { <RegistryPredicates> }, <Copy>
	lua_pushvalue(L, -1); // <Orig>, <Predicates>, { <RegistryPredicates> }, <Copy>, <Copy>
	lua_insert(L, -3); // <Orig>, <Predicates>, <Copy>, { <RegistryPredicates> }, <Copy>
	lua_pushvalue(L, 2); // <Orig>, <Predicates>, <Copy>, { <RegistryPredicates> }, <Copy>, <Predicates>
	lua_settable(L, -3); // <Orig>, <Predicates>, <Copy>, { <RegistryPredicates> }
	lua_pop(L, 1); // <Orig>, <Predicates>, <Copy>
	return 1;
}

/* @teal-export Query.exec: function(Query, Node) [[
   Runs a query. That's it. Nothing more, nothing less.
   This is intended to be used with the <code>Query.with</code> method and predicates that have side effects,
   i.e. for when you would use Query.match or Query.capture, but do nothing in the for loop.

   <pre>
   local parser = ltreesitter.require("teal")

   -- grab a node to query against
   local root_node = parser:parse_string[[
   local x: string = "foo"
   local y: string = "bar"
   ]]:root()

   parser
      :query[[(
         (var_declaration
            (var) @var-name
            (string) @value)
         (#set! @var-name @value)
      )]]
      :with{["set!"] = function(a, b) _G[a] = b:sub(2, -2) end}
      :exec(root_node)

   print(x) -- => foo
   print(y) -- => bar

   </pre>

   If you'd like to interact with the matches/captures of a query, see the Query.match and Query.capture iterators
]]*/
static int lua_query_exec(lua_State *L) {
	TSQuery *const q = get_query(L, 1);
	TSNode n = get_node(L, 2);

	TSQueryCursor *c = ts_query_cursor_new();

	push_parent(L, 2);
	struct LuaTSTree *const t = get_lua_tree(L, -1);

	TSQueryMatch m;
	ts_query_cursor_exec(c, q, n);
	while (ts_query_cursor_next_match(c, &m)) {
		do_predicates(L, 1, q, t, &m);
	}

	return 0;
}

/* {{{ Default Predicates */

static int eq_predicate(lua_State *L) {
	const int num_args = lua_gettop(L);
	if (num_args < 2) {
		luaL_error(L, "predicate eq? expects 2 or more arguments, got %d", num_args);
	}
	const char *a = luaL_checkstring(L, 1);
	const char *b;

	for (int i = 2; i <= num_args; ++i) {
		b = luaL_checkstring(L, i);
		if (strcmp(a, b) != 0) {
			lua_pushboolean(L, false);
			return 1;
		};
		a = b;
	}

	lua_pushboolean(L, true);
	return 1;
}

static int match_predicate(lua_State *L) {
	const int num_args = lua_gettop(L);
	if (num_args != 2) {
		luaL_error(L, "predicate match? expects exactly 2 arguments, got %d", num_args);
	}
	luaopen_string(L);
	lua_getfield(L, -1, "match");
	lua_remove(L, -2);

	lua_insert(L, -3);
	lua_call(L, 2, 1);

	return 1;
}

static int find_predicate(lua_State *L) {
	const int num_args = lua_gettop(L);
	if (num_args != 2) {
		luaL_error(L, "predicate find? expects exactly 2 arguments, got %d", num_args);
	}
	luaopen_string(L);
	lua_getfield(L, -1, "find");
	lua_remove(L, -2);

	lua_insert(L, -3);
	lua_pushnumber(L, 0);
	lua_pushboolean(L, true);
	lua_call(L, 4, 1);

	return 1;
}

static const luaL_Reg default_query_predicates[] = {
	{"eq?", eq_predicate},
	{"match?", match_predicate},
	{"find?", find_predicate},
	{NULL, NULL}
};
/* }}}*/

static const luaL_Reg query_methods[] = {
	{"pattern_count", lua_query_pattern_count},
	{"capture_count", lua_query_capture_count},
	{"string_count", lua_query_string_count},
	{"match", lua_query_match_factory},
	{"capture", lua_query_capture_factory},
	{"with", lua_query_copy_with_predicates},
	{"exec", lua_query_exec},
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

enum DLOpenError {
	DLERR_NONE,
	DLERR_DLOPEN,
	DLERR_BUFLEN,
	DLERR_DLSYM,
};

static enum DLOpenError try_dlopen(struct LuaTSParser *p, const char *parser_file, const char *lang_name) {
	void *handle = open_dynamic_lib(parser_file);
	if (!handle) {
		return DLERR_DLOPEN;
	}
	char buf[128];
	if (snprintf(buf, sizeof(buf) - sizeof(TREE_SITTER_SYM), TREE_SITTER_SYM "%s", lang_name) == 0) {
		close_dynamic_lib(handle);
		return DLERR_BUFLEN;
	}

	TSLanguage *(*tree_sitter_lang)(void);

#ifdef _WIN32
	tree_sitter_lang = (__cdecl TSLanguage *(*)(void))GetProcAddress(handle, buf);
#else
	// ISO C complains about casting void * to a function pointer
	*(void **) (&tree_sitter_lang) = dlsym(handle, buf);
#endif

	if (!tree_sitter_lang) {
		close_dynamic_lib(handle);
		return DLERR_DLSYM;
	}
	TSParser *parser = ts_parser_new();
	const TSLanguage *lang = tree_sitter_lang();
	ts_parser_set_language(parser, lang);

	p->dlhandle = handle;
	p->lang = lang;
	p->parser = parser;
	return DLERR_NONE;
}

/* @teal-export load: function(file_name: string, language_name: string): Parser, string [[
   Load a parser from a given file

   Keep in mind that this includes the <code>.so</code> or <code>.dll</code> extension

   On Unix this uses dlopen, on Windows this uses LoadLibrary so if a path without a path separator is given, these functions have their own path's that they will search for your file in.
   So if in doubt use a file path like
   <pre>
   local my_parser = ltreesitter.load("./my_parser.so", "my_language")
   </pre>
]] */
static int lua_load_parser(lua_State *L) {
	lua_settop(L, 2);
	const char *parser_file = luaL_checkstring(L, 1);
	const char *lang_name = luaL_checkstring(L, 2);

	struct LuaTSParser *const p = lua_newuserdata(L, sizeof(struct LuaTSParser));

	switch (try_dlopen(p, parser_file, lang_name)) {
	case DLERR_NONE:
		break;
	case DLERR_DLSYM:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to find symbol %s%s", TREE_SITTER_SYM, lang_name);
		return 2;
	case DLERR_DLOPEN:
		lua_pushnil(L);
#ifdef _WIN32
		lua_pushstring(L, "Error in LoadLibrary");
#else
		lua_pushstring(L, dlerror());
#endif
		return 2;
	case DLERR_BUFLEN:
		lua_pushnil(L);
		lua_pushfstring(L, "Unable to copy language name '%s' into buffer", lang_name);
		return 2;
	}

	setmetatable(L, LUA_TSPARSER_METATABLE);
	return 1;
}

/* @teal-export require: function(library_file_name: string, language_name: string): Parser [[
   Search <code>~/.tree-sitter/bin</code> and <code>package.cpath</code> for a parser with the filename <code>library_file_name.so</code> (or <code>.dll</code> on Windows) and try to load the symbol <code>tree_sitter_'language_name'</code>
   <code>language_name</code> is optional and will be set to <code>library_file_name</code> if not provided.

   So if you want to load a Lua parser from a file named <code>lua.so</code> then use <code>ltreesitter.require("lua")</code>
   But if you want to load a Lua parser from a file named <code>parser.so</code> then use <code>ltreesitter.require("parser", "lua")</code>

   Like the regular <code>require</code>, this will error if the parser is not found or the symbol couldn't be loaded. Use either <code>pcall</code> or <code>ltreesitter.load</code> to not error out on failure.

   <pre>
   local my_parser = ltreesitter.require("my_language")
   my_parser:parse_string(...)
   -- etc.
   </pre>
]] */
static inline void buf_add_str(luaL_Buffer *b, const char *s) { luaL_addlstring(b, s, strlen(s)); }
static int lua_require_parser(lua_State *L) {

	// grab args
	lua_settop(L, 2);
	const char *so_name = luaL_checkstring(L, 1);
	const size_t so_len = strlen(so_name);
	const char *lang_name = luaL_optstring(L, 2, so_name);
	const size_t lang_len = strlen(lang_name);

	const char *user_home = getenv("HOME");
	if (user_home) {
		lua_pushfstring(L, "%s/.tree-sitter/bin/?." DL_EXT ";", user_home);
	}

	// prepend ~/.tree-sitter/bin/?.so to package.cpath
	lua_getglobal(L, "package"); // lang_name, <ts path>, package
	lua_getfield(L, -1, "cpath"); // lang_name, <ts path>, package, package.cpath
	lua_remove(L, -2); // lang_name, <ts path>, package.cpath

	if (user_home) {
		lua_concat(L, 2);
	}

	// lang_name, package.cpath
	const char *cpath = lua_tostring(L, -1);
	const size_t buf_size = strlen(cpath);
	char *buf = malloc(sizeof(char) * (buf_size + lang_len));

	// create parser, prepare strbuffer for error message if we can't load it
	struct LuaTSParser *const p = lua_newuserdata(L, sizeof(struct LuaTSParser));
	luaL_Buffer b;

	luaL_buffinit(L, &b);
	buf_add_str(&b, "Unable to load parser for ");
	buf_add_str(&b, lang_name);

	// Do an imitation of a package.searchpath
	//	Searchpath will just return the first path which we may be able to open,
	//	but it may not have the symbol we want, so we should keep searching afterward
	ssize_t j = 0;
	for (size_t i = 0; i <= buf_size; ++i, ++j) {
		// cpath doesn't necessarily end with a ; so lets pretend it does
		char c;
		if (i == buf_size) c = ';';
		else c = cpath[i];

		switch (c) {
		case '?':
			for (size_t k = 0; k < so_len; ++k, ++j) {
				buf[j] = so_name[k];
			}
			--j;
			break;
		case ';': {
			buf[j] = '\0';

			switch (try_dlopen(p, buf, lang_name)) {
			case DLERR_NONE:
				luaL_pushresult(&b);
				free(buf);
				lua_pop(L, 1);
				setmetatable(L, LUA_TSPARSER_METATABLE);
				return 1;
			case DLERR_DLSYM:
				buf_add_str(&b, "\n\tFound ");
				luaL_addlstring(&b, buf, j);
				buf_add_str(&b, ":\n\t\tunable to find symbol " TREE_SITTER_SYM);
				buf_add_str(&b, TREE_SITTER_SYM);
				buf_add_str(&b, lang_name);
				break;
			case DLERR_DLOPEN:
				buf_add_str(&b, "\n\tTried ");
				luaL_addlstring(&b, buf, j);
#ifdef _WIN32
				buf_add_str(&b, ":\n\t\tLoadLibrary error");
#else
				buf_add_str(&b, ":\n\t\tdlopen error ");
				buf_add_str(&b, dlerror());
#endif
				break;
			case DLERR_BUFLEN:
				buf_add_str(&b, "\n\tUnable to copy langauge name '");
				buf_add_str(&b, lang_name);
				buf_add_str(&b, "' into buffer");
				goto err_cleanup;
			}

			j = -1;
			break;
		}
		default:
			buf[j] = cpath[i];
			break;
		}
	}

err_cleanup:
	free(buf);

	luaL_pushresult(&b);
	return lua_error(L);
}

static int lua_parser_gc(lua_State *L) {
	struct LuaTSParser *lp = luaL_checkudata(L, 1, LUA_TSPARSER_METATABLE);
#ifdef LOG_GC
	printf("Parser %p is being garbage collected\n", lp);
#endif
	ts_parser_delete(lp->parser);

	close_dynamic_lib(lp->dlhandle);
	return 0;
}

/* @teal-export Parser.parse_string: function(Parser, string, Tree): Tree [[
   Uses the given parser to parse the string

   If <code>Tree</code> is provided then it will be used to create a new updated tree
   (but it is the responsibility of the programmer to make the correct <code>Tree:edit</code> calls)

   Could return <code>nil</code> if the parser has a timeout
]] */
static int lua_parser_parse_string(lua_State *L) {
	lua_settop(L, 3);
	struct LuaTSParser *p = get_lua_parser(L, 1);
	size_t len;
	const char *str = luaL_checklstring(L, 2, &len);
	struct LuaTSTree *old_lua_tree;
	TSTree *old_tree;
	if (lua_type(L, 3) == LUA_TNIL) {
		old_tree = NULL;
	} else {
		old_lua_tree = get_lua_tree(L, 3);
		old_tree = old_lua_tree->t;
	}
	TSTree *tree = ts_parser_parse_string(
		p->parser,
		old_tree,
		str,
		len
	);
	if (!tree) {
		lua_pushnil(L);
		return 1;
	}
	push_lua_tree(L, tree, p->lang, str, len, false);
	return 1;
}

enum ReadError {
	READERR_NONE,
	READERR_PCALL,
	READERR_TYPE,
};
struct CallInfo {
	lua_State *L;
	struct {
		size_t len;
		size_t real_len;
		char *str;
	} string_builder;
	enum ReadError read_error;
};
static const char *lua_parser_read(void *payload, uint32_t byte_index, TSPoint position, uint32_t *bytes_read) {
	struct CallInfo *const i = payload;
	lua_State *const L = i->L;
	lua_pushvalue(L, -1); // grab a copy of the function
	lua_pushnumber(L, byte_index);

	lua_newtable(L); // byte_index, {}
	lua_pushnumber(L, position.row); // byte_index, {}, row
	lua_setfield(L, -2, "row"); // byte_index, { row = row }
	lua_pushnumber(L, position.column); // byte_index, { row = row }, column
	lua_setfield(L, -2, "column"); // byte_index, { row = row, column = column }

	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		i->read_error = READERR_PCALL;
		*bytes_read = 0;
		return NULL;
	}

	if (lua_isnil(L, -1)) {
		*bytes_read = 0;
		return NULL;
	}

	if (lua_type(L, -1) != LUA_TSTRING) {
		i->read_error = READERR_TYPE;
		*bytes_read = 0;
		return NULL;
	}

	const char *read_str = lua_tolstring(L, -1, (size_t *)bytes_read);
	while (i->string_builder.real_len < byte_index + *bytes_read) {
		i->string_builder.real_len *= 2;
		i->string_builder.str = realloc(i->string_builder.str, i->string_builder.real_len);
		if (!i->string_builder.str) {
			ALLOC_FAIL(L);
			*bytes_read = 0;
			return NULL;
		}
	}
	memcpy(&i->string_builder.str[byte_index], read_str, *bytes_read);
	if (byte_index + *bytes_read > i->string_builder.len) {
		i->string_builder.len = byte_index + *bytes_read;
	}

	lua_pop(L, 1);

	return read_str;
}

/* @teal-export Parser.parse_with: function(Parser, reader: function(number, Point): (string), old_tree: Tree) [[
   <code>reader</code> should be a function that takes a byte index
   and a <code>Point</code> and returns the text at that point. The
   function should return either <code>nil</code> or an empty string
   to signal that there is no more text.

   A <code>Tree</code> can be provided to reuse parts of it for parsing,
   provided the <code>Tree:edit</code> has been called previously
]]*/
static int lua_parser_parse_with(lua_State *L) {
	lua_settop(L, 3);
	struct LuaTSParser *const p = get_lua_parser(L, 1);
	TSTree *old_tree = NULL;
	if (!lua_isnil(L, 3)) {
		old_tree = get_tree(L, 3);
	}
	lua_pop(L, 1);
	struct CallInfo payload = {
		.L = L,
		.read_error = READERR_NONE,
		// The *TSTree structure is opaque so we don't have access to how it internally gets the source of the string that it parsed, so we manage it ourselves here.
		.string_builder = {
			.len = 0,
			.real_len = 64,
			.str = malloc(sizeof(char) * 64),
		},
	};
	if (!payload.string_builder.str) {
		return ALLOC_FAIL(L);
	}

	TSInput input = (TSInput){
		.read = lua_parser_read,
		.payload = &payload,
		.encoding = TSInputEncodingUTF8,
	};


	// TODO: allow passing in the old tree
	TSTree *t = ts_parser_parse(p->parser, old_tree, input);

	switch (payload.read_error) {
		case READERR_PCALL:
			return luaL_error(L, "read error: Provided function errored: %s", lua_tostring(L, -1));
		case READERR_TYPE:
			return luaL_error(L, "read error: Provided function returned %s (expected string)", lua_typename(L, lua_type(L, -1)));

		case READERR_NONE: default: break;
	}

	if (!t) {
		lua_pushnil(L);
		return 1;
	}
	push_lua_tree(L, t, p->lang, payload.string_builder.str, payload.string_builder.len, true);

	return 1;
}

/* @teal-export Parser.set_timeout: function(Parser, number) [[
   Sets how long the parser is allowed to take in microseconds
]] */
static int lua_parser_set_timeout(lua_State *L) {
	TSParser *const p = get_parser(L, 1);
	const lua_Number n = luaL_checknumber(L, 2);
	luaL_argcheck(L, n >= 0, 2, "expected non-negative number");
	ts_parser_set_timeout_micros(p, (uint64_t)n);
	return 0;
}

static const luaL_Reg parser_methods[] = {
	{"parse_string", lua_parser_parse_string},
	{"parse_with", lua_parser_parse_with},
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
static int lua_tree_root(lua_State *L) {
	struct LuaTSTree *const t = get_lua_tree(L, 1);
	lua_newuserdata(L, sizeof(struct LuaTSNode));
	push_lua_node(L, 1, ts_tree_root_node(t->t), t->lang);
	return 1;
}

static int lua_tree_to_string(lua_State *L) {
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
static int lua_tree_copy(lua_State *L) {
	struct LuaTSTree *t = get_lua_tree(L, 1);
	struct LuaTSTree *const t_copy = lua_newuserdata(L, sizeof(struct LuaTSTree));
	t_copy->t = ts_tree_copy(t->t);
	if (t->own_str) {
		t_copy->src = malloc(sizeof(char) * t->src_len + 1);
		if (!t_copy->src) {
			ALLOC_FAIL(L);
			ts_tree_delete(t_copy->t);
			return 0;
		}
		memcpy((char *)t_copy->src, t->src, t->src_len);
		t_copy->own_str = true;
	} else {
		t_copy->src = t->src;
		t_copy->src_len = t->src_len;
		t_copy->own_str = false;
	}
	setmetatable(L, LUA_TSTREE_METATABLE);
	return 1;
}

static inline bool is_non_negative(lua_State *L, int i) { return lua_tonumber(L, i) >= 0; }

static inline int getfield_type(lua_State *L, int idx, const char *field_name) {
	// TODO: make a macro or something for this for different lua versions
	/*int actual_type = */lua_getfield(L, idx, field_name);
	return lua_type(L, -1);
}

static void expect_arg_field(lua_State *L, int idx, const char *field_name, int expected_type) {
	const int actual_type = getfield_type(L, idx, field_name);
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

static void expect_nested_arg_field(lua_State *L, int idx, const char *parent_name, const char *field_name, int expected_type) {
	const int actual_type = getfield_type(L, idx, field_name);
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

// Maybe make this Tree.Edit?
/* @teal-inline [[
   record TreeEdit
      start_byte: number
      old_end_byte: number
      new_end_byte: number

      start_point: Point
      old_end_point: Point
      new_end_point: Point
   end
]]*/
/* @teal-export Tree.edit: function(Tree, TreeEdit) [[
   Create an edit to the given tree
]] */
static int lua_tree_edit(lua_State *L) {
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

static int lua_tree_gc(lua_State *L) {
	struct LuaTSTree *t = get_lua_tree(L, 1);
#ifdef LOG_GC
	printf("Tree %p is being garbage collected\n", t);
#endif
	if (t->own_str) {
#ifdef LOG_GC
		printf("Tree %p owns its string %p, collecting that too...\n", t, t->src);
#endif
		free((char *)t->src);
	}
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

static struct LuaTSTreeCursor *push_lua_tree_cursor(lua_State *L, int parent_idx, const TSLanguage *lang, TSNode n) {
	struct LuaTSTreeCursor *c = lua_newuserdata(L, sizeof(struct LuaTSTreeCursor));
	c->c = ts_tree_cursor_new(n);
	c->lang = lang;
	setmetatable(L, LUA_TSTREECURSOR_METATABLE);
	set_parent(L, lua_gettop(L), parent_idx);
	return c;
}

/* @teal-export Node.create_cursor: function(Node): Cursor [[
   Create a new cursor at the given node
]] */
static int lua_tree_cursor_create(lua_State *L) {
	lua_settop(L, 1);
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	push_lua_tree_cursor(L, 2, n->lang, n->n);
	return 1;
}

/* @teal-export Cursor.current_node: function(Cursor): Node [[
   Get the current node under the cursor
]] */
static int lua_tree_cursor_current_node(lua_State *L) {
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
static int lua_tree_cursor_current_field_name(lua_State *L) {
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
static int lua_tree_cursor_reset(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	TSNode n = get_node(L, 2);
	ts_tree_cursor_reset(&c->c, n);
	return 0;
}

/* @teal-export Cursor.goto_parent: function(Cursor): boolean [[
   Position the cursor at the parent of the current node
]] */
static int lua_tree_cursor_goto_parent(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_parent(&c->c));
	return 1;
}

/* @teal-export Cursor.goto_next_sibling: function(Cursor): boolean [[
   Position the cursor at the sibling of the current node
]] */
static int lua_tree_cursor_goto_next_sibling(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(&c->c));
	return 1;
}

/* @teal-export Cursor.goto_first_child: function(Cursor): boolean [[
   Position the cursor at the first child of the current node
]] */
static int lua_tree_cursor_goto_first_child(lua_State *L) {
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, 1);
	lua_pushboolean(L, ts_tree_cursor_goto_first_child(&c->c));
	return 1;
}

static int lua_tree_cursor_gc(lua_State *L) {
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
static int lua_node_type(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushstring(L, ts_node_type(n));
	return 1;
}

/* @teal-export Node.start_byte: function(Node): number [[
   Get the byte of the source string that the given node starts at
]] */
static int lua_node_start_byte(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_start_byte(n));
	return 1;
}

/* @teal-export Node.end_byte: function(Node): number [[
   Get the byte of the source string that the given node ends at
]] */
static int lua_node_end_byte(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_end_byte(n));
	return 1;
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
static int lua_node_start_point(lua_State *L) {
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
static int lua_node_end_point(lua_State *L) {
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
static int lua_node_is_named(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushboolean(L, ts_node_is_named(n));
	return 1;
}

/* @teal-export Node.is_missing: function(Node): boolean [[
   Get whether or not the current node is missing
]] */
static int lua_node_is_missing(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushboolean(L, ts_node_is_missing(n));
	return 1;
}

/* @teal-export Node.is_extra: function(Node): boolean [[
   Get whether or not the current node is missing
]] */
static int lua_node_is_extra(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushboolean(L, ts_node_is_extra(n));
	return 1;
}

/* @teal-export Node.child: function(Node, idx: number): Node [[
   Get the node's idx'th child (0-indexed)
]] */
static int lua_node_child(lua_State *L) {
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
static int lua_node_child_count(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_child_count(n));
	return 1;
}

/* @teal-export Node.named_child: function(Node, idx: number): Node [[
   Get the node's idx'th named child (0-indexed)
]] */
static int lua_node_named_child(lua_State *L) {
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
static int lua_node_named_child_count(lua_State *L) {
	TSNode n = get_node(L, 1);
	lua_pushnumber(L, ts_node_named_child_count(n));
	return 1;
}

static int lua_node_children_iterator(lua_State *L) {
	const bool b = lua_toboolean(L, lua_upvalueindex(2));
	if (!b) { return 0; }

	lua_settop(L, 0);
	struct LuaTSTreeCursor *const c = get_lua_tree_cursor(L, lua_upvalueindex(1));

	const TSNode n = ts_tree_cursor_current_node(&c->c);
	push_parent(L, lua_upvalueindex(1));
	push_lua_node(
		L, 1,
		n, c->lang
	);

	lua_pushboolean(L, ts_tree_cursor_goto_next_sibling(&c->c));
	lua_replace(L, lua_upvalueindex(2));

	return 1;
}

static int lua_node_named_children_iterator(lua_State *L) {
	lua_settop(L, 0);
	struct LuaTSNode *const n = get_lua_node(L, lua_upvalueindex(1));

	const uint32_t idx = lua_tonumber(L, lua_upvalueindex(3));
	lua_pushnumber(L, idx + 1);
	lua_replace(L, lua_upvalueindex(3));

	if (idx >= ts_node_named_child_count(n->n)) {
		lua_pushnil(L);
	} else {
		// TODO: make this less odd
		lua_pushvalue(L, lua_upvalueindex(2));
		push_lua_node(
			L, 1,
			ts_node_named_child(n->n, idx),
			n->lang
		);
		lua_remove(L, -2);
	}

	return 1;
}

/* @teal-export Node.children: function(Node): function(): Node [[
   Iterate over a node's children
]] */
static int lua_node_children(lua_State *L) {
	lua_settop(L, 1);
	struct LuaTSNode *const n = get_lua_node(L, 1);
	push_parent(L, 1);
	struct LuaTSTreeCursor *const c = push_lua_tree_cursor(L, 2, n->lang, n->n);
	const bool b = ts_tree_cursor_goto_first_child(&c->c);
	lua_pushboolean(L, b);
	lua_pushcclosure(L, lua_node_children_iterator, 2);
	return 1;
}

/* @teal-export Node.named_children: function(Node): function(): Node [[
   Iterate over a node's named children
]] */
static int lua_node_named_children(lua_State *L) {
	get_node(L, 1);
	push_parent(L, 1);
	lua_pushnumber(L, 0);
	
	lua_pushcclosure(L, lua_node_named_children_iterator, 3);
	return 1;
}

/* @teal-export Node.next_sibling: function(Node): Node [[
   Get a node's next sibling
]] */
static int lua_node_next_sibling(lua_State *L) {
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
static int lua_node_prev_sibling(lua_State *L) {
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
static int lua_node_next_named_sibling(lua_State *L) {
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
static int lua_node_prev_named_sibling(lua_State *L) {
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

static int lua_node_string(lua_State *L) {
	TSNode n = get_node(L, 1);
	char *s = ts_node_string(n);
	lua_pushstring(L, s);
	free(s);
	return 1;
}

static int lua_node_eq(lua_State *L) {
	TSNode n1 = get_node(L, 1);
	TSNode n2 = get_node(L, 2);
	lua_pushboolean(L, ts_node_eq(n1, n2));
	return 1;
}

/* @teal-export Node.name: function(Node): string [[
   Returns the name of a given node
   <pre>
   print(node) -- => (comment)
   print(node:name()) -- => comment
   </pre>
]] */

static int lua_node_name(lua_State *L) {
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
static int lua_node_child_by_field_name(lua_State *L) {
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

/* @teal-export Node.source: function(Node): string [[
   Get the substring of the source that was parsed to create <code>Node</code>
]]*/
static int lua_node_get_source_str(lua_State *L) {
	lua_settop(L, 1);
	TSNode n = get_node(L, 1);
	uint32_t start = ts_node_start_byte(n);
	uint32_t end = ts_node_end_byte(n);
	push_parent(L, 1);
	struct LuaTSTree *const t = get_lua_tree(L, 2);
	lua_pushlstring(L, t->src + start, end - start);
	return 1;
}

static const luaL_Reg node_methods[] = {
	{"child", lua_node_child},
	{"child_by_field_name", lua_node_child_by_field_name},
	{"child_count", lua_node_child_count},
	{"children", lua_node_children},
	{"create_cursor", lua_tree_cursor_create},
	{"end_byte", lua_node_end_byte},
	{"end_point", lua_node_end_point},
	{"is_extra", lua_node_is_extra},
	{"is_missing", lua_node_is_missing},
	{"is_named", lua_node_is_named},
	{"name", lua_node_name},
	{"named_child", lua_node_named_child},
	{"named_child_count", lua_node_named_child_count},
	{"named_children", lua_node_named_children},
	{"next_named_sibling", lua_node_next_named_sibling},
	{"next_sibling", lua_node_next_sibling},
	{"prev_named_sibling", lua_node_prev_named_sibling},
	{"prev_sibling", lua_node_prev_sibling},
	{"source", lua_node_get_source_str},
	{"start_byte", lua_node_start_byte},
	{"start_point", lua_node_start_point},
	{"type", lua_node_type},
	{NULL, NULL}
};
static const luaL_Reg node_metamethods[] = {
	{"__eq", lua_node_eq},
	{"__tostring", lua_node_string},
	{NULL, NULL}
};

/* }}}*/

static const luaL_Reg lib_funcs[] = {
	{"load", lua_load_parser},
	{"require", lua_require_parser},
	{"_get_registry_entry", push_registry_table},
	{NULL, NULL}
};

LUA_API int luaopen_ltreesitter(lua_State *L) {
	create_metatable(L, LUA_TSNODE_METATABLE, node_metamethods, node_methods);
	create_metatable(L, LUA_TSPARSER_METATABLE, parser_metamethods, parser_methods);
	create_metatable(L, LUA_TSQUERYCURSOR_METATABLE, query_cursor_metamethods, query_cursor_methods);
	create_metatable(L, LUA_TSQUERY_METATABLE, query_metamethods, query_methods);
	create_metatable(L, LUA_TSTREECURSOR_METATABLE, tree_cursor_metamethods, tree_cursor_methods);
	create_metatable(L, LUA_TSTREE_METATABLE, tree_metamethods, tree_methods);

	lua_pushlightuserdata(L, (void *)&registry_index); // void *
	// Do the registry dance
	lua_newtable(L); // {}
	lua_newtable(L); // {}, {}
	lua_newtable(L); // {}, {}, {}
#ifndef PREVENT_GC
	lua_pushstring(L, "k"); // {}, {}, {}, "k"
	lua_setfield(L, -2, "__mode"); // {}, {}, { __mode = "k" }
#endif
	lua_setmetatable(L, -2); // {}, { <metatable { __mode = "k" }> }
	lua_setfield(L, -2, "objects"); // { objects = { <metatable { __mode = "k" }> } }

	lua_newtable(L); // { objects = {...} }, {}
	luaL_setfuncs(L, default_query_predicates, 0); // { objects = {...} }, { ["eq?"] = <function>, ... }
	lua_setfield(L, -2, default_predicates_index); // { objects = {...}, predicates = {...} }

	lua_newtable(L); // { objects, predicates }, {}
	lua_newtable(L); // { objects, predicates }, {}, {}

#ifndef PREVENT_GC
	lua_pushstring(L, "v"); // { obj, pred }, {}, {}, "v"
	lua_setfield(L, -2, "__mode"); // { obj, pred }, {}, { __mode = "v" }
#endif

	lua_setmetatable(L, -2); // { obj, pred }, { <metatable { __mode = "v" }> }
	lua_setfield(L, -2, query_predicates_index); // { obj, pred, query_predicates = { <__mode = "v"> }}

	lua_settable(L, LUA_REGISTRYINDEX);

	libtable(L, lib_funcs); // { <lib> }
	lua_pushstring(L, version_str); // {}, version_str
	lua_setfield(L, -2, "version"); // { <lib>, version = version_str }

	return 1;
}
