#include "luautils.h"
#include "node.h"
#include "types.h"

#include <lauxlib.h>
#include <string.h>

#ifdef LOG_GC
static int ltreesitter_source_text_gc(lua_State *L) {
	SourceText *st = source_text_assert(L, -1);
	printf("SourceText %p is being garbage collected\n", (void const *)st);
	printf("   text: %.*s...\n", (int)st->length >= 10 ? 10 : (int)st->length, st->text);
	return 0;
}
#endif

static int ltreesitter_source_text_tostring(lua_State *L) {
	SourceText *st = source_text_assert(L, -1);
	lua_pushlstring(L, st->text, st->length);
	return 1;
}

static const luaL_Reg source_text_metamethods[] = {
	{"__tostring", ltreesitter_source_text_tostring},
#ifdef LOG_GC
	{"__gc", ltreesitter_source_text_gc},
#endif
	{NULL, NULL},
};

SourceText *source_text_push_uninitialized(lua_State *L, uint32_t len) {
	SourceText *src_text = lua_newuserdata(L, sizeof(SourceText) + len);
	if (!src_text) {
		lua_pushnil(L);
		return NULL;
	}
	setmetatable(L, LTREESITTER_SOURCE_TEXT_METATABLE_NAME);
	src_text->length = len;
	return src_text;
}

SourceText *source_text_push(lua_State *L, uint32_t len, char const *src) {
	SourceText *st = source_text_push_uninitialized(L, len);
	if (!st)
		return NULL;
	memcpy(&st->text, src, len);
	return st;
}

void source_text_init_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_SOURCE_TEXT_METATABLE_NAME, source_text_metamethods, NULL);
}

TSPoint topoint(lua_State *L, int const idx) {
	int const absidx = absindex(L, idx);
	expect_field(L, absidx, "row", LUA_TNUMBER);
	expect_field(L, absidx, "column", LUA_TNUMBER);
	uint32_t const row = lua_tonumber(L, -2);
	uint32_t const col = lua_tonumber(L, -1);
	lua_pop(L, 2);
	return (TSPoint){
		.row = row,
		.column = col,
	};
}

void push_match(lua_State *L, TSQueryMatch m, TSQuery const *q, int tree_index) {
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
}
