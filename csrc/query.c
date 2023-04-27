
#include <stddef.h>
#include <string.h>
#include <inttypes.h>
#include <tree_sitter/api.h>

#include "luautils.h"
#include "object.h"
#include <ltreesitter/node.h>
#include <ltreesitter/parser.h>
#include <ltreesitter/query.h>
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

static inline void offset_to_pos(const char *src, uint32_t offset, uint32_t *row, uint32_t *col) {
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

// returns true when there is no error
bool ltreesitter_handle_query_error(
	lua_State *L,
	TSQuery *q,
	uint32_t err_offset,
	TSQueryError err_type,
	const char *query_src) {
	if (q)
		return true;
	char slice[16] = {0};
	strncpy(slice, &query_src[err_offset >= 10 ? err_offset - 10 : err_offset], 15);
	uint32_t row, col;
	offset_to_pos(query_src, err_offset, &row, &col);
	char buf[128];

#define CASE(typ, str) \
	case typ: \
		snprintf(buf, sizeof buf, "Query " str " error %" PRIu32 ":%" PRIu32 ": around '%s' (at byte offset %" PRIu32 ")", row, col, slice, err_offset); \
		lua_pushstring(L, buf); \
		break

	switch (err_type) {
		case TSQueryErrorNone: return true; // unreachable
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

void ltreesitter_push_query(
	lua_State *L,
	const TSLanguage *lang,
	const char *src,
	const size_t src_len,
	TSQuery *q,
	int parent_idx) {

	parent_idx = absindex(L, parent_idx);
	ltreesitter_Query *lq = lua_newuserdata(L, sizeof(struct ltreesitter_Query)); // query
	setmetatable(L, LTREESITTER_QUERY_METATABLE_NAME);
	lua_pushvalue(L, -1); // query, query
	set_parent(L, parent_idx); // query
	lq->lang = lang;

	lq->source = ltreesitter_source_text_push(L, src_len, src); // query, source text
	lua_pushvalue(L, -2); // query, source text, query
	set_parent(L, -2); // query, source text
	lua_pop(L, 1); // query

	lq->query = q;
}

static void push_query_copy(lua_State *L, int query_idx) {
	ltreesitter_Query *orig = ltreesitter_check_query(L, query_idx);
	push_parent(L, query_idx); // parent

	uint32_t err_offset;
	TSQueryError err_type;
	TSQuery *q = ts_query_new(
		orig->lang,
		orig->source->text,
		orig->source->length,
		&err_offset,
		&err_type);

	if (!ltreesitter_handle_query_error(L, q, err_offset, err_type, orig->source->text))
		return;

	ltreesitter_Query *lq = lua_newuserdata(L, sizeof(struct ltreesitter_Query)); // parent, query
	setmetatable(L, LTREESITTER_QUERY_METATABLE_NAME);

	lq->lang = orig->lang;
	lq->query = q;
	lq->source = ltreesitter_source_text_push(L, orig->source->length, orig->source->text); // parent, query, sourcetext
	set_parent(L, -1); // parent, query
	lua_pushvalue(L, -1); // parent, query, query
	set_parent(L, -2); // parent, query
	lua_remove(L, -2); // query
}

static int query_gc(lua_State *L) {
	ltreesitter_Query *q = ltreesitter_check_query(L, 1);
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

// the capture table is just a map of @name -> Node
static void add_capture_to_table(
	lua_State *L,
	int table_index,

	const char *key,
	size_t key_len,

	int parent_index,
	TSNode value
) {
	lua_pushlstring(L, key, key_len);
	ltreesitter_push_node(L, parent_index, value);
	lua_rawset(L, table_index);
}

static void get_capture_from_table(
	lua_State *L,
	int table_index,
	const char *key,
	size_t key_len
) {
	lua_pushlstring(L, key, key_len);
	lua_rawget(L, table_index);
}

// TODO: this function cannot handle upvalue indexes for query_idx nor tree_idx
static bool do_predicates(
	lua_State *L,
	int query_idx,
	const TSQuery *const q,
	int tree_idx,
	const TSQueryMatch *const m) {
	query_idx = absindex(L, query_idx);
	tree_idx = absindex(L, tree_idx);
	bool result = true;

#define RETURN(value) do { result = (value); goto deferred; } while (0)
#define return plz use "RETURN" macro

	const int initial_stack_top = lua_gettop(L);

	// store captures as a map of {string:Node} where the keys are the
	// "@name"s and the values are the matched nodes
	lua_createtable(L, 0, m->capture_count);
	int capture_table_index = initial_stack_top + 1;
	for (uint32_t i = 0; i < m->capture_count; ++i) {
		TSQueryCapture capture = m->captures[i];
		uint32_t name_len;
		const char *name = ts_query_capture_name_for_id(q, capture.index, &name_len);

		add_capture_to_table(L, capture_table_index, name, name_len, tree_idx, capture.node);
	}

	// {
		// lua_pushvalue(L, capture_table_index);
		// lua_setglobal(L, "__captures");
		// luaL_dostring(L, "print(require'inspect'(__captures))");
	// }

	uint32_t num_steps;
	const TSQueryPredicateStep *const predicate_step = ts_query_predicates_for_pattern(q, m->pattern_index, &num_steps);
	bool is_question = false; // if a predicate is a question then the query should only match if it results in a truthy value
	/* TODO:
		Currently queries are evaluated in order
		So if a question comes after something with a side effect,
		the side effect happens even if the query doesn't match
		*/

	{
		// count the max number of args we need to prep for
		int current_args = 0;
		bool need_func_name = true;
		int max_args = 0;
		for (uint32_t j = 0; j < num_steps; ++j) {
			switch (predicate_step[j].type) {
			case TSQueryPredicateStepTypeString:
				if (need_func_name)
					need_func_name = false;
				else
					current_args += 1;
				break;

			case TSQueryPredicateStepTypeCapture:
				current_args += 1;
				break;

			case TSQueryPredicateStepTypeDone:
				if (current_args > max_args)
					max_args = current_args;
				need_func_name = true;
				current_args = 0;
				break;
			}
		}
		if (!lua_checkstack(L, max_args))
			luaL_error(L, "Internal lua error, unable to handle %d arguments to predicate", max_args);
	}

	int num_args = 0;
	const char *func_name = NULL;
	for (uint32_t j = 0; j < num_steps; ++j) {
		switch (predicate_step[j].type) {
		case TSQueryPredicateStepTypeString: {
			// literal strings

			uint32_t len;
			const char *pred_name = ts_query_string_value_for_id(q, predicate_step[j].value_id, &len);
			if (!func_name) {
				// Find predicate
				push_query_predicates(L, query_idx);
				lua_getfield(L, -1, pred_name);
				if (lua_isnil(L, -1)) {
					lua_pop(L, 1);
					// try to grab default value
					push_default_predicate_table(L);
					lua_getfield(L, -1, pred_name);
					lua_remove(L, -2);
					if (lua_isnil(L, -1))
						luaL_error(L, "Query doesn't have predicate '%s'", pred_name);
				}
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
			uint32_t len;
			const char *name = ts_query_capture_name_for_id(q, predicate_step[j].value_id, &len);
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
				RETURN(false);
			}

			num_args = 0;
			func_name = NULL;
			is_question = false;
			break;
		}
	}

#undef RETURN
#undef return
deferred:
	lua_settop(L, initial_stack_top);
	return result;
}

/* @teal-inline [[
   record Match
      id: integer
      pattern_index: integer
      capture_count: integer
      captures: {string:Node|{Node}}
   end
]] */

#define INTERNAL_PARENT_CHECK_ERR_MSG "Internal error: node parent is not a tree"

static int query_iterator_next_match(lua_State *L) {
	// upvalues: Query, Node, Cursor
	const int initial_query_idx = lua_upvalueindex(1);
	ltreesitter_Query *const q = ltreesitter_check_query(L, initial_query_idx);
	ltreesitter_QueryCursor *c = ltreesitter_check_query_cursor(L, lua_upvalueindex(3));
	TSQueryMatch m;
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	(void)ltreesitter_check_tree(L, parent_idx, INTERNAL_PARENT_CHECK_ERR_MSG);

	lua_pushvalue(L, initial_query_idx);
	const int query_idx = lua_gettop(L);

	do {
		if (!ts_query_cursor_next_match(c->query_cursor, &m))
			return 0;
	} while (!do_predicates(L, query_idx, q->query, parent_idx, &m));

	lua_createtable(L, 0, 5); // { <match> }
	pushinteger(L, m.id);
	lua_setfield(L, -2, "id"); // { <match> }
	pushinteger(L, m.pattern_index);
	lua_setfield(L, -2, "pattern_index"); // { <match> }
	pushinteger(L, m.capture_count);
	lua_setfield(L, -2, "capture_count"); // { <match> }
	lua_createtable(L, 0, m.capture_count); // { <match> }, { <capture-map> }

	for (uint16_t i = 0; i < m.capture_count; ++i) {
#define push_current_node() do { \
	ltreesitter_push_node( \
		L, parent_idx, \
		m.captures[i].node); \
} while (0)

		const TSQuantifier quantifier = ts_query_capture_quantifier_for_id(
			c->query->query,
			m.pattern_index,
			m.captures[i].index);

		uint32_t len;
		const char *name = ts_query_capture_name_for_id(c->query->query, m.captures[i].index, &len);
		lua_pushlstring(L, name, len); // {<capture-map>}, name
		switch (table_rawget(L, -2)) {
		case LUA_TNIL: // first node, just set it, or set up table
			lua_pop(L, 1);                     // {<capture-map>}
			lua_pushlstring(L, name, len);     // {<capture-map>}, name
			switch (quantifier) {
			case TSQuantifierZero: // unreachable?
				break;
			case TSQuantifierZeroOrOne:
			case TSQuantifierOne:
				push_current_node();           // {<capture-map>}, name, <Node>
				lua_rawset(L, -3);             // {<capture-map>}
				break;
			case TSQuantifierZeroOrMore:
			case TSQuantifierOneOrMore:
				lua_createtable(L, 1, 0);      // {<capture-map>}, name, array
				push_current_node();           // {<capture-map>}, name, array, <Node>
				lua_rawseti(L, -2, 1);         // {<capture-map>}, name, array
				lua_rawset(L, -3);             // {<capture-map>}
				break;
			}
			break;
		case LUA_TTABLE: // append it
			// {<capture-map>}, array
			{
				size_t len = length_of(L, -1);
				push_current_node();     // {<capture-map>}, array, <nth Node>
				lua_rawseti(L, -2, len + 1); // {<capture-map>}, array
				lua_pop(L, 1);           // {<capture-map>}
			}
			break;
		}
#undef push_current_node
	} // { <match> }, { <capture-map> }
	lua_setfield(L, -2, "captures"); // {<match> captures=<capture-map>}
	return 1;
}

static int query_iterator_next_capture(lua_State *L) {
	// upvalues: Query, Node, Cursor
	const int initial_query_idx = lua_upvalueindex(1);
	ltreesitter_Query *const q = ltreesitter_check_query(L, initial_query_idx);
	TSQueryCursor *c = ltreesitter_check_query_cursor(L, lua_upvalueindex(3))->query_cursor;
	push_parent(L, lua_upvalueindex(2));
	const int parent_idx = lua_gettop(L);
	(void)ltreesitter_check_tree(L, -1, INTERNAL_PARENT_CHECK_ERR_MSG);
	TSQueryMatch m;
	uint32_t capture_index;
	lua_pushvalue(L, initial_query_idx);
	const int query_idx = lua_gettop(L);

	do {
		if (!ts_query_cursor_next_capture(c, &m, &capture_index))
			return 0;
	} while (!do_predicates(L, query_idx, q->query, parent_idx, &m));

	ltreesitter_push_node(
		L, parent_idx,
		m.captures[capture_index].node);
	uint32_t len;
	const char *name = ts_query_capture_name_for_id(
		q->query, m.captures[capture_index].index, &len);
	lua_pushlstring(L, name, len);
	return 2;
}

static void query_cursor_set_range(lua_State *L, TSQueryCursor *c) {
	if (lua_isnumber(L, 3)) {
		ts_query_cursor_set_byte_range(
			c,
			luaL_checkinteger(L, 3),
			luaL_checkinteger(L, 4));
	} else {
		luaL_argcheck(L, lua_type(L, 3) == LUA_TTABLE, 3, "expected number or table");
		luaL_argcheck(L, lua_type(L, 4) == LUA_TTABLE, 4, "expected table");
		expect_field(L, 3, "row", LUA_TNUMBER);
		expect_field(L, 3, "column", LUA_TNUMBER);
		expect_field(L, 4, "row", LUA_TNUMBER);
		expect_field(L, 4, "column", LUA_TNUMBER);

		ts_query_cursor_set_point_range(
			c,
			(TSPoint){.row = lua_tonumber(L, 5), .column = lua_tonumber(L, 6)},
			(TSPoint){.row = lua_tonumber(L, 7), .column = lua_tonumber(L, 8)}
		);
	}
}

/* @teal-export Query.match: function(Query, Node, start: integer | Point, end_: integer | Point): function(): Match [[
   Iterate over the matches of a given query.
   <code>start</code> and <code>end</code> are optional.
   They must be passed together with the same type, describing either two bytes or two points.
   If passed, the query will be executed within the range denoted.
   If not passed, the default behaviour is to execute the query through the entire range of the node.

   The match object is a record populated with all the information given by treesitter
   <pre>
   type Match = record
      id: integer
      pattern_index: integer
      capture_count: integer
      captures: {string:Node|{Node}}
   end
   </pre>

   If a capture can only contain at most one node (as is the case with regular <code>(node) @capture-name</code> patterns and <code>(node)? @capture-name</code> patterns),
   it will either be <code>nil</code> or that <code>Node</code>.

   If a capture can containe multiple nodes (as is the case with <code>(node)* @capture-name</code> and <code>(node)+ @capture-name</code> patterns)
   it will either be <code>nil</code> or an array of <code>Node</code>

   Example:
   <pre>
   local q = parser:query[[ (comment) @my_match ]]
   for match in q:match(node) do
      print(match.captures.my_match)
   end
   </pre>
]]*/
static int query_match_factory(lua_State *L) {
	ltreesitter_Query *const q = ltreesitter_check_query(L, 1);
	TSNode n = ltreesitter_check_node(L, 2)->node;
	TSQueryCursor *c = ts_query_cursor_new();
	if (lua_gettop(L) > 2) query_cursor_set_range(L, c);
	lua_settop(L, 2);
	ltreesitter_QueryCursor *lc = lua_newuserdata(L, sizeof(struct ltreesitter_QueryCursor));
	setmetatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
	lc->query_cursor = c;
	lc->query = q;
	ts_query_cursor_exec(c, q->query, n);
	lua_pushcclosure(L, query_iterator_next_match, 3);
	return 1;
}

/* @teal-export Query.capture: function(Query, Node, start: integer | Point, end_: integer | Point): function(): (Node, string) [[
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
	ltreesitter_Query *const q = ltreesitter_check_query(L, 1);
	TSNode n = ltreesitter_check_node(L, 2)->node;
	TSQueryCursor *c = ts_query_cursor_new();
	if (lua_gettop(L) > 2) query_cursor_set_range(L, c);
	lua_settop(L, 2);
	ltreesitter_QueryCursor *lc = lua_newuserdata(L, sizeof(struct ltreesitter_QueryCursor));
	setmetatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME);
	lc->query_cursor = c;
	lc->query = q;
	ts_query_cursor_exec(c, q->query, n);
	lua_pushcclosure(L, query_iterator_next_capture, 3); // prevent the node + query from being gc'ed
	return 1;
}

/* @teal-export Query.with: function(Query, {string:function(...: string | Node | {Node}): any...}): Query [[
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

/* @teal-export Query.exec: function(Query, Node, start: integer | Point, end_: integer | Point) [[
   Runs a query. That's it. Nothing more, nothing less.
   This is intended to be used with the <code>Query.with</code> method and predicates that have side effects,
   i.e. for when you would use Query.match or Query.capture, but do nothing in the for loop.
   <code>start</code> and <code>end</code> are optional.
   They must be passed together with the same type, describing either two bytes or two points.
   If passed, the query will be executed within the range denoted.
   If not passed, the default behaviour is to execute the query through the entire range of the node.

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
	if (lua_gettop(L) > 2) query_cursor_set_range(L, c);

	push_parent(L, 2);
	const int parent_idx = absindex(L, -1);
	(void)ltreesitter_check_tree(L, parent_idx, INTERNAL_PARENT_CHECK_ERR_MSG);

	TSQueryMatch m;
	ts_query_cursor_exec(c, q, n);
	while (ts_query_cursor_next_match(c, &m)) {
		do_predicates(L, 1, q, parent_idx, &m);
	}

	return 0;
}

/* @teal-export Query.source: function(Query): string [[
   Gets the source that the query was initialized with
]]*/
static int query_source(lua_State *L) {
	ltreesitter_Query *q = ltreesitter_check_query(L, 1);
	lua_pushlstring(L, q->source->text, q->source->length);
	return 1;
}

static bool predicate_arg_to_string(
	lua_State *L,
	int index,
	const char **out_ptr,
	size_t *out_len
) {
	if (lua_isnil(L, index))
		return false;

	if (lua_type(L, index) == LUA_TSTRING) {
		*out_ptr = luaL_checklstring(L, index, out_len);
		return true;
	} else {
		const ltreesitter_Node *node = ltreesitter_check_node(L, index);
		push_parent(L, index);
		const ltreesitter_Tree *tree = ltreesitter_check_tree(L, -1, INTERNAL_PARENT_CHECK_ERR_MSG);
		uint32_t start = ts_node_start_byte(node->node);
		uint32_t end = ts_node_end_byte(node->node);
		*out_ptr = tree->source->text + start;
		*out_len = end - start;
		lua_remove(L, -1);
		return true;
	}
}

static bool ensure_predicate_arg_string(
	lua_State *L,
	int index
) {
	index = absindex(L, index);
	size_t len;
	const char *str;
	if (!predicate_arg_to_string(L, index, &str, &len))
		return false;
	lua_pushlstring(L, str, len);
	lua_replace(L, index);
	return true;
}

// Predicates
static int eq_predicate(lua_State *L) {
	const int num_args = lua_gettop(L);
	if (num_args < 2) {
		luaL_error(L, "predicate eq? expects 2 or more arguments, got %d", num_args);
	}
	const char *a;
	size_t a_len;
	if (!predicate_arg_to_string(L, 1, &a, &a_len)) {
		lua_pushboolean(L, false);
		return 1;
	}
	const char *b;
	size_t b_len;

	for (int i = 2; i <= num_args; ++i) {
		if (!predicate_arg_to_string(L, i, &b, &b_len)) {
			lua_pushboolean(L, false);
			return 1;
		}
		if (a_len != b_len || strncmp(a, b, a_len) != 0) {
			lua_pushboolean(L, false);
			return 1;
		};
		a = b;
		a_len = b_len;
	}

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
	const int num_args = lua_gettop(L);
	if (num_args != 2) {
		luaL_error(L, "predicate match? expects exactly 2 arguments, got %d", num_args);
	}

	open_stringlib(L); // string|Node, pattern, string lib
	lua_getfield(L, -1, "match"); // string|Node, pattern, string lib, string.match
	lua_remove(L, -2); // string|Node, pattern, string.match

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
	const int num_args = lua_gettop(L);
	if (num_args != 2) {
		return luaL_error(L, "predicate find? expects exactly 2 arguments, got %d", num_args);
	}

	open_stringlib(L); // string|Node, pattern, string lib
	lua_getfield(L, -1, "find"); // string|Node, pattern, string lib, string.find
	lua_remove(L, -2); // string|Node, pattern, string.find

	lua_insert(L, -3); // string.find, string|Node, pattern
	lua_insert(L, -2); // string.find, pattern, string|Node
	if (!ensure_predicate_arg_string(L, -1)) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_insert(L, -2); // string.find, string, pattern

	pushinteger(L, 0); // string.find, string, pattern, 0
	lua_pushboolean(L, true); // string.find, string, pattern, 0, true
	lua_call(L, 4, 1);

	return 1;
}

static const luaL_Reg default_query_predicates[] = {
	{"eq?", eq_predicate},
	{"match?", match_predicate},
	{"find?", find_predicate},
	{NULL, NULL}};

void ltreesitter_setup_query_predicate_tables(lua_State *L) {
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
	{"source", query_source},
	{NULL, NULL}};

static const luaL_Reg query_metamethods[] = {
	{"__gc", query_gc},
	{NULL, NULL}};

void ltreesitter_create_query_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_QUERY_METATABLE_NAME, query_metamethods, query_methods);
}
