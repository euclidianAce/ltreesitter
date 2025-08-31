
#include <inttypes.h>
#include <stddef.h>
#include <string.h>
#include <tree_sitter/api.h>

#include "luautils.h"
#include "node.h"
#include "object.h"
#include "query.h"
#include "query_cursor.h"
#include "tree.h"
#include "types.h"

static char const *default_predicate_field = "default_predicates";

static void push_default_predicate_table(lua_State *L) {
	push_registry_field(L, default_predicate_field);
}

static inline void offset_to_pos(char const *src, uint32_t offset, uint32_t *row, uint32_t *col) {
	*row = *col = 1;
	for (uint32_t i = 0; i <= offset; ++i) {
		if (src[i] == '\n') {
			++*row;
			*col = 1;
		} else {
			++*col;
		}
	}
}

bool query_handle_error(
	lua_State *L,
	TSQuery *q,
	uint32_t err_offset,
	TSQueryError err_type,
	char const *query_src,
	size_t query_src_len) {
	if (q)
		return true;
	char slice[16] = {0};
	{
		size_t start_idx = err_offset >= 10 ? err_offset - 10 : err_offset;
		size_t n_bytes = query_src_len - start_idx;
		if (n_bytes > 15)
			n_bytes = 15;
		memcpy(slice, &query_src[start_idx], n_bytes);
	}
	uint32_t row, col;
	offset_to_pos(query_src, err_offset, &row, &col);
	char buf[128];

#define CASE(typ, str)                                                                                                                                   \
	case typ:                                                                                                                                            \
		snprintf(buf, sizeof buf, "Query " str " error %" PRIu32 ":%" PRIu32 ": around '%s' (at byte offset %" PRIu32 ")", row, col, slice, err_offset); \
		lua_pushstring(L, buf);                                                                                                                          \
		break

	switch (err_type) {
	case TSQueryErrorNone:
		return true; // unreachable
		CASE(TSQueryErrorSyntax, "syntax");
		CASE(TSQueryErrorNodeType, "node");
		CASE(TSQueryErrorField, "field");
		CASE(TSQueryErrorCapture, "capture");
		CASE(TSQueryErrorStructure, "structure");
		CASE(TSQueryErrorLanguage, "language");
	}
#undef CASE

	lua_error(L);
	return false;
}

// src will be duplicated
void query_push(
	lua_State *L,
	char const *src,
	size_t const src_len,
	TSQuery *q,
	int kept_index) {
	kept_index = absindex(L, kept_index);
	TSQuery **lq = lua_newuserdata(L, sizeof(TSQuery *)); // query
	setmetatable(L, LTREESITTER_QUERY_METATABLE_NAME);

	bind_lifetimes(L, -1, kept_index); // this query keeps `kept_index` alive

	SourceText *source = source_text_push(L, src_len, src); // query, source text
	if (!source) {
		ALLOC_FAIL(L);
		return;
	}
	bind_lifetimes(L, -2, -1); // query keeps source text alive
	lua_pop(L, 1);             // query

	*lq = q;
}

static int query_gc(lua_State *L) {
	TSQuery *q = *query_assert(L, 1);
	ts_query_delete(q);
	return 1;
}

static int query_pattern_count(lua_State *L) {
	TSQuery *q = *query_assert(L, 1);
	pushinteger(L, ts_query_pattern_count(q));
	return 1;
}
static int query_capture_count(lua_State *L) {
	TSQuery *q = *query_assert(L, 1);
	pushinteger(L, ts_query_capture_count(q));
	return 1;
}
static int query_string_count(lua_State *L) {
	TSQuery *q = *query_assert(L, 1);
	pushinteger(L, ts_query_string_count(q));
	return 1;
}

// the capture table is just a map of @name -> Node
static void add_capture_to_table(
	lua_State *L,
	int table_index,

	char const *key,
	size_t key_len,

	int child_index,
	TSNode value) {
	lua_pushlstring(L, key, key_len);
	node_push(L, child_index, value);
	lua_rawset(L, table_index);
}

static void get_capture_from_table(
	lua_State *L,
	int table_index,
	char const *key,
	size_t key_len) {
	lua_pushlstring(L, key, key_len);
	lua_rawget(L, table_index);
}

// TODO: this function cannot handle upvalue indexes for query_idx, tree_idx, nor predicate_table_idx
static bool do_predicates(
	lua_State *L,
	int query_idx,
	TSQuery const *const q,
	int tree_idx,
	TSQueryMatch const *const m,
	int predicate_table_idx) {
	query_idx = absindex(L, query_idx);
	tree_idx = absindex(L, tree_idx);
	predicate_table_idx = absindex(L, predicate_table_idx);
	bool const predicates_provided = lua_type(L, predicate_table_idx) != LUA_TNIL;
	bool result = true;

	int const initial_stack_top = lua_gettop(L);

	// store captures as a map of {string:Node} where the keys are the
	// "@name"s and the values are the matched nodes
	lua_createtable(L, 0, m->capture_count);
	int capture_table_index = initial_stack_top + 1;
	for (uint32_t i = 0; i < m->capture_count; ++i) {
		TSQueryCapture capture = m->captures[i];
		uint32_t name_len;
		char const *name = ts_query_capture_name_for_id(q, capture.index, &name_len);

		add_capture_to_table(L, capture_table_index, name, name_len, tree_idx, capture.node);
	}

	// {
	// lua_pushvalue(L, capture_table_index);
	// lua_setglobal(L, "__captures");
	// luaL_dostring(L, "print(require'inspect'(__captures))");
	// }

	uint32_t num_steps;
	TSQueryPredicateStep const *const predicate_step = ts_query_predicates_for_pattern(q, m->pattern_index, &num_steps);

	{
		// count the max number of args we need to prep for
		int current_args = 0;
		int max_args = 0;
		for (uint32_t j = 0; j < num_steps; ++j) {
			switch (predicate_step[j].type) {
			case TSQueryPredicateStepTypeString:
			case TSQueryPredicateStepTypeCapture:
				current_args += 1;
				break;

			case TSQueryPredicateStepTypeDone:
				if (current_args > max_args)
					max_args = current_args;
				current_args = 0;
				break;
			}
		}
		if (!lua_checkstack(L, max_args))
			luaL_error(L, "Internal lua error, unable to handle %d arguments to predicate", max_args);
	}

	enum {
		questions,
		non_questions,
		end
	};
	for (int step = questions; step < end; ++step) {
		int num_args = 0;
		bool is_question = false; // if a predicate is a question then the query should only match if it results in a truthy value
		char const *func_name = NULL;
		for (uint32_t j = 0; j < num_steps; ++j) {
			switch (predicate_step[j].type) {
			case TSQueryPredicateStepTypeString: {
				// literal strings

				uint32_t len;
				char const *pred_name = ts_query_string_value_for_id(q, predicate_step[j].value_id, &len);
				if (!func_name) {
					bool predicate_found = false;
					if (predicates_provided) {
						lua_getfield(L, predicate_table_idx, pred_name);
						if (lua_isnil(L, -1))
							lua_pop(L, 1);
						else
							predicate_found = true;
					}
					if (!predicate_found) {
						push_default_predicate_table(L);
						predicate_found = getfield_type(L, -1, pred_name) != LUA_TNIL;
						lua_remove(L, -2);
					}
					if (!predicate_found)
						luaL_error(L, "Query doesn't have predicate '%s'", pred_name);
					func_name = pred_name;
					if (func_name[len - 1] == '?') {
						is_question = true;
					}

					if ((step == questions) != is_question) {
						do j += 1;
						while (j < num_steps && predicate_step[j].type != TSQueryPredicateStepTypeDone);
						num_args = 0;
						func_name = NULL;
						is_question = false;
						continue;
					}
				} else {
					lua_pushlstring(L, pred_name, len);
					++num_args;
				}
				break;
			}
			case TSQueryPredicateStepTypeCapture: {
				uint32_t len;
				char const *name = ts_query_capture_name_for_id(q, predicate_step[j].value_id, &len);
				get_capture_from_table(L, capture_table_index, name, len);
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
					result = false;
					goto break_predicate_loop;
				}

				num_args = 0;
				func_name = NULL;
				is_question = false;
				break;
			}
		}
	}

break_predicate_loop:
	lua_settop(L, initial_stack_top);
	return result;
}

/* @teal-inline [[
   interface Match
      id: integer
      pattern_index: integer
      capture_count: integer
      captures: {string:Node|{Node}}
   end
]] */

static int query_iterator_next_match(lua_State *L) {
	// upvalues: Query, Node, Predicate Map, Cursor
	int const initial_query_idx = lua_upvalueindex(1);
	TSQuery *const q = *query_assert(L, initial_query_idx);
	TSQueryCursor *c = *query_cursor_assert(L, lua_upvalueindex(4));
	TSQueryMatch m;
	node_push_tree(L, lua_upvalueindex(2));
	int const tree_index = lua_gettop(L);

	lua_pushvalue(L, initial_query_idx);
	int const query_idx = lua_gettop(L);

	lua_pushvalue(L, lua_upvalueindex(3));
	int const predicate_table_index = lua_gettop(L);

	do {
		if (!ts_query_cursor_next_match(c, &m))
			return 0;
	} while (!do_predicates(L, query_idx, q, tree_index, &m, predicate_table_index));

	lua_createtable(L, 0, 5); // { <match> }
	pushinteger(L, m.id);
	lua_setfield(L, -2, "id"); // { <match> }
	pushinteger(L, m.pattern_index);
	lua_setfield(L, -2, "pattern_index"); // { <match> }
	pushinteger(L, m.capture_count);
	lua_setfield(L, -2, "capture_count");   // { <match> }
	lua_createtable(L, 0, m.capture_count); // { <match> }, { <capture-map> }

	for (uint16_t i = 0; i < m.capture_count; ++i) {
#define push_current_node() node_push(L, tree_index, m.captures[i].node)

		TSQuantifier const quantifier = ts_query_capture_quantifier_for_id(
			q,
			m.pattern_index,
			m.captures[i].index);

		uint32_t len;
		char const *name = ts_query_capture_name_for_id(q, m.captures[i].index, &len);
		lua_pushlstring(L, name, len); // {<capture-map>}, name
		switch (table_rawget(L, -2)) {
		case LUA_TNIL:                     // first node, just set it, or set up table
			lua_pop(L, 1);                 // {<capture-map>}
			lua_pushlstring(L, name, len); // {<capture-map>}, name
			switch (quantifier) {
			case TSQuantifierZero: // unreachable?
				break;
			case TSQuantifierZeroOrOne:
			case TSQuantifierOne:
				push_current_node(); // {<capture-map>}, name, <Node>
				lua_rawset(L, -3);   // {<capture-map>}
				break;
			case TSQuantifierZeroOrMore:
			case TSQuantifierOneOrMore:
				lua_createtable(L, 1, 0); // {<capture-map>}, name, array
				push_current_node();      // {<capture-map>}, name, array, <Node>
				lua_rawseti(L, -2, 1);    // {<capture-map>}, name, array
				lua_rawset(L, -3);        // {<capture-map>}
				break;
			}
			break;
		case LUA_TTABLE: // append it
			// {<capture-map>}, array
			{
				size_t arr_len = length_of(L, -1);
				push_current_node();             // {<capture-map>}, array, <nth Node>
				lua_rawseti(L, -2, arr_len + 1); // {<capture-map>}, array
				lua_pop(L, 1);                   // {<capture-map>}
			}
			break;
		}
#undef push_current_node
	} // { <match> }, { <capture-map> }
	lua_setfield(L, -2, "captures"); // {<match> captures=<capture-map>}
	return 1;
}

static int query_iterator_next_capture(lua_State *L) {
	// upvalues: Query, Node, Predicate Map, Cursor
	int const initial_query_idx = lua_upvalueindex(1);
	TSQuery *const q = *query_assert(L, initial_query_idx);
	TSQueryCursor *c = *query_cursor_assert(L, lua_upvalueindex(4));
	node_push_tree(L, lua_upvalueindex(2));
	int const tree_index = lua_gettop(L);
	TSQueryMatch m;
	uint32_t capture_index;
	lua_pushvalue(L, initial_query_idx);
	int const query_idx = lua_gettop(L);

	lua_pushvalue(L, lua_upvalueindex(3));
	int const predicate_table_idx = lua_gettop(L);

	do {
		if (!ts_query_cursor_next_capture(c, &m, &capture_index))
			return 0;
	} while (!do_predicates(L, query_idx, q, tree_index, &m, predicate_table_idx));

	node_push(
		L, tree_index,
		m.captures[capture_index].node);
	uint32_t len;
	char const *name = ts_query_capture_name_for_id(
		q, m.captures[capture_index].index, &len);
	lua_pushlstring(L, name, len);
	return 2;
}

static void query_cursor_set_range(lua_State *L, TSQueryCursor *c) {
	if (lua_isnumber(L, 4)) {
		ts_query_cursor_set_byte_range(
			c,
			luaL_checkinteger(L, 4),
			luaL_checkinteger(L, 5));
	} else {
		luaL_argcheck(L, lua_type(L, 4) == LUA_TTABLE, 4, "expected number or table");
		luaL_argcheck(L, lua_type(L, 5) == LUA_TTABLE, 5, "expected table");
		expect_field(L, 4, "row", LUA_TNUMBER);
		expect_field(L, 4, "column", LUA_TNUMBER);
		expect_field(L, 5, "row", LUA_TNUMBER);
		expect_field(L, 5, "column", LUA_TNUMBER);

		ts_query_cursor_set_point_range(
			c,
			(TSPoint){.row = lua_tointeger(L, 6), .column = lua_tointeger(L, 7)},
			(TSPoint){.row = lua_tointeger(L, 8), .column = lua_tointeger(L, 9)});
	}
}

/* @teal-export Query.match: function(Query, Node, predicates?: {string:Predicate}, start?: integer | Point, end_?: integer | Point): function(): Match [[
   Iterate over the matches of a given query.
   <code>start</code> and <code>end</code> are optional.
   They must be passed together with the same type, describing either two bytes or two points.
   If passed, the query will be executed within the range denoted.
   If not passed, the default behaviour is to execute the query through the entire range of the node.

   The match object is a record populated with all the information given by treesitter
   <pre>
   interface Match
      id: integer
      pattern_index: integer
      capture_count: integer
      captures: {string:Node|{Node}}
   end
   </pre>

   If a capture can only contain at most one node (as is the case with regular <code>(node) @capture-name</code> patterns and <code>(node)? @capture-name</code> patterns),
   it will either be <code>nil</code> or that <code>Node</code>.

   If a capture can contain multiple nodes (as is the case with <code>(node)* @capture-name</code> and <code>(node)+ @capture-name</code> patterns)
   it will either be <code>nil</code> or an array of <code>Node</code>

   Example:
   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for match in q:match(node) do
      print(match.captures.my_match)
   end
   </pre>

   <code>predicates</code> is a map of functions to determine whether a query matches and/or execute side effects

   Predicates that end in a <code>'?'</code> character will be seen as conditions that must be met for the pattern to be matched.
   Predicates that don't will be seen just as functions to be executed given the matches provided.

   Additionally, you will not have access to the return values of these functions, if you'd like to keep the results of a computation, make your functions have side-effects to write somewhere you can access.

   By default the following predicates are provided.
      <code> (#eq? ...) </code> will match if all arguments provided are equal
      <code> (#match? text pattern) </code> will match the provided <code>text</code> matches the given <code>pattern</code>. Matches are determined by Lua's standard <code>string.match</code> function.
      <code> (#find? text substring) </code> will match if <code>text</code> contains <code>substring</code>. The substring is found with Lua's standard <code>string.find</code>, but the search always starts from the beginning, and pattern matching is disabled. This is equivalent to <code>string.find(text, substring, 0, true)</code>

   Predicate evaluation order:

      Since predicates that end with a `?` affect whether a node matches, these are run first, in the order they appear in the query's source. Once all `?` queries are run, all the non-`?` queries are run in the order they appear in the query's source.

   Example:
   The following snippet will match lua functions that have a single LDoc/EmmyLua style comment above them
   <pre>
   local parser = ltreesitter.require("lua"):parser()

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
         (#is-doc-comment? @the-comment)
      )]]
      :match(root_node, {
         ["is-doc-comment?"] = function(str)
            return str:source():sub(1, 4) == "---@"
         end
      })
   do
      print("Function: " .. match.captures["the-function-name"] .. " has documentation")
      print("   " .. match.captures["the-comment"])
   end
   </pre>
]]*/
static int query_match_factory(lua_State *L) {
	TSQuery *const q = *query_assert(L, 1);
	TSNode n = *node_assert(L, 2);
	TSQueryCursor *c = ts_query_cursor_new();
	if (lua_gettop(L) > 3)
		query_cursor_set_range(L, c);
	lua_settop(L, 3);
	TSQueryCursor **lc = lua_newuserdata(L, sizeof(TSQueryCursor *));
	setmetatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
	*lc = c;
	ts_query_cursor_exec(c, q, n);
	lua_pushcclosure(L, query_iterator_next_match, 4);
	return 1;
}

/* @teal-export Query.capture: function(Query, Node, predicates?: {string:Predicate}, start?: integer | Point, end_?: integer | Point): function(): (Node, string) [[
   Iterate over the captures of a given query in <code>Node</code>, <code>name</code> pairs.
   <code>start</code> and <code>end</code> are optional.
   They must be passed together with the same type, describing either two bytes or two points.
   If passed, the query will be executed within the range denoted.
   If not passed, the default behaviour is to execute the query through the entire range of the node.

   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for capture, name in q:capture(node) do
      print(capture, name) -- => (comment), "my_match"
   end
   </pre>
]]*/
static int query_capture_factory(lua_State *L) {
	TSQuery *const q = *query_assert(L, 1);
	TSNode n = *node_assert(L, 2);
	TSQueryCursor *c = ts_query_cursor_new();
	if (lua_gettop(L) > 3)
		query_cursor_set_range(L, c);
	lua_settop(L, 3);
	TSQueryCursor **lc = lua_newuserdata(L, sizeof(TSQueryCursor *));
	setmetatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
	*lc = c;
	ts_query_cursor_exec(c, q, n);
	lua_pushcclosure(L, query_iterator_next_capture, 4); // prevent the node + query from being gc'ed
	return 1;
}

/* @teal-inline [[
   type Predicate = function(...: string | Node | {Node}): any...
]] */

/* @teal-export Query._with: function(Query, {string:Predicate}): Query [[
]]*/

/* @teal-export Query.exec: function(Query, Node, predicates?: {string:Predicate}, start?: integer | Point, end_?: integer | Point) [[
   Runs a query. That's it. Nothing more, nothing less.
   This is intended to be used with the <code>Query.with</code> method and predicates that have side effects,
   i.e. for when you would use Query.match or Query.capture, but do nothing in the for loop.
   <code>start</code> and <code>end</code> are optional.
   They must be passed together with the same type, describing either two bytes or two points.
   If passed, the query will be executed within the range denoted.
   If not passed, the default behaviour is to execute the query through the entire range of the node.

   <pre>
   local parser = ltreesitter.require("teal"):parser()

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
      :exec(root_node, {["set!"] = function(a, b) _G[a] = b:sub(2, -2) end})

   print(x) -- => foo
   print(y) -- => bar

   </pre>

   If you'd like to interact with the matches/captures of a query, see the Query.match and Query.capture iterators
]]*/
static int query_exec(lua_State *L) {
	TSQuery *const q = *query_assert(L, 1);
	TSNode n = *node_assert(L, 2);

	TSQueryCursor *c = ts_query_cursor_new();
	if (lua_gettop(L) > 3)
		query_cursor_set_range(L, c);

	node_push_tree(L, 2);
	int const parent_idx = absindex(L, -1);

	TSQueryMatch m;
	ts_query_cursor_exec(c, q, n);
	while (ts_query_cursor_next_match(c, &m)) {
		do_predicates(L, 1, q, parent_idx, &m, 3);
	}

	return 0;
}

static bool predicate_arg_to_string(
	lua_State *L,
	int index,
	MaybeOwnedString *out_str) {
	if (lua_isnil(L, index))
		return false;

	if (lua_type(L, index) == LUA_TSTRING) {
		out_str->owned = false;
		size_t len;
		out_str->data = luaL_checklstring(L, index, &len);
		out_str->length = len;
	} else {
		lua_pushvalue(L, index);
		*out_str = node_get_source(L);
		lua_pop(L, 1);
	}
	return true;
}

static bool ensure_predicate_arg_string(
	lua_State *L,
	int index) {
	index = absindex(L, index);
	MaybeOwnedString str;
	if (!predicate_arg_to_string(L, index, &str))
		return false;
	mos_push_to_lua(L, str);
	mos_free(&str);
	lua_replace(L, index);
	return true;
}

// Predicates
static int eq_predicate(lua_State *L) {
	int const num_args = lua_gettop(L);
	if (num_args < 2) {
		luaL_error(L, "predicate eq? expects 2 or more arguments, got %d", num_args);
	}
	MaybeOwnedString a;
	if (!predicate_arg_to_string(L, 1, &a)) {
		lua_pushboolean(L, false);
		mos_free(&a);
		return 1;
	}
	MaybeOwnedString b;
	for (int i = 2; i <= num_args; ++i) {
		if (!predicate_arg_to_string(L, i, &b)) {
			lua_pushboolean(L, false);
			mos_free(&a);
			mos_free(&b);
			return 1;
		}
		if (!mos_eq(a, b)) {
			lua_pushboolean(L, false);
			mos_free(&a);
			mos_free(&b);
			return 1;
		};
		mos_free(&a);
		a = b;
	}
	mos_free(&a);

	lua_pushboolean(L, true);
	return 1;
}

static inline void open_stringlib(lua_State *L) {
#if LUA_VERSION_NUM <= 501
	lua_getglobal(L, "string");
#else
	luaL_requiref(L, "string", luaopen_string, false);
#endif
}

static int match_predicate(lua_State *L) {
	int const num_args = lua_gettop(L);
	if (num_args != 2) {
		luaL_error(L, "predicate match? expects exactly 2 arguments, got %d", num_args);
	}

	open_stringlib(L);            // string|Node, pattern, string lib
	lua_getfield(L, -1, "match"); // string|Node, pattern, string lib, string.match
	lua_remove(L, -2);            // string|Node, pattern, string.match

	lua_insert(L, -3); // string.match, string|Node, pattern
	lua_insert(L, -2); // string.match, pattern, string|Node
	if (!ensure_predicate_arg_string(L, -1)) {
		lua_pushboolean(L, false);
		return 1;
	}
	// string.match, pattern, string
	lua_insert(L, -2); // string.match, string, pattern
	lua_call(L, 2, 1);

	return 1;
}

static int find_predicate(lua_State *L) {
	int const num_args = lua_gettop(L);
	if (num_args != 2) {
		return luaL_error(L, "predicate find? expects exactly 2 arguments, got %d", num_args);
	}

	open_stringlib(L);           // string|Node, pattern, string lib
	lua_getfield(L, -1, "find"); // string|Node, pattern, string lib, string.find
	lua_remove(L, -2);           // string|Node, pattern, string.find

	lua_insert(L, -3); // string.find, string|Node, pattern
	lua_insert(L, -2); // string.find, pattern, string|Node
	if (!ensure_predicate_arg_string(L, -1)) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_insert(L, -2); // string.find, string, pattern

	pushinteger(L, 0);        // string.find, string, pattern, 0
	lua_pushboolean(L, true); // string.find, string, pattern, 0, true
	lua_call(L, 4, 1);

	return 1;
}

static const luaL_Reg default_query_predicates[] = {
	{"eq?", eq_predicate},
	{"match?", match_predicate},
	{"find?", find_predicate},
	{NULL, NULL}};

void query_setup_predicate_tables(lua_State *L) {
	lua_newtable(L);
	setfuncs(L, default_query_predicates);
	set_registry_field(L, default_predicate_field);
}

static const luaL_Reg query_methods[] = {
	{"pattern_count", query_pattern_count},
	{"capture_count", query_capture_count},
	{"string_count", query_string_count},
	{"match", query_match_factory},
	{"capture", query_capture_factory},
	{"exec", query_exec},
	{NULL, NULL}};

static const luaL_Reg query_metamethods[] = {
	{"__gc", query_gc},
	{NULL, NULL}};

void query_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_QUERY_METATABLE_NAME, query_metamethods, query_methods);
}
