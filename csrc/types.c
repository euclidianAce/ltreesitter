#include <ltreesitter/types.h>
#include "luautils.h"
#include <string.h>
#include <lauxlib.h>

ltreesitter_SourceText *ltreesitter_check_source_text(lua_State *L, int index) {
	return luaL_checkudata(L, index, LTREESITTER_SOURCE_TEXT_METATABLE_NAME);
}

#ifdef LOG_GC
static int ltreesitter_source_text_gc(lua_State *L) {
	ltreesitter_SourceText *st = ltreesitter_check_source_text(L, -1);
	printf("SourceText %p is being garbage collected\n", (void const *)st);
	printf("   text: %.*s...\n", (int)st->length >= 10 ? 10 : (int)st->length, st->text);
	return 0;
}
#endif

static int ltreesitter_source_text_tostring(lua_State *L) {
	ltreesitter_SourceText *st = ltreesitter_check_source_text(L, -1);
	if (!st) {
		lua_pushlstring(L, "", 0);
		return 1;
	}

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

ltreesitter_SourceText *ltreesitter_source_text_push_uninitialized(lua_State *L, uint32_t len) {
	ltreesitter_SourceText *src_text = lua_newuserdata(L, sizeof(ltreesitter_SourceText) + len);
	if (!src_text) {
		lua_pushnil(L);
		return NULL;
	}
	setmetatable(L, LTREESITTER_SOURCE_TEXT_METATABLE_NAME);
	src_text->length = len;
	return src_text;
}

ltreesitter_SourceText *ltreesitter_source_text_push(lua_State *L, uint32_t len, const char *src) {
	ltreesitter_SourceText *st = ltreesitter_source_text_push_uninitialized(L, len);
	if (!st)
		return NULL;
	memcpy(&st->text, src, len);
	return st;
}

void ltreesitter_create_source_text_metatable(lua_State *L) {
	create_metatable(L, LTREESITTER_SOURCE_TEXT_METATABLE_NAME, source_text_metamethods, NULL);
}
