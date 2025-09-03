#include "luautils.h"
#include "node.h"
#include "object.h"
#include "query.h"
#include "query_cursor.h"
#include "types.h"

static int query_cursor_gc(lua_State *L) {
	TSQueryCursor *c = *query_cursor_assert(L, 1);
	ts_query_cursor_delete(c);
	return 0;
}

/* @teal-export QueryCursor.did_exceed_match_limit: function(QueryCursor): boolean */
static int did_exceed_match_limit(lua_State *L) {
	TSQueryCursor const *qc = *query_cursor_assert(L, 1);
	lua_pushboolean(L, ts_query_cursor_did_exceed_match_limit(qc));
	return 1;
}

/* @teal-export QueryCursor.match_limit: function(QueryCursor): integer */
static int match_limit(lua_State *L) {
	TSQueryCursor const *qc = *query_cursor_assert(L, 1);
	lua_pushinteger(L, ts_query_cursor_match_limit(qc));
	return 1;
}

/* @teal-export QueryCursor.set_match_limit: function(QueryCursor, integer) */
static int set_match_limit(lua_State *L) {
	TSQueryCursor *qc = *query_cursor_assert(L, 1);
	lua_Integer lim = luaL_checkinteger(L, 2);
	luaL_argcheck(L, lim >= 0, 2, "expected a non-negative integer");
	ts_query_cursor_set_match_limit(qc, (uint32_t)lim);
	return 0;
}

/* @teal-export QueryCursor.set_byte_range: function(QueryCursor, start: integer, end_: integer): boolean [[
   returns true when the given range was non-empty
]] */
static int set_byte_range(lua_State *L) {
	TSQueryCursor *qc = *query_cursor_assert(L, 1);
	lua_Integer start = luaL_checkinteger(L, 2);
	lua_Integer end = luaL_checkinteger(L, 3);
	luaL_argcheck(L, start >= 0, 2, "expected a non-negative integer");
	luaL_argcheck(L, end >= 0, 3, "expected a non-negative integer");
	lua_pushboolean(L, ts_query_cursor_set_byte_range(qc, (uint32_t)start, (uint32_t)end));
	return 1;
}

/* @teal-export QueryCursor.set_point_range: function(QueryCursor, start: Point, end_: Point): boolean [[
   returns true when the given range was non-empty
]] */
static int set_point_range(lua_State *L) {
	TSQueryCursor *qc = *query_cursor_assert(L, 1);
	TSPoint start = topoint(L, 2);
	TSPoint end = topoint(L, 3);
	lua_pushboolean(L, ts_query_cursor_set_point_range(qc, start, end));
	return 1;
}

/* @teal-export QueryCursor.next_match_without_executing_predicates: function(QueryCursor): Match */
static int next_match_without_executing_predicates(lua_State *L) {
	lua_settop(L, 1);
	luaL_checkstack(L, 5, "Internal allocation error");

	TSQueryCursor *qc = *query_cursor_assert(L, 1); // cursor
	TSQueryMatch match;
	if (!ts_query_cursor_next_match(qc, &match)) {
		lua_pushnil(L);
		return 1;
	}
	push_kept(L, 1); // cursor, {query, node}
	lua_rawgeti(L, -1, 1); // cursor, {query, node}, query
	TSQuery const *q = *query_assert(L, -1);

	lua_rawgeti(L, -2, 2); // cursor, {query, node}, query, node
	push_kept(L, -1); // cursor, {query, node}, query, node, tree

	push_match(L, match, q, lua_gettop(L));
	return 1;
}

/* @teal-export QueryCursor.next_capture_without_executing_predicates: function(QueryCursor): Node, string */
static int next_capture_without_executing_predicates(lua_State *L) {
	lua_settop(L, 1);
	luaL_checkstack(L, 5, "Internal allocation error");

	TSQueryCursor *qc = *query_cursor_assert(L, 1); // cursor
	TSQueryMatch match;
	uint32_t capture_index;
	if (!ts_query_cursor_next_capture(qc, &match, &capture_index)) {
		lua_pushnil(L);
		return 1;
	}
	push_kept(L, 1); // cursor, {query, node}
	lua_rawgeti(L, -1, 1); // cursor, {query, node}, query
	TSQuery const *q = *query_assert(L, -1);

	lua_rawgeti(L, -2, 2); // cursor, {query, node}, query, node
	push_kept(L, -1); // cursor, {query, node}, query, node, tree

	TSQueryCapture cap = match.captures[capture_index];
	node_push(L, -1, cap.node);

	uint32_t name_len;
	char const *capture_name = ts_query_capture_name_for_id(q, cap.index, &name_len);
	lua_pushlstring(L, capture_name, name_len);
	return 2;
}

/* @teal-export QueryCursor.remove_match: function(QueryCursor, integer) */
static int remove_match(lua_State *L) {
	TSQueryCursor *qc = *query_cursor_assert(L, 1); // cursor
	lua_Integer match_id = luaL_checkinteger(L, 2);
	luaL_argcheck(L, match_id >= 0, 2, "expected a non-negative integer (a match id)");
	ts_query_cursor_remove_match(qc, match_id);
	return 0;
}

/* @teal-export QueryCursor.set_max_start_depth: function(QueryCursor, integer) */
static int set_max_start_depth(lua_State *L) {
	TSQueryCursor *qc = *query_cursor_assert(L, 1);
	lua_Integer depth = luaL_checkinteger(L, 2);
	luaL_argcheck(L, depth >= 0, 2, "expected a non-negative integer");
	ts_query_cursor_set_max_start_depth(qc, depth);
	return 0;
}

static const luaL_Reg query_cursor_methods[] = {
	{"did_exceed_match_limit", did_exceed_match_limit},
	{"match_limit", match_limit},
	{"set_match_limit", set_match_limit},
	{"set_byte_range", set_byte_range},
	{"set_point_range", set_point_range},
	{"next_match_without_executing_predicates", next_match_without_executing_predicates},
	{"next_capture_without_executing_predicates", next_capture_without_executing_predicates},
	{"set_max_start_depth", set_max_start_depth},
	{"remove_match", remove_match},
	{NULL, NULL}};

static const luaL_Reg query_cursor_metamethods[] = {
	{"__gc", query_cursor_gc},
	{NULL, NULL}};

void query_cursor_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_QUERY_CURSOR_METATABLE_NAME, query_cursor_metamethods, query_cursor_methods);
}
