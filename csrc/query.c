
#include <stddef.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "luautils.h"
#include "object.h"
#include <ltreesitter/node.h>
#include <ltreesitter/parser.h>
#include <ltreesitter/query_cursor.h>
#include <ltreesitter/tree.h>
#include <ltreesitter/types.h>

static const char *default_predicate_field = "default_predicates";
static const char *predicate_field = "predicates";

static void push_default_predicate_table(lua_State *L) {
	push_registry_field(L, default_predicate_field);
}

static void push_predicate_table(lua_State *L) {
	push_registry_field(L, predicate_field);
}

ltreesitter_Query *ltreesitter_check_query(lua_State *L, int idx) {
	return luaL_checkudata(L, idx, LTREESITTER_QUERY_METATABLE_NAME);
}

void handle_query_error(
    lua_State *L,
    TSQuery *q,
    uint32_t err_offset,
    TSQueryError err_type,
    const char *query_src) {
	if (q)
		return;
	char slice[16] = {0};
	strncpy(slice, &query_src[err_offset >= 10 ? err_offset - 10 : err_offset], 15);

#define CASE(typ, str)                                   \
	case typ:                                            \
		lua_pushfstring(L, str, slice, (int)err_offset); \
		break

	switch (err_type) {
		CASE(TSQueryErrorSyntax, "Query syntax error: around '%s' (at offset %d)");
		CASE(TSQueryErrorNodeType, "Query node type error: around '%s' (at offset %d)");
		CASE(TSQueryErrorField, "Query field error: around '%s' (at offset %d)");
		CASE(TSQueryErrorCapture, "Query capture error: around '%s' (at offset %d)");
		CASE(TSQueryErrorStructure, "Query structure error: around '%s' (at offset %d)");
	default:
		UNREACHABLE(L);
	}
#undef CASE

	lua_error(L);
}

void ltreesitter_push_query(
    lua_State *L,
    const TSLanguage *lang,
    const char *src,
    const size_t src_len,
    TSQuery *q,
    int parent_idx) {

	ltreesitter_Query *lq = lua_newuserdata(L, sizeof(struct ltreesitter_Query));
	setmetatable(L, LTREESITTER_QUERY_METATABLE_NAME);
	lua_pushvalue(L, -1);
	set_parent(L, parent_idx);
	lq->lang = lang;
	lq->src = src;
	lq->src_len = src_len;
	lq->query = q;
}

static void push_query_copy(lua_State *L, int query_idx) {
	ltreesitter_Query *orig = ltreesitter_check_query(L, query_idx);
	push_parent(L, query_idx); // <parent>

	uint32_t err_offset;
	TSQueryError err_type;
	TSQuery *q = ts_query_new(
	    orig->lang,
	    orig->src,
	    orig->src_len,
	    &err_offset,
	    &err_type);

	handle_query_error(L, q, err_offset, err_type, orig->src);
	const char *src_copy = str_ldup(orig->src, orig->src_len);
	if (!src_copy) {
		ALLOC_FAIL(L);
		return;
	}
	ltreesitter_push_query(L, orig->lang, src_copy, orig->src_len, q, -2); // <Parent>, <Query>

	lua_remove(L, -2); // <Query>
}

static int query_gc(lua_State *L) {
	ltreesitter_Query *q = ltreesitter_check_query(L, 1);
	free((char *)q->src);
	ts_query_delete(q->query);
	return 1;
}

static int query_pattern_count(lua_State *L) {
	TSQuery *q = ltreesitter_check_query(L, 1)->query;
	pushinteger(L, ts_query_pattern_count(q));
	return 1;
}
static int query_capture_count(lua_State *L) {
	TSQuery *q = ltreesitter_check_query(L, 1)->query;
	pushinteger(L, ts_query_capture_count(q));
	return 1;
}
static int query_string_count(lua_State *L) {
	TSQuery *q = ltreesitter_check_query(L, 1)->query;
	pushinteger(L, ts_query_string_count(q));
	return 1;
}

static void push_query_predicates(lua_State *L, int query_idx) {
	lua_pushvalue(L, query_idx); // <Query>
	push_predicate_table(L);     // <Query>, { <Predicates> }
	lua_insert(L, -2);           // { <Predicates> }, <Query>
	lua_gettable(L, -2);         // { <Predicates> }, <Predicate>
	lua_remove(L, -2);           // <Predicate>
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);                   // (nothing)
		push_default_predicate_table(L); // <Default Predicate>
	}
}

static bool do_predicates(
    lua_State *L,
    const int query_idx,
    const TSQuery *const q,
    const ltreesitter_Tree *const t,
    const TSQueryMatch *const m) {
	const uint32_t num_patterns = ts_query_pattern_count(q);
	for (uint32_t i = 0; i < num_patterns; ++i) {
		uint32_t num_steps;
		const TSQueryPredicateStep *const s = ts_query_predicates_for_pattern(q, i, &num_steps);
		bool is_question = false; // if a predicate is a question then the query should only match if it results in a truthy value
		/* TODO:
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
					if (func_name[len - 1] == '?') {
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
				if (lua_pcall(L, num_args, 1, 0) != LUA_OK) {
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

/* @teal-inline [[
   record Match
      id: integer
      pattern_index: integer
      capture_count: integer
      captures: {string|integer:Node}
   end
]] */

static int query_match(lua_State *L) {
	// upvalues: Query, Node, Cursor
	const int query_idx = lua_upvalueindex(1);
	ltreesitter_Query *const q = ltreesitter_check_query(L, query_idx);
	ltreesitter_QueryCursor *c = ltreesitter_check_query_cursor(L, lua_upvalueindex(3));
	TSQueryMatch m;
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	/* printf("checking if parent is tree\n"); */
	ltreesitter_Tree *const t = ltreesitter_check_tree(L, parent_idx, "check failed");
	/* printf("   ptr: %p\n", t); */

	do {
		if (!ts_query_cursor_next_match(c->query_cursor, &m))
			return 0;
	} while (!do_predicates(L, query_idx, q->query, t, &m));

	lua_createtable(L, 0, 5); // { <match> }
	pushinteger(L, m.id);
	lua_setfield(L, -2, "id"); // { <match> }
	pushinteger(L, m.pattern_index);
	lua_setfield(L, -2, "pattern_index"); // { <match> }
	pushinteger(L, m.capture_count);
	lua_setfield(L, -2, "capture_count");                 // { <match> }
	lua_createtable(L, m.capture_count, m.capture_count); // { <match> }, { <arraymap> }

	for (uint16_t i = 0; i < m.capture_count; ++i) {
		ltreesitter_push_node(
		    L, parent_idx,
		    m.captures[i].node);   // {<arraymap>}, <Node>
		lua_pushvalue(L, -1);      // {<arraymap>}, <Node>, <Node>
		lua_rawseti(L, -3, i + 1); // {<arraymap> <Node>}, <Node>
		uint32_t len;
		const char *name = ts_query_capture_name_for_id(c->query->query, m.captures[i].index, &len);
		if (len > 0) {
			lua_pushstring(L, name); // {<arraymap>}, <Node>, "name"
			lua_insert(L, -2);       // {<arraymap>}, "name", <Node>
			lua_rawset(L, -3);       // {<arraymap> <Node>, [name]=<Node>}
		} else {
			lua_pop(L, 1);
		}                            // {<arraymap>}
	}                                // { <match> }, { <arraymap> }
	lua_setfield(L, -2, "captures"); // {<match> captures=<arraymap>}
	return 1;
}

static int query_capture(lua_State *L) {
	// upvalues: Query, Node, Cursor
	const int query_idx = lua_upvalueindex(1);
	ltreesitter_Query *const q = ltreesitter_check_query(L, query_idx);
	TSQueryCursor *c = ltreesitter_check_query_cursor(L, lua_upvalueindex(3))->query_cursor;
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	ltreesitter_Tree *const t = ltreesitter_check_tree(L, -1, "parent check fail");
	TSQueryMatch m;
	uint32_t capture_index;

	do {
		if (!ts_query_cursor_next_capture(c, &m, &capture_index))
			return 0;
	} while (!do_predicates(L, query_idx, q->query, t, &m));

	ltreesitter_push_node(
	    L, parent_idx,
	    m.captures[capture_index].node);
	uint32_t len;
	const char *name = ts_query_capture_name_for_id(
			q->query, m.captures[capture_index].index, &len);
	lua_pushlstring(L, name, len);
	return 2;
}

/* @teal-export Query.match: function(Query, Node): function(): Match [[
   Iterate over the matches of a given query

   The match object is a record populated with all the information given by treesitter
   <pre>
   type Match = record
      id: integer
      pattern_index: integer
      capture_count: integer
      captures: {string|integer:Node}
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
static int query_match_factory(lua_State *L) {
	lua_settop(L, 2);
	ltreesitter_Query *const q = ltreesitter_check_query(L, 1);
	TSNode n = ltreesitter_check_node(L, 2)->node;
	TSQueryCursor *c = ts_query_cursor_new();
	ltreesitter_QueryCursor *lc = lua_newuserdata(L, sizeof(struct ltreesitter_QueryCursor));
	setmetatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
	lc->query_cursor = c;
	lc->query = q;
	ts_query_cursor_exec(c, q->query, n);
	lua_pushcclosure(L, query_match, 3);
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
static int query_capture_factory(lua_State *L) {
	lua_settop(L, 2);
	ltreesitter_Query *const q = ltreesitter_check_query(L, 1);
	TSNode n = ltreesitter_check_node(L, 2)->node;
	TSQueryCursor *c = ts_query_cursor_new();
	ltreesitter_QueryCursor *lc = lua_newuserdata(L, sizeof(struct ltreesitter_QueryCursor));
	setmetatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
	lc->query_cursor = c;
	lc->query = q;
	ts_query_cursor_exec(c, q->query, n);
	lua_pushcclosure(L, query_capture, 3); // prevent the node + query from being gc'ed
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

static int query_copy_with_predicates(lua_State *L) {
	lua_settop(L, 2);        // <Orig>, <Predicates>
	push_predicate_table(L); // <Orig>, <Predicates>, { <RegistryPredicates> }
	push_query_copy(L, 1);   // <Orig>, <Predicates>, { <RegistryPredicates> }, <Copy>
	lua_pushvalue(L, -1);    // <Orig>, <Predicates>, { <RegistryPredicates> }, <Copy>, <Copy>
	lua_insert(L, -3);       // <Orig>, <Predicates>, <Copy>, { <RegistryPredicates> }, <Copy>
	lua_pushvalue(L, 2);     // <Orig>, <Predicates>, <Copy>, { <RegistryPredicates> }, <Copy>, <Predicates>
	lua_settable(L, -3);     // <Orig>, <Predicates>, <Copy>, { <RegistryPredicates> }
	lua_pop(L, 1);           // <Orig>, <Predicates>, <Copy>
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
static int query_exec(lua_State *L) {
	TSQuery *const q = ltreesitter_check_query(L, 1)->query;
	TSNode n = ltreesitter_check_node(L, 2)->node;

	TSQueryCursor *c = ts_query_cursor_new();

	push_parent(L, 2);
	ltreesitter_Tree *const t = ltreesitter_check_tree_arg(L, -1);

	TSQueryMatch m;
	ts_query_cursor_exec(c, q, n);
	while (ts_query_cursor_next_match(c, &m)) {
		do_predicates(L, 1, q, t, &m);
	}

	return 0;
}

// Predicates
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
	pushinteger(L, 0);
	lua_pushboolean(L, true);
	lua_call(L, 4, 1);

	return 1;
}

static const luaL_Reg default_query_predicates[] = {
    {"eq?", eq_predicate},
    {"match?", match_predicate},
    {"find?", find_predicate},
    {NULL, NULL}};

void setup_predicate_tables(lua_State *L) {
	lua_newtable(L);
	setfuncs(L, default_query_predicates);
	set_registry_field(L, default_predicate_field);

	newtable_with_mode(L, "k");
	set_registry_field(L, predicate_field);
}

static const luaL_Reg query_methods[] = {
    {"pattern_count", query_pattern_count},
    {"capture_count", query_capture_count},
    {"string_count", query_string_count},
    {"match", query_match_factory},
    {"capture", query_capture_factory},
    {"with", query_copy_with_predicates},
    {"exec", query_exec},
    {NULL, NULL}};

static const luaL_Reg query_metamethods[] = {
    {"__gc", query_gc},
    {NULL, NULL}};

void ltreesitter_create_query_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_QUERY_METATABLE_NAME, query_metamethods, query_methods);
}
