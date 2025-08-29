#include "types.h"
#include "luautils.h"
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
